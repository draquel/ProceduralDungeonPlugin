#include "DungeonVoxelStampPlan.h"
#include "DungeonTypes.h"
#include "DungeonBoundaryRules.h"

void FDungeonVoxelStampPlan::FaceSlabBox(
	const FVector& CellWorldMin,
	float CellWorldSize,
	int32 Face,
	float Thickness,
	bool bOutward,
	FVector& OutMin,
	FVector& OutMax)
{
	const FVector CellMax = CellWorldMin + FVector(CellWorldSize);
	const float Lateral = bOutward ? Thickness : 0.0f;

	OutMin = CellWorldMin - FVector(Lateral);
	OutMax = CellMax + FVector(Lateral);

	switch (Face)
	{
	case 0: // +X
		OutMin.X = bOutward ? CellMax.X : CellMax.X - Thickness;
		OutMax.X = bOutward ? CellMax.X + Thickness : CellMax.X;
		break;
	case 1: // -X
		OutMin.X = bOutward ? CellWorldMin.X - Thickness : CellWorldMin.X;
		OutMax.X = bOutward ? CellWorldMin.X : CellWorldMin.X + Thickness;
		break;
	case 2: // +Y
		OutMin.Y = bOutward ? CellMax.Y : CellMax.Y - Thickness;
		OutMax.Y = bOutward ? CellMax.Y + Thickness : CellMax.Y;
		break;
	case 3: // -Y
		OutMin.Y = bOutward ? CellWorldMin.Y - Thickness : CellWorldMin.Y;
		OutMax.Y = bOutward ? CellWorldMin.Y : CellWorldMin.Y + Thickness;
		break;
	case 4: // +Z (ceiling)
		OutMin.Z = bOutward ? CellMax.Z : CellMax.Z - Thickness;
		OutMax.Z = bOutward ? CellMax.Z + Thickness : CellMax.Z;
		break;
	case 5: // -Z (floor)
		OutMin.Z = bOutward ? CellWorldMin.Z - Thickness : CellWorldMin.Z;
		OutMax.Z = bOutward ? CellWorldMin.Z : CellWorldMin.Z + Thickness;
		break;
	default:
		OutMin = OutMax = FVector::ZeroVector;
		break;
	}
}

void FDungeonVoxelStampPlan::CollectOpenCellSamples(
	const FDungeonGrid& Grid,
	const FDungeonVoxelLattice& Lattice,
	const FVector& WorldOffset,
	float CellWorldSize,
	float CarveMargin,
	TSet<FIntVector>& OutSamples)
{
	for (int32 GZ = 0; GZ < Grid.GridSize.Z; ++GZ)
	{
		for (int32 GY = 0; GY < Grid.GridSize.Y; ++GY)
		{
			for (int32 GX = 0; GX < Grid.GridSize.X; ++GX)
			{
				if (!FDungeonBoundaryRules::IsOpenCell(Grid.GetCell(GX, GY, GZ).CellType))
				{
					continue;
				}

				const FVector CellWorldMin = WorldOffset + FVector(GX, GY, GZ) * CellWorldSize;
				const FVector BoxMin = CellWorldMin - FVector(CarveMargin);
				const FVector BoxMax = CellWorldMin + FVector(CellWorldSize + CarveMargin);

				FIntVector Min, Max;
				Lattice.RangeForBox(BoxMin, BoxMax, Min, Max);
				if (!FDungeonVoxelLattice::IsRangeValid(Min, Max))
				{
					continue;
				}

				for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
				{
					for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
					{
						for (int32 IX = Min.X; IX <= Max.X; ++IX)
						{
							OutSamples.Add(FIntVector(IX, IY, IZ));
						}
					}
				}
			}
		}
	}
}

void FDungeonVoxelStampPlan::CollectOuterSealSamples(
	const FDungeonVoxelLattice& Lattice,
	const FVector& CellWorldMin,
	float CellWorldSize,
	int32 Face,
	float Thickness,
	const TSet<FIntVector>& OpenCellSamples,
	TSet<FIntVector>& SealedSamples,
	TArray<FIntVector>& OutToWrite)
{
	FVector BoxMin, BoxMax;
	FaceSlabBox(CellWorldMin, CellWorldSize, Face, Thickness, /*bOutward=*/true, BoxMin, BoxMax);

	FIntVector Min, Max;
	Lattice.RangeForBox(BoxMin, BoxMax, Min, Max);
	if (!FDungeonVoxelLattice::IsRangeValid(Min, Max))
	{
		return;
	}

	for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
	{
		for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
		{
			for (int32 IX = Min.X; IX <= Max.X; ++IX)
			{
				const FIntVector Index(IX, IY, IZ);

				// Never plug a room, hallway or the cell this seal belongs to. Membership is by
				// lattice index, the same enumeration that carved the sample, so no world-space
				// rounding can attribute a sample to the wrong cell.
				if (OpenCellSamples.Contains(Index))
				{
					continue;
				}

				// Already sealed by an adjoining face or cell: same value, so skip the write.
				bool bAlreadySealed = false;
				SealedSamples.Add(Index, &bAlreadySealed);
				if (bAlreadySealed)
				{
					continue;
				}

				OutToWrite.Add(Index);
			}
		}
	}
}
