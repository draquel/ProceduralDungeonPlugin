#pragma once

#include "CoreMinimal.h"
#include "DungeonTileMapper.h" // EDungeonTileType

class UDungeonTileSet;
class UDungeonTileModule;
struct FDungeonWallProfile;

/** One wall-family piece measured against the profile. Distances at the profile's ReferenceCellSize. */
struct DUNGEONOUTPUT_API FDungeonWallProfileMeasure
{
	EDungeonTileType Type = EDungeonTileType::COUNT;
	/** Finished face of the BODY element (largest face area) toward the room: X in the module frame. */
	float BodyInnerX = 0.0f;
	/** Outer plane of the body element. */
	float BodyOuterX = 0.0f;
	/** Innermost / outermost extent over ALL elements (decoration included). */
	float InnerX = 0.0f;
	float OuterX = 0.0f;
	int32 ElementCount = 0;
};

/**
 * Measures wall-family modules against a tileset's FDungeonWallProfile so a wall, a partition and
 * a door frame authored separately can be proven to share one face plane. Pure apart from loading
 * the element meshes for their bounds; the editor validator and the automation tests both call it.
 */
struct DUNGEONOUTPUT_API FDungeonWallProfileConformance
{
	/** WallSegment, WallPartition, DoorFrame, EntranceFrame. Corners are not measured (no face plane). */
	static bool IsWallFamily(EDungeonTileType Type);

	/**
	 * Measure a module in the profile's frame. Element bounds come from each mesh's bounding box
	 * transformed by its RelativeTransform, rescaled from the module's ReferenceCellSize to
	 * ProfileCellSize. Returns false when no element mesh could be loaded.
	 */
	static bool MeasureModule(const UDungeonTileModule& Module, float ProfileCellSize, FDungeonWallProfileMeasure& Out);

	/** The rules for one measured piece; appends one message per violation. */
	static void CheckMeasure(const FDungeonWallProfileMeasure& M, const FDungeonWallProfile& Profile, TArray<FString>& OutIssues);

	/** Every module-backed wall-family slot of the tileset. Single-mesh slots are fitted by the mapper and pass. */
	static void Check(const UDungeonTileSet& TileSet, TArray<FString>& OutIssues);
};
