#include "DungeonTileSetValidator.h"
#include "DungeonTileSet.h"
#include "Misc/DataValidation.h"

bool UDungeonTileSetValidator::CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* InAsset, FDataValidationContext& InContext) const
{
	return InAsset && InAsset->IsA<UDungeonTileSet>();
}

EDataValidationResult UDungeonTileSetValidator::ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* InAsset, FDataValidationContext& Context)
{
	const UDungeonTileSet* TileSet = Cast<UDungeonTileSet>(InAsset);
	if (!TileSet)
	{
		return EDataValidationResult::NotValidated;
	}

	const TArray<FString> Issues = TileSet->CheckWallProfile();
	if (Issues.Num() == 0)
	{
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}

	for (const FString& Issue : Issues)
	{
		AssetWarning(InAsset, FText::FromString(FString::Printf(TEXT("Wall profile: %s"), *Issue)));
	}
	// Warnings, not errors: the tileset still renders; the pieces just do not meet at one plane.
	return EDataValidationResult::Valid;
}
