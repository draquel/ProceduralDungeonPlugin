// DungeonBoundaryRules.h — The single source of truth for "is there a boundary between two cells?"
#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"

/**
 * Boundary predicates shared by every output backend.
 *
 * A dungeon's walls, floors and ceilings are not stored in the grid — each backend derives them by
 * asking, per cell face, whether the cell and its neighbour belong to different logical spaces.
 * The tile mapper (DungeonOutput) uses the answer to place wall / floor / ceiling meshes; the voxel
 * stamper (DungeonVoxelIntegration) uses it to decide which faces of the carved void get a solid
 * shell. If those two answers ever disagree, one layer seals space the other leaves open: a wall
 * tile with collision across a doorway the voxels carved, or a carved hole under a tiled floor.
 *
 * These predicates used to be duplicated per backend and drifted three times (doorway to hallway,
 * staircase head to exit corridor, stacked flat hallways). They live here, in DungeonCore, precisely
 * so that they cannot drift again: backends must call these and never re-implement them.
 *
 * Conventions:
 *  - "Current" is the grid coordinate of the cell whose face is being evaluated; (NX,NY,NZ) is the
 *    neighbour across it. Both predicates take the coordinate rather than the cell because the
 *    staircase flank rule needs the face direction.
 *  - Out-of-bounds and solid (Empty / RoomWall) neighbours always need a boundary.
 *  - Door / Entrance cells own their frames: a neighbour of that type never gets a boundary from
 *    this side, and from their own side they open toward the hallway family.
 *  - Room family = Room, Door, Entrance (grouped by RoomIndex).
 *    Hallway family = Hallway, Staircase, StaircaseHead (grouped by HallwayIndex).
 *  - A Staircase / StaircaseHead cell is entered only along its climb axis (StaircaseDirection).
 *    Its two flank faces are always walls, from both sides of the face.
 */
struct DUNGEONCORE_API FDungeonBoundaryRules
{
	/** True for every traversable cell type (anything that is not Empty or RoomWall). */
	static bool IsOpenCell(EDungeonCellType Type);

	/** Room, Door, Entrance — cells that belong to a room and share its RoomIndex. */
	static bool IsRoomFamily(EDungeonCellType Type);

	/** Hallway, Staircase, StaircaseHead — cells that belong to a hallway and share its HallwayIndex. */
	static bool IsHallwayFamily(EDungeonCellType Type);

	/**
	 * True when the horizontal face (DX,DY) of a Staircase / StaircaseHead cell is a flank: a face
	 * perpendicular to the cell's climb axis (StaircaseDirection 0=+X, 1=-X, 2=+Y, 3=-Y). The ramp
	 * and the shaft above it can only be entered along the climb axis, so a flank is never an
	 * opening. False for every other cell type.
	 *
	 * @param Cell The cell whose face is evaluated.
	 * @param DX,DY Unit face direction from the cell toward its neighbour (exactly one non-zero).
	 */
	static bool IsStairSideFace(const FDungeonCell& Cell, int32 DX, int32 DY);

	/**
	 * True when Coord lies on the flank of a Staircase / StaircaseHead cell of the grid: some
	 * cardinal neighbour is a stair-family cell whose face toward Coord is a flank. Whatever is
	 * carved at Coord will be walled off from that ramp or shaft, so the pathfinder keeps hallway
	 * cells out of such positions and the validator reports any that slipped through.
	 *
	 * @param Grid  The dungeon grid.
	 * @param Coord Cell to test (must be in bounds).
	 */
	static bool IsStairFlankCell(const FDungeonGrid& Grid, const FIntVector& Coord);

	/**
	 * Whether the HORIZONTAL face from Current toward the neighbour at (NX,NY,NZ) needs a wall.
	 *
	 * Rules, in order:
	 *  1. Out of bounds, Empty or RoomWall neighbour: wall.
	 *  2. Staircase flank on either side of the face (see IsStairSideFace): wall. A corridor that
	 *     runs alongside a ramp, a landing beside another staircase's shaft, or a doorway that
	 *     happens to face a shaft is walled off from it. This precedes the door rules on purpose:
	 *     a door frame must never open onto the side of a ramp. Without this the hallway-family
	 *     merge below opened the side of every ramp to any hallway that touched it, and the tile
	 *     mapper patched it locally (one-sided) while the voxel stamper did not — the third drift
	 *     this file exists to prevent.
	 *  3. Door / Entrance neighbour: no wall (it places its own frame).
	 *  4. Door / Entrance current facing the hallway family: no wall (the doorway itself).
	 *  5. Same room (room family, equal RoomIndex): no wall.
	 *  6. Hallway family on both sides: no wall (hallways merge), EXCEPT a StaircaseHead only
	 *     opens toward cells of its own hallway (equal HallwayIndex), which includes the plain
	 *     Hallway it exits into.
	 *  7. Anything else (room vs hallway, different rooms): wall.
	 *
	 * The caller may layer further staircase placement policy on top (entry / climb faces toward
	 * rooms); that is placement policy, not space membership, and stays in the backend.
	 *
	 * @param Grid     The dungeon grid.
	 * @param Current  Grid coordinate of the cell whose face is being evaluated (must be in bounds).
	 * @param NX,NY,NZ Grid coordinate of the horizontal neighbour across that face (may be OOB).
	 * @return True if a wall belongs on that face.
	 */
	static bool NeedsWall(const FDungeonGrid& Grid, const FIntVector& Current, int32 NX, int32 NY, int32 NZ);

	/**
	 * Whether the VERTICAL face from Current toward the neighbour at (NX,NY,NZ) needs a floor or
	 * ceiling. Rules 1 (solid / out of bounds) and 5 (same room) match NeedsWall; the door rules
	 * and the flank rule are walls-only, since a doorway is an opening in a wall and a door frame
	 * is never a hole in a floor or ceiling (applying them vertically dropped the floor of every
	 * hallway routed over a Door cell and the ceiling beneath one). Rule 5 differs: two same-hallway cells stacked
	 * vertically are open to each other ONLY when at least one is a Staircase / StaircaseHead (the
	 * shaft the ramp climbs through). Two FLAT Hallway cells stacked at different Z are separate
	 * walkable levels and each keeps its floor and ceiling; suppressing the boundary there dropped
	 * the upper hallway floor into the level below.
	 *
	 * @param Grid     The dungeon grid.
	 * @param Current  Grid coordinate of the cell whose face is being evaluated (must be in bounds).
	 * @param NX,NY,NZ Grid coordinate of the vertical neighbour across that face (may be OOB).
	 * @return True if a floor / ceiling belongs on that face.
	 */
	static bool NeedsVerticalBoundary(const FDungeonGrid& Grid, const FIntVector& Current, int32 NX, int32 NY, int32 NZ);
};
