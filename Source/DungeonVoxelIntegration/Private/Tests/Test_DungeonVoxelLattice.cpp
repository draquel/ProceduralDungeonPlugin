// Test_DungeonVoxelLattice.cpp — Unit tests for FDungeonVoxelLattice.
//
// These pin two defects the lattice exists to fix:
//  1. Dungeon stamping used to enumerate edit positions in CELL space (CellMin + (V + 0.5) *
//     VoxelSize for V in [0, VoxelsPerCell)), which under-covers every cell whenever
//     CellWorldSize is not an integer multiple of VoxelSize (the demo runs 400 / 75) and
//     misaligns against the voxel grid for any dungeon origin that is not lattice-phased. The
//     result was a solid rind left through every carved room.
//  2. The first lattice assumed CENTRE sampling ((I + 0.5) * VoxelSize) while VoxelWorlds is
//     corner-sampled (index I is generated and meshed at I * VoxelSize). Every carve landed half
//     a voxel low, so rock intruded up to a full voxel past the +X/+Y/+Z cell planes.

#include "Misc/AutomationTest.h"
#include "DungeonVoxelLattice.h"

namespace
{
	/** The demo's numbers: 400 cm cells on a 75 cm voxel grid. */
	constexpr double DemoCellSize = 400.0;
	constexpr double DemoVoxelSize = 75.0;

	/** A dungeon origin with no particular relationship to the lattice. */
	const FVector UnalignedOffset(-142216.203, 20979.24, -4664.986);

	/** The engine's mapping (UVoxelEditManager::WorldToLocalPos): floor of the scaled offset. */
	int32 EngineIndexFor(double World, double Origin, double VoxelSize)
	{
		return FMath::FloorToInt32((World - Origin) / VoxelSize);
	}
}

// ============================================================================
// The old cell-space stepping genuinely under-covers a cell
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLatticeCellStepUnderCovers, "Dungeon.VoxelLattice.CellStepUnderCovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonLatticeCellStepUnderCovers::RunTest(const FString& Parameters)
{
	// RoundToInt(400 / 75) == 5, and 5 steps of 75 span 375: 25 cm of every cell, on every
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

	// No voxel claimed twice: cell-space stepping double-wrote wherever the phase drifted.
	for (const TPair<int32, int32>& Claim : ClaimsPerIndex)
	{
		TestEqual(FString::Printf(TEXT("Voxel index %d claimed once"), Claim.Key), Claim.Value, 1);
	}

	// No voxel skipped: the claimed indices must be one contiguous run covering the whole span.
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
// Reported sample positions really do land inside the box that produced them
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLatticeSamplesInsideBox, "Dungeon.VoxelLattice.SamplesInsideBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonLatticeSamplesInsideBox::RunTest(const FString& Parameters)
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

		// Every reported sample position is in [BoxMin, BoxMax)...
		for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
		{
			for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
			{
				for (int32 IX = Min.X; IX <= Max.X; ++IX)
				{
					const FVector S = Lattice.SamplePosition(FIntVector(IX, IY, IZ));
					const bool bInside =
						S.X >= BoxMin.X && S.X < BoxMax.X &&
						S.Y >= BoxMin.Y && S.Y < BoxMax.Y &&
						S.Z >= BoxMin.Z && S.Z < BoxMax.Z;
					TestTrue(FString::Printf(TEXT("Phase %d: sample (%d,%d,%d) inside box"), Step, IX, IY, IZ),
						bInside);
				}
			}
		}

		// ...and the samples just outside the range are genuinely outside the box, so nothing
		// that belonged to this cell was dropped.
		const FVector JustBelow = Lattice.SamplePosition(FIntVector(Min.X - 1, Min.Y, Min.Z));
		const FVector JustAbove = Lattice.SamplePosition(FIntVector(Max.X + 1, Min.Y, Min.Z));
		TestTrue(FString::Printf(TEXT("Phase %d: sample below range is outside box"), Step),
			JustBelow.X < BoxMin.X);
		TestTrue(FString::Printf(TEXT("Phase %d: sample above range is outside box"), Step),
			JustAbove.X >= BoxMax.X);
	}

	return true;
}

// ============================================================================
// The position handed to ApplyEdit resolves, under the engine's floor mapping, to the sample
// the lattice meant. This is the seam between the two conventions.
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLatticeEditPositionResolves, "Dungeon.VoxelLattice.EditPositionResolvesToIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonLatticeEditPositionResolves::RunTest(const FString& Parameters)
{
	for (int32 Step = 0; Step < 16; ++Step)
	{
		const double Phase = Step * (DemoVoxelSize / 16.0);
		const FDungeonVoxelLattice Lattice(FVector(Phase, -Phase, Phase * 0.5), DemoVoxelSize);

		for (int32 I = -2000; I <= 2000; I += 37)
		{
			const FIntVector Index(I, -I, I / 3);
			const FVector Edit = Lattice.EditPosition(Index);
			TestEqual(FString::Printf(TEXT("Phase %d: X of index %d resolves"), Step, I),
				EngineIndexFor(Edit.X, Lattice.Origin.X, DemoVoxelSize), Index.X);
			TestEqual(FString::Printf(TEXT("Phase %d: Y of index %d resolves"), Step, I),
				EngineIndexFor(Edit.Y, Lattice.Origin.Y, DemoVoxelSize), Index.Y);
			TestEqual(FString::Printf(TEXT("Phase %d: Z of index %d resolves"), Step, I),
				EngineIndexFor(Edit.Z, Lattice.Origin.Z, DemoVoxelSize), Index.Z);

			// And the sample the engine will generate / mesh for that index is where we think.
			const FVector Sample = Lattice.SamplePosition(Index);
			TestEqual(FString::Printf(TEXT("Phase %d: sample X of index %d"), Step, I),
				Sample.X, Lattice.Origin.X + I * DemoVoxelSize, 1e-6);
		}
	}
	return true;
}

// ============================================================================
// The carved void's surface stays within half a voxel of every cell plane. This is the
// contract tile modules are authored against (faces inset >= VoxelSize / 2 are never buried).
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonLatticeSurfaceWithinHalfVoxel, "Dungeon.VoxelLattice.SurfaceWithinHalfVoxelOfPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonLatticeSurfaceWithinHalfVoxel::RunTest(const FString& Parameters)
{
	const double HalfVoxel = DemoVoxelSize * 0.5;
	for (int32 Step = 0; Step < 32; ++Step)
	{
		const double Phase = Step * (DemoVoxelSize / 32.0);
		const FDungeonVoxelLattice Lattice(UnalignedOffset, DemoVoxelSize);

		const FVector BoxMin = UnalignedOffset + FVector(7 * DemoCellSize + Phase, 3 * DemoCellSize, 0.0);
		const FVector BoxMax = BoxMin + FVector(DemoCellSize);

		FIntVector Min, Max;
		Lattice.RangeForBox(BoxMin, BoxMax, Min, Max);
		TestTrue(TEXT("non-empty"), FDungeonVoxelLattice::IsRangeValid(Min, Max));

		// A binary carve puts the isosurface midway between the last carved sample and the first
		// solid one, on both sides of the cell.
		const double LastCarvedX = Lattice.SamplePosition(Max).X;
		const double FirstSolidAboveX = Lattice.SamplePosition(FIntVector(Max.X + 1, Max.Y, Max.Z)).X;
		const double SurfaceMaxX = 0.5 * (LastCarvedX + FirstSolidAboveX);
		TestTrue(FString::Printf(TEXT("Phase %d: +X surface within half a voxel of the plane (%.1f vs %.1f)"), Step, SurfaceMaxX, BoxMax.X),
			FMath::Abs(SurfaceMaxX - BoxMax.X) <= HalfVoxel + 1e-6);

		const double FirstCarvedX = Lattice.SamplePosition(Min).X;
		const double LastSolidBelowX = Lattice.SamplePosition(FIntVector(Min.X - 1, Min.Y, Min.Z)).X;
		const double SurfaceMinX = 0.5 * (FirstCarvedX + LastSolidBelowX);
		TestTrue(FString::Printf(TEXT("Phase %d: -X surface within half a voxel of the plane (%.1f vs %.1f)"), Step, SurfaceMinX, BoxMin.X),
			FMath::Abs(SurfaceMinX - BoxMin.X) <= HalfVoxel + 1e-6);
	}
	return true;
}
