// Test_DungeonVoxelLattice.cpp — Unit tests for FDungeonVoxelLattice.
//
// These pin the defect the lattice exists to fix: dungeon stamping used to enumerate edit
// positions in CELL space (CellMin + (V + 0.5) * VoxelSize for V in [0, VoxelsPerCell)), which
// under-covers every cell whenever CellWorldSize is not an integer multiple of VoxelSize — the
// shipped demo runs 400 / 75 — and misaligns against the voxel grid for any dungeon origin that
// is not lattice-phased. The result was a solid rind left through every carved room.

#include "Misc/AutomationTest.h"
#include "DungeonVoxelLattice.h"

namespace
{
	/** The demo's numbers: 400 cm cells on a 75 cm voxel grid. */
	constexpr double DemoCellSize = 400.0;
	constexpr double DemoVoxelSize = 75.0;

	/** A dungeon origin with no particular relationship to the lattice. */
	const FVector UnalignedOffset(-142216.203, 20979.24, -4664.986);
}

// ============================================================================
// The old cell-space stepping genuinely under-covers a cell
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLatticeCellStepUnderCovers, "Dungeon.VoxelLattice.CellStepUnderCovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonLatticeCellStepUnderCovers::RunTest(const FString& Parameters)
{
	// RoundToInt(400 / 75) == 5, and 5 steps of 75 span 375 — 25 cm of every cell, on every
	// axis, was never carved.
	const int32 VoxelsPerCell = FMath::RoundToInt32(DemoCellSize / DemoVoxelSize);
	TestEqual(TEXT("VoxelsPerCell for 400/75"), VoxelsPerCell, 5);

	const double Covered = VoxelsPerCell * DemoVoxelSize;
	TestTrue(TEXT("Cell-space stepping does not span the cell"), Covered < DemoCellSize);

	return true;
}

// ============================================================================
// Every voxel in a cell's span is claimed, and claimed exactly once
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLatticeCellsPartition, "Dungeon.VoxelLattice.AbuttingCellsPartition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonLatticeCellsPartition::RunTest(const FString& Parameters)
{
	const FDungeonVoxelLattice Lattice(UnalignedOffset, DemoVoxelSize);

	// Walk a row of abutting cells and tally how many times each voxel index is claimed.
	constexpr int32 CellCount = 12;
	TMap<int32, int32> ClaimsPerIndex;

	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		const FVector CellMin = UnalignedOffset + FVector(Cell * DemoCellSize, 0.0, 0.0);
		const FVector CellMax = CellMin + FVector(DemoCellSize);

		FIntVector Min, Max;
		Lattice.RangeForBox(CellMin, CellMax, Min, Max);
		TestTrue(FString::Printf(TEXT("Cell %d yields a non-empty range"), Cell),
			FDungeonVoxelLattice::IsRangeValid(Min, Max));

		for (int32 IX = Min.X; IX <= Max.X; ++IX)
		{
			ClaimsPerIndex.FindOrAdd(IX)++;
		}
	}

	// No voxel claimed twice — cell-space stepping double-wrote wherever the phase drifted.
	for (const TPair<int32, int32>& Claim : ClaimsPerIndex)
	{
		TestEqual(FString::Printf(TEXT("Voxel index %d claimed once"), Claim.Key), Claim.Value, 1);
	}

	// No voxel skipped — the claimed indices must be one contiguous run covering the whole span.
	FIntVector SpanMin, SpanMax;
	Lattice.RangeForBox(
		UnalignedOffset,
		UnalignedOffset + FVector(CellCount * DemoCellSize, DemoCellSize, DemoCellSize),
		SpanMin, SpanMax);

	const int32 ExpectedCount = SpanMax.X - SpanMin.X + 1;
	TestEqual(TEXT("Union of cells covers the whole span with no gaps"), ClaimsPerIndex.Num(), ExpectedCount);

	return true;
}

// ============================================================================
// Reported centres really do land inside the box that produced them
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLatticeCentresInsideBox, "Dungeon.VoxelLattice.CentresInsideBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonLatticeCentresInsideBox::RunTest(const FString& Parameters)
{
	// Sweep sub-voxel phases so no single lucky alignment can carry the test.
	for (int32 Step = 0; Step < 16; ++Step)
	{
		const double Phase = Step * (DemoVoxelSize / 16.0);
		const FDungeonVoxelLattice Lattice(FVector(Phase, -Phase, Phase * 0.5), DemoVoxelSize);

		const FVector BoxMin(1234.5 + Phase, -987.25, 4321.0);
		const FVector BoxMax = BoxMin + FVector(DemoCellSize);

		FIntVector Min, Max;
		Lattice.RangeForBox(BoxMin, BoxMax, Min, Max);
		TestTrue(FString::Printf(TEXT("Phase %d yields a non-empty range"), Step),
			FDungeonVoxelLattice::IsRangeValid(Min, Max));

		// Every reported voxel centre is in [BoxMin, BoxMax)...
		for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
		{
			for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
			{
				for (int32 IX = Min.X; IX <= Max.X; ++IX)
				{
					const FVector C = Lattice.Center(FIntVector(IX, IY, IZ));
					const bool bInside =
						C.X >= BoxMin.X && C.X < BoxMax.X &&
						C.Y >= BoxMin.Y && C.Y < BoxMax.Y &&
						C.Z >= BoxMin.Z && C.Z < BoxMax.Z;
					TestTrue(FString::Printf(TEXT("Phase %d: centre (%d,%d,%d) inside box"), Step, IX, IY, IZ),
						bInside);
				}
			}
		}

		// ...and the voxels just outside the range are genuinely outside the box, so nothing
		// that belonged to this cell was dropped.
		const FVector JustBelow = Lattice.Center(FIntVector(Min.X - 1, Min.Y, Min.Z));
		const FVector JustAbove = Lattice.Center(FIntVector(Max.X + 1, Min.Y, Min.Z));
		TestTrue(FString::Printf(TEXT("Phase %d: voxel below range is outside box"), Step),
			JustBelow.X < BoxMin.X);
		TestTrue(FString::Printf(TEXT("Phase %d: voxel above range is outside box"), Step),
			JustAbove.X >= BoxMax.X);
	}

	return true;
}
