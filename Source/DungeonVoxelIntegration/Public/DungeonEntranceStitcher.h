#pragma once

#include "CoreMinimal.h"
#include "DungeonVoxelTypes.h"
#include "DungeonEntranceStitcher.generated.h"

struct FDungeonResult;
struct FDungeonPassageSegment;
struct FDungeonEntrancePassagePlan;
class UVoxelChunkManager;
class UDungeonVoxelConfig;

/**
 * Connects the dungeon entrance to the terrain surface.
 *
 * Carves a traversable passage between the voxel terrain surface and the dungeon entrance using
 * one of four visual styles. The geometry comes from FDungeonEntrancePassagePlan, which reads
 * FDungeonResult::EntranceApproach so the carve stays inside the volume the generator kept
 * clear; a style that does not match the dungeon's approach is refused. Works with the
 * VoxelEditManager to create undo-able edits marked as System source.
 */
UCLASS(BlueprintType)
class DUNGEONVOXELINTEGRATION_API UDungeonEntranceStitcher : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Stitch a passage from the terrain surface to the dungeon entrance.
	 * @param Result The generated dungeon data (EntranceCell, EntranceApproach, CellWorldSize).
	 * @param ChunkManager The voxel chunk manager to edit.
	 * @param WorldOffset World-space offset applied to dungeon grid coordinates.
	 * @param Style Visual style for the entrance passage. Must agree with the dungeon's approach:
	 *        VerticalShaft / CaveOpening / Trapdoor need FromAbove, SlopedTunnel needs FromSide;
	 *        a legacy result (approach None) accepts any style best-effort.
	 * @param Config Material and scale configuration.
	 * @param bStopAtEntranceCellTop Stop a vertical carve at the entrance ROOM's lid (the top plane
	 *        of FDungeonResult::GetEntranceOpeningCell) instead of the entrance cell's bottom, and
	 *        a side tunnel at the room face instead of carving the opening cell. For tile-dressed
	 *        dungeons (CarveOnly + ADungeonActor): the mapper opens that lid / wall, and carving
	 *        deeper would run the passage (and its wall shell) through the tiled room interior.
	 * @return Number of voxels modified, or -1 on failure (including a style / approach mismatch).
	 */
	UFUNCTION(BlueprintCallable, Category = "DungeonVoxelIntegration|Entrance")
	int32 StitchEntrance(
		const FDungeonResult& Result,
		UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset,
		EDungeonEntranceStyle Style,
		UDungeonVoxelConfig* Config,
		bool bStopAtEntranceCellTop = false);

private:
	/** Detect terrain surface height at a world XY position using the world mode or vertical sweep. */
	float DetectSurfaceHeight(UVoxelChunkManager* ChunkManager, float WorldX, float WorldY) const;

	/**
	 * Carve a column of air from top Z to bottom Z, with optional wall shell.
	 * @param WallTopZ Walls are only placed on layers BELOW this Z (typically the terrain
	 *        surface): the carve may overshoot the surface to break the mouth open, but a wall
	 *        ring above ground would collar the opening shut for walking characters.
	 */
	int32 CarveColumn(
		class UVoxelEditManager* EditManager,
		UVoxelChunkManager* ChunkManager,
		const FVector& Center,
		float HalfExtentXY,
		float TopZ,
		float BottomZ,
		float VoxelSize,
		bool bPlaceWalls,
		float WallTopZ,
		uint8 WallMaterialID,
		uint8 BiomeID);

	/** The CaveOpening style: a noise-displaced, tapering column around the plan's single segment. */
	int32 CarveCaveOpening(
		class UVoxelEditManager* EditManager,
		UVoxelChunkManager* ChunkManager,
		const FDungeonPassageSegment& Segment,
		float SurfaceZ,
		float VoxelSize,
		UDungeonVoxelConfig* Config);
};
