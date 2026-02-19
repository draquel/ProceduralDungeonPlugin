// DungeonValidator.h — Validates dungeon generation results for structural correctness
#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"

class UDungeonConfiguration;

/** A single validation issue found in a dungeon result. */
struct DUNGEONCORE_API FDungeonValidationIssue
{
	FString Category;
	FString Description;
	FIntVector Location;
	int32 RoomIndex;

	FDungeonValidationIssue()
		: Location(FIntVector::ZeroValue)
		, RoomIndex(-1)
	{}

	FDungeonValidationIssue(const FString& InCategory, const FString& InDescription,
		const FIntVector& InLocation = FIntVector::ZeroValue, int32 InRoomIndex = -1)
		: Category(InCategory)
		, Description(InDescription)
		, Location(InLocation)
		, RoomIndex(InRoomIndex)
	{}
};

/** Aggregated validation result. */
struct DUNGEONCORE_API FDungeonValidationResult
{
	bool bPassed = true;
	TArray<FDungeonValidationIssue> Issues;

	/** Human-readable summary of all issues. */
	FString GetSummary() const;
};

/** Static validator for dungeon generation results. */
struct DUNGEONCORE_API FDungeonValidator
{
	/** Run all validations and return aggregated result. */
	static FDungeonValidationResult ValidateAll(const FDungeonResult& Result, const UDungeonConfiguration& Config);

	/** Entrance room and cell exist and are marked correctly. */
	static void ValidateEntrance(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues);

	/** Cell type counts match reported metrics. */
	static void ValidateMetrics(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues);

	/** All rooms/hallways/staircases within grid bounds. */
	static void ValidateCellBounds(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues);

	/** No two rooms share grid cells (AABB overlap check). */
	static void ValidateNoRoomOverlap(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues);

	/** Buffer distance maintained between rooms (XY only, matching RoomPlacement convention). */
	static void ValidateRoomBuffer(const FDungeonResult& Result, const UDungeonConfiguration& Config, TArray<FDungeonValidationIssue>& OutIssues);

	/** BFS on room adjacency graph from entrance reaches all rooms. */
	static void ValidateRoomConnectivity(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues);

	/** OccupiedCells above staircase body are Staircase/StaircaseHead, not Room/RoomWall. */
	static void ValidateStaircaseHeadroom(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues);

	/** 6-directional flood fill from entrance cell reaches all non-Empty cells. */
	static void ValidateReachability(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues);

private:
	/** Flood fill from Start through non-Empty cells in 6 directions. Populates VisitedIndices with flat cell indices. */
	static void FloodFill(const FDungeonGrid& Grid, const FIntVector& Start, TSet<int32>& VisitedIndices);
};
