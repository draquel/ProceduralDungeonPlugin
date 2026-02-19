#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "RoomSemantics.generated.h"

/**
 * FDungeonRoomTypeRule
 * Declarative rule for assigning room types during generation.
 * Used in UDungeonConfiguration::RoomTypeRules.
 */
USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonRoomTypeRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type")
	EDungeonRoomType RoomType = EDungeonRoomType::Generic;

	/** How many rooms of this type to assign. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type", meta=(ClampMin="1"))
	int32 Count = 1;

	/** Higher priority rules are assigned first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type")
	int32 Priority = 0;

	/** Normalized 0-1. 0 = adjacent to entrance, 1 = farthest room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinGraphDistanceFromEntrance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxGraphDistanceFromEntrance = 1.0f;

	/** Prefer leaf nodes in the MST (treasure, secret rooms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type")
	bool bPreferLeafNodes = false;

	/** Prefer rooms on the main path (entrance -> boss). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type")
	bool bPreferMainPath = false;

	/** Require the room to span multiple floors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type")
	bool bRequireMultiFloor = false;

	/** Override minimum size for this room type. Zero = no override. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Room Type")
	FIntVector MinSize = FIntVector::ZeroValue;
};
