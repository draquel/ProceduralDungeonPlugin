#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "DungeonTileSetValidator.generated.h"

/**
 * Asset validation for UDungeonTileSet: on save (and "Validate Assets"), every module-backed
 * wall-family slot is measured against the tileset's wall profile and any piece whose finished
 * face is off the profile is reported as a warning — a door frame authored to a wall that was
 * later swapped is caught here instead of in play.
 */
UCLASS()
class DUNGEONEDITOR_API UDungeonTileSetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* InAsset, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
