// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DungeonBoundaryRules.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonTypes.h"
#include "DungeonVoxelConfig.h"

/**
 * The voxel dungeon suites' shared fixtures (code review CR2 row 17, DNG-35): the 8 x 8 x 2 three-room dungeon the frame
 * and lining tests stamp (rooted; the caller removes it from the root), its generation entered from above, a voxel
 * configuration, and the grid's open-cell and boundary rules as the tests read them.
 */
namespace DungeonVoxelTesting
{
	/** Three rooms of 2 to 3 cells in an 8 x 8 x 2 grid, a buffer of one, seed 4242, cells CellWorldSize wide. Rooted. */
	inline UDungeonConfiguration* MakeConfig(float CellWorldSize = 400.0f)
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = FIntVector(8, 8, 2);
		Config->CellWorldSize = CellWorldSize;
		Config->RoomCount = 3;
		Config->MinRoomSize = FIntVector(2, 2, 1);
		Config->MaxRoomSize = FIntVector(3, 3, 1);
		Config->RoomBuffer = 1;
		Config->bUseFixedSeed = true;
		Config->FixedSeed = 4242;
		return Config;
	}

	/** The dungeon generated from a configuration with seed 4242, its entrance room on the top floor entered from above. */
	inline FDungeonResult Generate(UDungeonConfiguration* Config)
	{
		FDungeonEntranceSpec Spec;
		Spec.Floor = EDungeonEntranceFloor::Top;
		Spec.Approach = EDungeonEntranceApproach::FromAbove;
		UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
		return Generator->GenerateWithEntrance(Config, 4242, Spec);
	}

	/** A voxel configuration with a wall thickness (rooted; the caller removes it from the root). */
	inline UDungeonVoxelConfig* MakeVoxelConfig(int32 WallThickness)
	{
		UDungeonVoxelConfig* VoxelConfig = NewObject<UDungeonVoxelConfig>();
		VoxelConfig->AddToRoot();
		VoxelConfig->WallThickness = WallThickness;
		return VoxelConfig;
	}

	/** An in-bounds open cell (room, hallway, staircase: FDungeonBoundaryRules::IsOpenCell). */
	inline bool IsOpen(const FDungeonResult& Result, const FIntVector& C)
	{
		return Result.Grid.IsInBounds(C) && FDungeonBoundaryRules::IsOpenCell(Result.Grid.GetCell(C).CellType);
	}

	/** Whether the cell's face (FDungeonGrid::FaceDirections' order) needs a wall (the four sides) or a floor / ceiling. */
	inline bool NeedsBoundary(const FDungeonResult& Result, const FIntVector& C, int32 Face)
	{
		const FIntVector N = C + FDungeonGrid::FaceDirections[Face];
		return Face < 4
			? FDungeonBoundaryRules::NeedsWall(Result.Grid, C, N.X, N.Y, N.Z)
			: FDungeonBoundaryRules::NeedsVerticalBoundary(Result.Grid, C, N.X, N.Y, N.Z);
	}

}

#endif // WITH_DEV_AUTOMATION_TESTS
