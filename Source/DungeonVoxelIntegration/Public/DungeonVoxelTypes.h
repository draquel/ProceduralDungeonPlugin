#pragma once

#include "CoreMinimal.h"
#include "DungeonVoxelTypes.generated.h"

/** How the dungeon geometry is applied to the voxel world. */
UENUM(BlueprintType)
enum class EDungeonStampMode : uint8
{
	/** Carved below terrain surface. Entrance stitched to surface above. */
	CarveUnderground,

	/** Replaces a terrain volume entirely — clears the bounding box first. */
	ReplaceRegion,

	/** Merged with existing terrain SDF — only carves where terrain is currently solid. */
	MergeAsStructure,

	/**
	 * CarveUnderground WITHOUT the boundary-voxel pass: open cells become bare voids in the
	 * surrounding terrain. For tile-dressed dungeons (ADungeonActor over the same grid) — the
	 * tile meshes provide the walls/floors/ceilings and the seal, and the boundary pass would
	 * bury them (it thickens INTO the open cell, exactly where the tiles stand).
	 */
	CarveOnly,
};

/** Visual style for connecting the dungeon entrance to the terrain surface. */
UENUM(BlueprintType)
enum class EDungeonEntranceStyle : uint8
{
	/** Straight vertical column from surface down to entrance. */
	VerticalShaft,

	/** Gradual stepped descent toward the dungeon entrance. */
	SlopedTunnel,

	/** Organic noise-displaced opening for a natural cave feel. */
	CaveOpening,

	/** Minimal 1x1 hole punched straight through — no wall shell. */
	Trapdoor,
};
