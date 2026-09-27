// DungeonCoverage.cpp — light-tightness report: needed boundaries vs mapped pieces (E5).
#include "DungeonCoverage.h"
#include "DungeonTypes.h"
#include "DungeonTileMapper.h"
#include "DungeonBoundaryRules.h"

FString FDungeonCoverageReport::Describe() const
{
	FString S = FString::Printf(TEXT("Coverage: walls %d/%d covered (%d doubled), floors %d/%d, ceilings %d/%d — %s"),
		WallFacesNeeded - WallFacesUncovered, WallFacesNeeded, WallFacesDoubled,
		FloorsNeeded - FloorsUncovered, FloorsNeeded,
		CeilingsNeeded - CeilingsUncovered, CeilingsNeeded,
		IsLightTight() ? TEXT("light-tight") : TEXT("NOT light-tight"));
	for (const FString& P : Problems)
	{
		S += TEXT("\n  ") + P;
	}
	return S;
}

FDungeonCoverageReport FDungeonCoverage::Analyse(const FDungeonResult& Result, const FDungeonTileMapResult& TileMap,
	const FVector& WorldOffset, bool bOpenEntranceCeiling)
{
	FDungeonCoverageReport Report;
	const float CS = Result.CellWorldSize;
	if (CS <= 0.0f)
	{
		return Report;
	}
	const FIntVector& GridSize = Result.Grid.GridSize;

	// Instances bucketed on the doubled cell lattice: a wall piece sits on a face centre (its
	// doubled X/Y are even along the normal, odd along the wall); a floor / ceiling piece at a
	// cell centre (odd, odd) on the boundary plane below / above. Rounding absorbs the profile
	// shifts and slab lifts (all < CS/4).
	auto WallKey = [&](const FVector& World) -> FIntVector
	{
		// Walls anchor at the cell's MID-height: their Z bucket is the cell itself.
		const FVector L = World - WorldOffset;
		return FIntVector(FMath::RoundToInt(2.0f * L.X / CS), FMath::RoundToInt(2.0f * L.Y / CS), FMath::FloorToInt(L.Z / CS));
	};
	auto SlabKey = [&](const FVector& World) -> FIntVector
	{
		// Slabs sit on a boundary plane (plus a lift < CS/4): their Z bucket is that plane's index.
		const FVector L = World - WorldOffset;
		return FIntVector(FMath::RoundToInt(2.0f * L.X / CS), FMath::RoundToInt(2.0f * L.Y / CS), FMath::FloorToInt((L.Z + CS * 0.5f) / CS));
	};
	TMap<FIntVector, int32> Walls, Slabs;
	static const EDungeonTileType WallFamily[] = {
		EDungeonTileType::WallSegment, EDungeonTileType::WallPartition,
		EDungeonTileType::DoorFrame, EDungeonTileType::EntranceFrame };
	for (EDungeonTileType T : WallFamily)
	{
		for (const FTransform& Xf : TileMap.Transforms[static_cast<int32>(T)])
		{
			Walls.FindOrAdd(WallKey(Xf.GetLocation()))++;
		}
	}
	static const EDungeonTileType SlabFamily[] = {
		EDungeonTileType::RoomFloor, EDungeonTileType::HallwayFloor, EDungeonTileType::RoomCeiling, EDungeonTileType::HallwayCeiling,
		EDungeonTileType::HallwayFloorStraight, EDungeonTileType::HallwayFloorCorner, EDungeonTileType::HallwayFloorTJunction,
		EDungeonTileType::HallwayFloorCrossroad, EDungeonTileType::HallwayFloorEndCap,
		EDungeonTileType::HallwayCeilingStraight, EDungeonTileType::HallwayCeilingCorner, EDungeonTileType::HallwayCeilingTJunction,
		EDungeonTileType::HallwayCeilingCrossroad, EDungeonTileType::HallwayCeilingEndCap };
	for (EDungeonTileType T : SlabFamily)
	{
		for (const FTransform& Xf : TileMap.Transforms[static_cast<int32>(T)])
		{
			Slabs.FindOrAdd(SlabKey(Xf.GetLocation()))++;
		}
	}

	// The entrance opening the mapper leaves unbuilt (mirrors FDungeonTileMapper::MapToTiles).
	const FDungeonEntranceApproachInfo& Approach = Result.EntranceApproach;
	const bool bApproachKnown = Approach.Approach != EDungeonEntranceApproach::None && Approach.bSatisfied;
	const EDungeonEntranceApproach OpeningKind = bApproachKnown ? Approach.Approach : EDungeonEntranceApproach::FromAbove;
	const FIntVector OpeningCell = bOpenEntranceCeiling ? Result.GetEntranceOpeningCell() : FIntVector(-1, -1, -1);
	int32 OpeningFaceDX = 0, OpeningFaceDY = 0;
	if (OpeningKind == EDungeonEntranceApproach::FromSide)
	{
		switch (Approach.Face)
		{
		case EDungeonGridFace::MinX: OpeningFaceDX = -1; break;
		case EDungeonGridFace::MaxX: OpeningFaceDX = +1; break;
		case EDungeonGridFace::MinY: OpeningFaceDY = -1; break;
		case EDungeonGridFace::MaxY: OpeningFaceDY = +1; break;
		default: break;
		}
	}

	static const int32 DX[4] = { 1, -1, 0, 0 };
	static const int32 DY[4] = { 0, 0, 1, -1 };
	TSet<FIntVector> WallFacesSeen;
	auto Problem = [&](const FString& Text, const FIntVector& K, float ZCells)
	{
		Report.Problems.Add(Text);
		Report.ProblemLocations.Add(WorldOffset + FVector(K.X * 0.5f * CS, K.Y * 0.5f * CS, ZCells * CS));
	};

	for (int32 Z = 0; Z < GridSize.Z; ++Z)
	for (int32 Y = 0; Y < GridSize.Y; ++Y)
	for (int32 X = 0; X < GridSize.X; ++X)
	{
		const FIntVector Cell(X, Y, Z);
		const EDungeonCellType Type = Result.Grid.GetCell(Cell).CellType;
		if (!FDungeonBoundaryRules::IsOpenCell(Type))
		{
			continue;
		}
		const bool bOpening = bOpenEntranceCeiling && Cell == OpeningCell;
		// Stair cells are dressed by the ramp; their side walls are their neighbours' business and
		// their floor / ceiling are the ramp's.
		const bool bStair = Type == EDungeonCellType::Staircase || Type == EDungeonCellType::StaircaseHead;

		if (!bStair)
		{
			for (int32 D = 0; D < 4; ++D)
			{
				const int32 NX = X + DX[D], NY = Y + DY[D];
				if (!FDungeonBoundaryRules::NeedsWall(Result.Grid, Cell, NX, NY, Z))
				{
					continue;
				}
				if (bOpening && OpeningKind == EDungeonEntranceApproach::FromSide && DX[D] == OpeningFaceDX && DY[D] == OpeningFaceDY)
				{
					continue; // the side tunnel enters here
				}
				const FIntVector K(2 * X + 1 + DX[D], 2 * Y + 1 + DY[D], Z);
				if (WallFacesSeen.Contains(K))
				{
					continue; // shared face: counted once
				}
				WallFacesSeen.Add(K);
				++Report.WallFacesNeeded;
				const int32 Count = Walls.FindRef(K);
				if (Count == 0)
				{
					++Report.WallFacesUncovered;
					Problem(FString::Printf(TEXT("wall %s face (%+d,%+d): no piece"), *Cell.ToString(), DX[D], DY[D]), K, Z + 0.5f);
				}
				else if (Count > 1)
				{
					++Report.WallFacesDoubled;
					Problem(FString::Printf(TEXT("wall %s face (%+d,%+d): %d pieces"), *Cell.ToString(), DX[D], DY[D], Count), K, Z + 0.5f);
				}
			}

			// Floor under this cell.
			const bool bOpenFloor = bOpening && OpeningKind == EDungeonEntranceApproach::FromBelow;
			if (!bOpenFloor && FDungeonBoundaryRules::NeedsVerticalBoundary(Result.Grid, Cell, X, Y, Z - 1))
			{
				++Report.FloorsNeeded;
				const FIntVector K(2 * X + 1, 2 * Y + 1, Z);
				if (Slabs.FindRef(K) == 0)
				{
					++Report.FloorsUncovered;
					Problem(FString::Printf(TEXT("floor %s: no piece"), *Cell.ToString()), K, static_cast<float>(Z));
				}
			}
			// Ceiling over this cell.
			const bool bOpenLid = bOpening && OpeningKind == EDungeonEntranceApproach::FromAbove;
			if (!bOpenLid && FDungeonBoundaryRules::NeedsVerticalBoundary(Result.Grid, Cell, X, Y, Z + 1))
			{
				++Report.CeilingsNeeded;
				const FIntVector K(2 * X + 1, 2 * Y + 1, Z + 1);
				if (Slabs.FindRef(K) == 0)
				{
					++Report.CeilingsUncovered;
					Problem(FString::Printf(TEXT("ceiling %s: no piece"), *Cell.ToString()), K, static_cast<float>(Z + 1));
				}
			}
		}
	}
	return Report;
}
