#include "DungeonModuleTools.h"
#include "DungeonEditorModule.h"

#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

#include "Editor.h"
#include "Selection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "DungeonModuleTools"

FDungeonModuleElement UDungeonModuleTools::MakeElement(
	UStaticMesh* Mesh,
	const FTransform& ComponentWorld,
	const FTransform& Anchor,
	UMaterialInterface* MaterialOverride)
{
	FDungeonModuleElement Element;
	Element.Mesh = Mesh;
	// ComponentWorld expressed in the anchor's frame. Runtime expansion (ADungeonActor) does
	// RelativeTransform * Anchor, which reproduces ComponentWorld exactly.
	Element.RelativeTransform = ComponentWorld.GetRelativeTransform(Anchor);
	Element.MaterialOverride = MaterialOverride;
	return Element;
}

int32 UDungeonModuleTools::CollectElements(
	const TArray<AActor*>& Actors,
	const FTransform& Anchor,
	TArray<FDungeonModuleElement>& OutElements)
{
	int32 Count = 0;
	for (const AActor* Actor : Actors)
	{
		if (!Actor)
		{
			continue;
		}
		TArray<UStaticMeshComponent*> Components;
		Actor->GetComponents<UStaticMeshComponent>(Components);
		for (UStaticMeshComponent* SMC : Components)
		{
			// Skip instanced components — their many instances have no single transform to capture.
			if (!SMC || SMC->IsA<UInstancedStaticMeshComponent>() || !SMC->GetStaticMesh())
			{
				continue;
			}

			UMaterialInterface* MaterialOverride = nullptr;
			if (SMC->OverrideMaterials.Num() > 0 && SMC->OverrideMaterials[0] != nullptr)
			{
				MaterialOverride = SMC->OverrideMaterials[0];
			}

			OutElements.Add(MakeElement(SMC->GetStaticMesh(), SMC->GetComponentTransform(), Anchor, MaterialOverride));
			++Count;
		}
	}
	return Count;
}

UDungeonTileModule* UDungeonModuleTools::CreateModuleAsset(
	const FString& PackagePath,
	const FString& AssetName,
	const TArray<FDungeonModuleElement>& Elements,
	float ReferenceCellSize)
{
	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		return nullptr;
	}

	const FString PackageName = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogDungeonEditor, Error, TEXT("CreateModuleAsset: failed to create package %s"), *PackageName);
		return nullptr;
	}

	UDungeonTileModule* Module = NewObject<UDungeonTileModule>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!Module)
	{
		return nullptr;
	}

	Module->ReferenceCellSize = FMath::Max(ReferenceCellSize, 1.0f);
	Module->Elements = Elements;

	FAssetRegistryModule::AssetCreated(Module);
	Package->MarkPackageDirty();

	// Save immediately (the user picked a location).
	UEditorLoadingAndSavingUtils::SavePackages({ Package }, /*bOnlyDirty=*/false);

	UE_LOG(LogDungeonEditor, Log, TEXT("Created dungeon module %s with %d element(s), ReferenceCellSize=%.0f"),
		*PackageName, Elements.Num(), Module->ReferenceCellSize);
	return Module;
}

UDungeonTileModule* UDungeonModuleTools::CreateModuleFromSelection(float ReferenceCellSize)
{
	if (!GEditor)
	{
		return nullptr;
	}

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	// The anchor is a selected actor that carries NO static mesh (a Target Point, an empty Actor,
	// a Note): its transform is the tile's placement point and it is excluded from capture. With
	// no such actor selected the world origin is the anchor. This lets a module be authored
	// anywhere in a level (next to reference geometry) instead of only at the origin.
	FTransform Anchor = FTransform::Identity;
	AActor* AnchorActor = nullptr;
	TArray<AActor*> MeshActors;
	for (AActor* Actor : SelectedActors)
	{
		if (!Actor)
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);
		bool bHasMesh = false;
		for (const UStaticMeshComponent* SMC : MeshComponents)
		{
			if (SMC && SMC->GetStaticMesh() && !SMC->IsA<UInstancedStaticMeshComponent>())
			{
				bHasMesh = true;
				break;
			}
		}
		if (bHasMesh)
		{
			MeshActors.Add(Actor);
		}
		else if (!AnchorActor)
		{
			AnchorActor = Actor;
		}
	}
	if (AnchorActor)
	{
		// Position + rotation only: a marker scaled up for visibility must not scale the pieces.
		Anchor = AnchorActor->GetActorTransform();
		Anchor.SetScale3D(FVector::OneVector);
		UE_LOG(LogDungeonEditor, Log, TEXT("Module anchor = actor '%s' at %s"),
			*AnchorActor->GetActorLabel(), *Anchor.GetLocation().ToString());
	}

	TArray<FDungeonModuleElement> Elements;
	const int32 NumElements = CollectElements(MeshActors, Anchor, Elements);
	if (NumElements == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoMeshes",
			"Select one or more static-mesh actors to build a dungeon module. Optionally include one "
			"mesh-less actor (e.g. a Target Point) to mark the tile anchor; otherwise the world origin is used."));
		return nullptr;
	}

	// Prompt for a save location.
	FSaveAssetDialogConfig SaveConfig;
	SaveConfig.DialogTitleOverride = LOCTEXT("SaveTitle", "Save Dungeon Tile Module");
	SaveConfig.DefaultPath = TEXT("/Game");
	SaveConfig.DefaultAssetName = TEXT("TM_NewModule");
	SaveConfig.AssetClassNames.Add(UDungeonTileModule::StaticClass()->GetClassPathName());
	SaveConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;

	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	const FString SaveObjectPath = ContentBrowser.Get().CreateModalSaveAssetDialog(SaveConfig);
	if (SaveObjectPath.IsEmpty())
	{
		return nullptr; // user cancelled
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(SaveObjectPath);
	const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);
	const FString AssetName = FPackageName::GetShortName(PackageName);

	UDungeonTileModule* Module = CreateModuleAsset(PackagePath, AssetName, Elements, ReferenceCellSize);
	if (Module)
	{
		TArray<UObject*> ToSync{ Module };
		ContentBrowser.Get().SyncBrowserToAssets(ToSync);
	}
	return Module;
}

#undef LOCTEXT_NAMESPACE
