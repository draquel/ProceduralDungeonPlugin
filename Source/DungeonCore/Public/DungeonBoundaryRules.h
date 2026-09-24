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
 *  - "Current" is the cell whose face is being evaluated; (NX,NY,NZ) is the neighbour across it.
 *  - Out-of-bounds and solid (Empty / RoomWall) neighbours always need a boundary.
 *  - Door / Entrance cells own their frames: a neighbour of that type never gets a boundary from
 *    this side, and from their own side they open toward the hallway family.
 *  - Room family = Room, Door, Entrance (grouped by RoomIndex).
 *    Hallway family = Hallway, Staircase, StaircaseHead (grouped by HallwayIndex).
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
	 * Whether the HORIZONTAL face from Current toward the neighbour at (NX,NY,NZ) needs a wall.
	 *
	 * Rules, in order:
	 *  1. Out of bounds, Empty or RoomWall neighbour: wall.
	 *  2. Door / Entrance neighbour: no wall (it places its own frame).
	 *  3. Door / Entrance current facing the hallway family: no wall (the doorway itself).
	 *  4. Same room (room family, equal RoomIndex): no wall.
	 *  5. Hallway family on both sides: no wall (hallways merge), EXCEPT a StaircaseHead only
	 *     opens toward cells of its own hallway (equal HallwayIndex), which includes the plain
	 *     Hallway it exits into.
	 *  6. Anything else (room vs hallway, different rooms): wall.
	 *
	 * The caller may layer staircase-direction rules on top (entry / climb / side faces); those are
	 * placement policy, not space membership, and stay in the backend.
	 *
	 * @param Grid     The dungeon grid.
	 * @param Current  The cell whose face is being evaluated.
	 * @param NX,NY,NZ Grid coordinate of the horizontal neighbour across that face (may be OOB).
	 * @return True if a wall belongs on that face.
	 */
	static bool NeedsWall(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ);

	/**
	 * Whether the VERTICAL face from Current toward the neighbour at (NX,NY,NZ) needs a floor or
	 * ceiling. Rules 1 to 4 match NeedsWall. Rule 5 differs: two same-hallway cells stacked
	 * vertically are open to each other ONLY when at least one is a Staircase / StaircaseHead (the
	 * shaft the ramp climbs through). Two FLAT Hallway cells stacked at different Z are separate
	 * walkable levels and each keeps its floor and ceiling; suppressing the boundary there dropped
	 * the upper hallway floor into the level below.
	 *
	 * @param Grid     The dungeon grid.
	 * @param Current  The cell whose face is being evaluated.
	 * @param NX,NY,NZ Grid coordinate of the vertical neighbour across that face (may be OOB).
	 * @return True if a floor / ceiling belongs on that face.
	 */
	static bool NeedsVerticalBoundary(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ);
};
