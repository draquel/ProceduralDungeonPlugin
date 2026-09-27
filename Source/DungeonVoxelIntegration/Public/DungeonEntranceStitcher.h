#pragma once

#include "CoreMinimal.h"
#include "DungeonVoxelTypes.h"
#include "DungeonEntranceStitcher.generated.h"

struct FDungeonResult;
struct FDungeonPassageSegment;
struct FDungeonEntrancePassagePlan;
struct FDungeonVoxelLattice;
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
 *
 * The carve is two-pass: every segment's interior is collected and set to air first, then the
 * shells are placed only on samples that are neither passage interior nor inside an open dungeon
 * cell. Segments overlap (a ramp is a stack of columns a voxel apart), so a one-pass carve let
 * each column's shell re-solidify its neighbour's interior.
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
	 * @param SideTunnelFloorLift SlopedTunnel only: raise the tunnel floor this much above the
	 *        entrance cell's bottom so it meets the room's walkable floor (tile slab or stone
	 *        lining thickness) instead of stepping up into it.
	 * @return Number of voxels modified, or -1 on failure (including a style / approach mismatch).
	 */
	UFUNCTION(BlueprintCallable, Category = "DungeonVoxelIntegration|Entrance")
	int32 StitchEntrance(
		const FDungeonResult& Result,
		UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset,
		EDungeonEntranceStyle Style,
		UDungeonVoxelConfig* Config,
		bool bStopAtEntranceCellTop = false,
		float SideTunnelFloorLift = 0.0f);

private:
	/** Detect terrain surface height at a world XY position using the world mode or vertical sweep. */
	float DetectSurfaceHeight(UVoxelChunkManager* ChunkManager, float WorldX, float WorldY) const;

	/** Pass 1: add every lattice sample inside the column to OutInterior. */
	static void CollectColumnInterior(
		const FDungeonVoxelLattice& Lattice,
		const FDungeonPassageSegment& Segment,
		TSet<FIntVector>& OutInterior);

	/**
	 * Pass 2: place the column's shell (sides, plus floor and ceiling when the segment asks) on
	 * every sample of the widened box that is not passage interior, not inside an open dungeon
	 * cell, and at least SurfaceSkinVoxels below the LOCAL terrain surface. A shell above the
	 * surface would collar the mouth; a shell in the top voxel layers re-textures ground that
	 * is solid anyway to dungeon stone (the ramp's overlapping columns painted a wide stone
	 * apron around the mouth), so those layers keep their natural material.
	 * @param SampleSurfaceZ Terrain surface height at a world XY (cached per lattice column).
	 * @param SurfaceCache   Per-(IX,IY) surface heights, shared across the segments of one carve.
	 * @return Voxels written.
	 */
	int32 PlaceColumnShell(
		class UVoxelEditManager* EditManager,
		const FDungeonVoxelLattice& Lattice,
		const FDungeonPassageSegment& Segment,
		const TSet<FIntVector>& Interior,
		const FDungeonResult& Result,
		const FVector& WorldOffset,
		float VoxelSize,
		TFunctionRef<float(float WorldX, float WorldY)> SampleSurfaceZ,
		TMap<FIntPoint, float>& SurfaceCache,
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
