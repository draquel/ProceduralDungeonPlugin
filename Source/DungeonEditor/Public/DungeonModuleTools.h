#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DungeonTileModule.h"
#include "DungeonModuleTools.generated.h"

class AActor;
class UStaticMesh;
class UMaterialInterface;

/**
 * Editor authoring helpers for the tile module system (P2).
 *
 * Workflow: arrange static-mesh actors inside one reference cell centred on the WORLD ORIGIN
 * (origin = the tile's placement anchor), select them, and run "Create Dungeon Module from
 * Selection" (level actor right-click menu, or CreateModuleFromSelection here). Each piece is
 * captured relative to the anchor into a new UDungeonTileModule asset. Assign that to a tileset
 * slot via UDungeonTileSet::TileModules.
 *
 * Capture/expansion round-trip: an element's RelativeTransform is ComponentWorld relative to the
 * anchor, and the runtime (ADungeonActor) places it at RelativeTransform * Anchor — so authoring at
 * the origin reproduces exactly at any cell (scaled by the cell/reference ratio).
 */
UCLASS()
class DUNGEONEDITOR_API UDungeonModuleTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Capture one static-mesh component as a module element expressed relative to Anchor. */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Modules")
	static FDungeonModuleElement MakeElement(
		UStaticMesh* Mesh,
		const FTransform& ComponentWorld,
		const FTransform& Anchor,
		UMaterialInterface* MaterialOverride);

	/**
	 * Collect every non-instanced static-mesh component under Actors into module elements,
	 * expressed relative to Anchor. Instanced components (ISM/HISM) are skipped (their multiple
	 * instances have no single transform). Returns the number of elements collected.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Modules")
	static int32 CollectElements(
		const TArray<AActor*>& Actors,
		const FTransform& Anchor,
		TArray<FDungeonModuleElement>& OutElements);

	/**
	 * Create and save a UDungeonTileModule asset at PackagePath/AssetName. Returns the asset, or
	 * null on failure. PackagePath is a content path like "/Game/Foo"; AssetName has no extension.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Modules")
	static UDungeonTileModule* CreateModuleAsset(
		const FString& PackagePath,
		const FString& AssetName,
		const TArray<FDungeonModuleElement>& Elements,
		float ReferenceCellSize);

	/**
	 * Build a module from the current editor actor selection (anchor = world origin), prompting for
	 * a save location. Returns the created asset, or null if nothing was selected or the user
	 * cancelled. Also callable from Python/Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Modules")
	static UDungeonTileModule* CreateModuleFromSelection(float ReferenceCellSize = 400.0f);
};
