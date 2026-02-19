#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "RoomSemantics.generated.h"

class UDungeonConfiguration;
struct FDungeonSeed;

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

/**
 * Per-room computed metrics used during type assignment scoring.
 * Not a USTRUCT — internal algorithm data only.
 */
struct DUNGEONCORE_API FRoomSemanticContext
{
	int32 RoomArrayIndex = -1;
	int32 GraphDistance = -1;
	float NormalizedDistance = 0.0f;
	bool bIsLeafNode = false;
	bool bOnMainPath = false;
	bool bSpansMultipleFloors = false;
};

/**
 * FRoomSemantics
 * Static utility functions for entrance selection, graph analysis, and room type assignment.
 */
struct DUNGEONCORE_API FRoomSemantics
{
	/** Pick entrance room index based on Config.EntrancePlacement. Uses RNG for tie-breaking. */
	static int32 SelectEntranceRoom(
		const FDungeonResult& Result,
		const UDungeonConfiguration& Config,
		FDungeonSeed& Seed);

	/** BFS from entrance; populates Room.GraphDistanceFromEntrance, Room.bOnMainPath, returns contexts. */
	static TArray<FRoomSemanticContext> ComputeGraphMetrics(
		FDungeonResult& Result);

	/** Assign room types from Config.RoomTypeRules using priority + scoring. Modifies Result.Rooms[].RoomType. */
	static void AssignRoomTypes(
		FDungeonResult& Result,
		const UDungeonConfiguration& Config,
		const TArray<FRoomSemanticContext>& Contexts,
		FDungeonSeed& Seed);
};
