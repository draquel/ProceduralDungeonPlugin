#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonTileModule.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * One piece of a tile module — a static mesh at a transform relative to the module origin.
 * The module origin is the tile's placement anchor (the same anchor the equivalent single-mesh
 * slot would use: face centre for walls, cell floor for floors, cell top for ceilings), and the
 * RelativeTransform is authored at UDungeonTileModule::ReferenceCellSize.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonModuleElement
{
	GENERATED_BODY()

	/** The piece to place. Null entries are skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Transform of this piece relative to the module origin, authored at ReferenceCellSize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	FTransform RelativeTransform;

	/**
	 * Optional material override applied to slot 0 of the piece. When set, the piece batches into a
	 * render group keyed by (Mesh, MaterialOverride) separate from the same mesh with its defaults.
	 * Null = the mesh's own materials.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;
};

/**
 * A composed dungeon tile built from several static meshes, authored at cell scale.
 *
 * Assign a module to a tile type via UDungeonTileSet::TileModules; it overrides that type's single
 * mesh slot. Unlike a single mesh (which the mapper auto-fits per-axis to the cell, distorting any
 * mesh whose proportions don't match), a module is placed with ONE uniform scale
 * (ActualCellSize / ReferenceCellSize), preserving the authored proportions exactly. This is what
 * lets a designer solve corner-clipping / z-fighting by authoring the geometry correctly rather
 * than fighting placement heuristics.
 *
 * Modules bake down to shared HISM instances at render time (one HISM per unique
 * (mesh, material) across the whole tileset), so N composed tiles cost the same draw calls as N
 * single meshes — no per-tile actors. See Documentation/TILE_MODULE_SYSTEM_PLAN.md.
 */
UCLASS(BlueprintType)
class DUNGEONOUTPUT_API UDungeonTileModule : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Cell size (cm) the element RelativeTransforms were authored at. At runtime every element is
	 * scaled by ActualCellWorldSize / ReferenceCellSize — a single uniform factor, never per-axis
	 * auto-fit. Must be > 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module", meta = (ClampMin = "1.0"))
	float ReferenceCellSize = 400.0f;

	/** The pieces that make up this tile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
	TArray<FDungeonModuleElement> Elements;

	/** Uniform scale that maps this module's authored size onto the target cell size. */
	float GetUniformScale(float ActualCellWorldSize) const
	{
		const float Ref = FMath::Max(ReferenceCellSize, 1.0f);
		return ActualCellWorldSize / Ref;
	}

	/** True if any element has a non-null mesh. */
	bool HasGeometry() const
	{
		for (const FDungeonModuleElement& E : Elements)
		{
			if (!E.Mesh.IsNull()) { return true; }
		}
		return false;
	}
};
