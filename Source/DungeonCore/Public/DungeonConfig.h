#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "RoomSemantics.h"
#include "DungeonConfig.generated.h"

/**
 * UDungeonConfiguration
 * Data asset defining all generation parameters. One per dungeon template.
 */
UCLASS(BlueprintType)
class DUNGEONCORE_API UDungeonConfiguration : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Grid ---

	/** Grid dimensions (X width, Y depth, Z floors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grid")
	FIntVector GridSize = FIntVector(30, 30, 5);

	/** World units per cell (cm). 400 = comfortable rooms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grid", meta=(ClampMin="100.0", ClampMax="2000.0"))
	float CellWorldSize = 400.0f;

	// --- Rooms ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rooms", meta=(ClampMin="2", ClampMax="255"))
	int32 RoomCount = 8;

	/** Minimum room dimensions (X width, Y depth, Z height in floors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rooms")
	FIntVector MinRoomSize = FIntVector(3, 3, 1);

	/** Maximum room dimensions (X width, Y depth, Z height in floors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rooms")
	FIntVector MaxRoomSize = FIntVector(7, 7, 2);

	/** Minimum gap between rooms in cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rooms", meta=(ClampMin="0", ClampMax="5"))
	int32 RoomBuffer = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rooms", meta=(ClampMin="10", ClampMax="1000"))
	int32 MaxPlacementAttempts = 100;

	// --- Room Semantics (Phase 3 — defined here for data asset completeness) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room Types")
	TArray<FDungeonRoomTypeRule> RoomTypeRules;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room Types")
	bool bGuaranteeEntrance = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Room Types")
	bool bGuaranteeBossRoom = true;

	// --- Hallways ---

	/** Probability of re-adding non-MST Delaunay edges (creates loops). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hallways", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EdgeReadditionChance = 0.125f;

	/** Cost multiplier when A* reuses an existing hallway cell. Lower = more merging. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hallways", meta=(ClampMin="0.01", ClampMax="1.0"))
	float HallwayMergeCostMultiplier = 0.5f;

	/** Cost multiplier when A* paths through a non-source/dest room. Higher = avoid rooms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hallways", meta=(ClampMin="1.0", ClampMax="10.0"))
	float RoomPassthroughCostMultiplier = 3.0f;

	// --- Staircases (Phase 2) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Staircases", meta=(ClampMin="1", ClampMax="5"))
	int32 StaircaseRiseToRun = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Staircases", meta=(ClampMin="1", ClampMax="5"))
	int32 StaircaseHeadroom = 2;

	// --- Entrance ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Entrance")
	EDungeonEntrancePlacement EntrancePlacement = EDungeonEntrancePlacement::BoundaryEdge;

	// --- Seed ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Seed")
	bool bUseFixedSeed = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Seed", meta=(EditCondition="bUseFixedSeed"))
	int64 FixedSeed = 0;
};
