#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "DungeonVoxelTypes.h"

/**
 * One axis-aligned column of the entrance passage carve: air inside [Center ± HalfExtentXY] from
 * BottomZ to TopZ, with an optional wall shell around it (placed only below the terrain surface).
 */
struct DUNGEONVOXELINTEGRATION_API FDungeonPassageSegment
{
	/** World XY of the column centre (Z unused). */
	FVector Center = FVector::ZeroVector;
	float HalfExtentXY = 0.0f;
	float BottomZ = 0.0f;
	float TopZ = 0.0f;
	/** Wall shell around the column's XY footprint (below the terrain surface only). */
	bool bWalls = true;
	/**
	 * Also shell the column's floor and ceiling: a horizontal passage through the natural cave
	 * layer needs a floor to walk on and a lid, where a vertical shaft is enclosed by its sides.
	 */
	bool bFloorCeilingShell = false;

	/** Grid cell the column stands in when bInsideGrid; otherwise the passage has left the grid footprint. */
	FIntVector Cell = FIntVector::ZeroValue;
	bool bInsideGrid = false;
};

/**
 * FDungeonEntrancePassagePlan
 * Pure geometry for UDungeonEntranceStitcher: which columns a style carves for a result, decided
 * from FDungeonResult::EntranceApproach so the carve stays inside the volume the generator kept
 * clear (keep-out + opening cell). No voxel world involved, so it can be unit tested: the only
 * world input is the surface height, supplied by the caller as a sampler.
 *
 * Style / approach agreement:
 *   VerticalShaft, CaveOpening, Trapdoor  need FromAbove (column over the opening cell's lid)
 *   SlopedTunnel                          needs FromSide  (corridor through the reserved cells to
 *                                         the grid edge, then a ramp outside the footprint)
 *   FromBelow                             no style enters from below yet
 *   None (legacy, or a request the generator could not satisfy) accepts every style and carves
 *   the pre-approach shapes best-effort: nothing guarantees the volume is clear.
 * A mismatch is an error (the stitcher refuses to carve) — a passage that the layout could have
 * blocked is exactly the failure this exists to prevent.
 */
struct DUNGEONVOXELINTEGRATION_API FDungeonEntrancePassagePlan
{
	/** FromSide ramp slope outside the grid: rise per unit of run (0.5 = ~27°, walkable). */
	static constexpr float TunnelRiseOverRun = 0.5f;

	/**
	 * How far ABOVE the sampled surface the carve reaches, in voxels. Carving to exactly the
	 * surface leaves the voxel band containing the isosurface crossing uncarved and the mesher
	 * skins a collidable lid over the mouth.
	 */
	static constexpr float SurfaceOvershootVoxels = 2.0f;

	EDungeonEntranceStyle Style = EDungeonEntranceStyle::VerticalShaft;

	/** The approach the plan was built for: None when legacy or unsatisfied (best effort). */
	EDungeonEntranceApproach Approach = EDungeonEntranceApproach::None;

	TArray<FDungeonPassageSegment> Segments;

	/** Where the passage meets the terrain (surface sampled here). */
	FVector2D MouthXY = FVector2D::ZeroVector;
	float SurfaceZ = 0.0f;
	/** Top of the carve (SurfaceZ + overshoot). Walls are placed only below SurfaceZ. */
	float CarveTopZ = 0.0f;
	/** Where the passage meets the dungeon: the lid / floor plane (vertical) or the tunnel floor (side). */
	float EntranceZ = 0.0f;

	/** Non-empty = refused; nothing must be carved. */
	FString Error;
	/** Non-empty = carved best-effort without a guarantee (logged at Warning). */
	FString Note;

	bool IsValid() const { return Error.IsEmpty(); }

	/** True when Style can be carved for Approach (see the table above). */
	static bool IsStyleCompatible(EDungeonEntranceStyle Style, EDungeonEntranceApproach Approach, FString* OutWhy = nullptr);

	/** The approach a style needs (None for styles with no requirement). */
	static EDungeonEntranceApproach RequiredApproach(EDungeonEntranceStyle Style);

	/**
	 * Build the plan.
	 * @param Result                 Generated dungeon (EntranceCell, EntranceApproach, CellWorldSize, rooms).
	 * @param WorldOffset            World position of grid cell (0,0,0)'s min corner.
	 * @param Style                  Passage style.
	 * @param VoxelSize              Target voxel world's voxel size (overshoot, trapdoor width, ramp run).
	 * @param bStopAtEntranceCellTop Vertical styles stop at the opening cell's TOP plane (tile-dressed
	 *                               dungeons) instead of the entrance cell's bottom. A side tunnel
	 *                               then stops at the room face (the mapper opens the wall) instead of
	 *                               carving the opening cell itself (voxel lining).
	 * @param SideTunnelFloorLift    FromSide only: raise the tunnel floor this much above the cell
	 *                               bottom so it meets the room's WALKABLE floor (the tile slab or
	 *                               stone lining thickness) instead of stepping up into it.
	 * @param SampleSurfaceZ         Terrain surface height at a world XY.
	 */
	static FDungeonEntrancePassagePlan Build(
		const FDungeonResult& Result,
		const FVector& WorldOffset,
		EDungeonEntranceStyle Style,
		float VoxelSize,
		bool bStopAtEntranceCellTop,
		float SideTunnelFloorLift,
		TFunctionRef<float(float WorldX, float WorldY)> SampleSurfaceZ);
};
