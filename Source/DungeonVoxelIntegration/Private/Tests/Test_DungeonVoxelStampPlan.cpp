// Test_DungeonVoxelStampPlan.cpp — Unit tests for FDungeonVoxelStampPlan (outer seal planning).
//
// Pins the defect measured in the demo on 2026-09-24 after the lattice went corner-sampled: some
// wall sections still read one full voxel (+63..+81 cm) of rock INSIDE the cell, varying with
// height along the wall. The outer seal classified each candidate sample by flooring its EDIT
// position (sample + half a voxel) into cell space, so the last sample layer before every
// +X / +Y / +Z plane counted as the neighbouring cell whenever the lattice phase was under half a
// voxel. Any slab that reached that layer — the WallThickness-wide lateral widening of a vertically
// adjacent cell's floor or ceiling seal, or a wall seal of a laterally adjacent open cell — then
// wrote it solid. The plan now excludes samples by lattice index, the same enumeration that carved
// them, so no world-space rounding can attribute a sample to the wrong cell.

#include "Misc/AutomationTest.h"
#include "DungeonVoxelStampPlan.h"
#include "DungeonVoxelLattice.h"
#include "DungeonTypes.h"
#include "DungeonBoundaryRules.h"

namespace
{
	/** The demo's numbers: 400 cm cells, 75 cm voxels, WallThickness 3 (225 cm seal). */
	constexpr float PlanCellSize = 400.0f;
	constexpr double PlanVoxelSize = 75.0;
	constexpr float PlanSealThickness = 3.0f * 75.0f;

	/** A dungeon origin with no particular relationship to the lattice. */
	const FVector PlanUnalignedOffset(-142216.203, 20979.24, -4664.986);

	/** Grid coordinates of the hand-built cells. */
	const FIntVector RoomA(1, 1, 1);      // Room 1
	const FIntVector HallwayB(1, 2, 1);   // Hallway 1: face-adjacent to A across A's +Y
	const FIntVector RoomD(1, 1, 2);      // Room 2: stacked directly on A (A's ceiling / D's floor)
	const FIntVector HallwayC(2, 1, 2);   // Hallway 2: diagonal above A, its floor seal dips into A

	static const FIntVector PlanDirections[6] = {
		{1, 0, 0}, {-1, 0, 0},
		{0, 1, 0}, {0, -1, 0},
		{0, 0, 1}, {0, 0, -1},
	};

	/** Two rooms and two hallways of different indices, every pair a different logical space. */
	void BuildGrid(FDungeonGrid& Grid)
	{
		Grid.Initialize(FIntVector(4, 4, 4));

		FDungeonCell& A = Grid.GetCell(RoomA);
		A.CellType = EDungeonCellType::Room;
		A.RoomIndex = 1;

		FDungeonCell& B = Grid.GetCell(HallwayB);
		B.CellType = EDungeonCellType::Hallway;
		B.HallwayIndex = 1;

		FDungeonCell& D = Grid.GetCell(RoomD);
		D.CellType = EDungeonCellType::Room;
		D.RoomIndex = 2;

		FDungeonCell& C = Grid.GetCell(HallwayC);
		C.CellType = EDungeonCellType::Hallway;
		C.HallwayIndex = 2;
	}

	bool FaceNeedsBoundary(const FDungeonGrid& Grid, const FIntVector& Coord, int32 Face)
	{
		const FDungeonCell& Cell = Grid.GetCell(Coord);
		const FIntVector N = Coord + PlanDirections[Face];
		return Face < 4
			? FDungeonBoundaryRules::NeedsWall(Grid, Cell, N.X, N.Y, N.Z)
			: FDungeonBoundaryRules::NeedsVerticalBoundary(Grid, Cell, N.X, N.Y, N.Z);
	}

	/** Independent oracle: which grid cell a world point lies in, by its SAMPLE position. */
	FIntVector CellOf(const FVector& WorldPos, const FVector& Offset)
	{
		const FVector Local = (WorldPos - Offset) / PlanCellSize;
		return FIntVector(FMath::FloorToInt32(Local.X), FMath::FloorToInt32(Local.Y), FMath::FloorToInt32(Local.Z));
	}

	bool IsOpenAt(const FDungeonGrid& Grid, const FIntVector& Coord)
	{
		return Grid.IsInBounds(Coord) && FDungeonBoundaryRules::IsOpenCell(Grid.GetCell(Coord).CellType);
	}
}

// ============================================================================
// The boundary rules give the hand-built grid the faces this test relies on
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStampPlanFixtureFaces, "Dungeon.VoxelStampPlan.FixtureHasExpectedBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStampPlanFixtureFaces::RunTest(const FString& Parameters)
{
	FDungeonGrid Grid;
	BuildGrid(Grid);

	// Room vs hallway: a wall on both sides of the shared face.
	TestTrue(TEXT("A +Y (toward hallway B) needs a wall"), FaceNeedsBoundary(Grid, RoomA, 2));
	TestTrue(TEXT("B -Y (toward room A) needs a wall"), FaceNeedsBoundary(Grid, HallwayB, 3));

	// Different rooms stacked: a floor / ceiling on both sides.
	TestTrue(TEXT("A +Z (toward room D) needs a ceiling"), FaceNeedsBoundary(Grid, RoomA, 4));
	TestTrue(TEXT("D -Z (toward room A) needs a floor"), FaceNeedsBoundary(Grid, RoomD, 5));

	// Hallway C over Empty: its floor seal is the slab that dips diagonally into A.
	TestTrue(TEXT("C -Z (over Empty) needs a floor"), FaceNeedsBoundary(Grid, HallwayC, 5));

	return true;
}

// ============================================================================
// No outer seal sample ever lands inside an open cell, and everything outside is sealed
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStampPlanSealNeverInsideOpenCell, "Dungeon.VoxelStampPlan.SealNeverWritesOpenCellSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStampPlanSealNeverInsideOpenCell::RunTest(const FString& Parameters)
{
	FDungeonGrid Grid;
	BuildGrid(Grid);

	const TArray<FIntVector> OpenCells = { RoomA, HallwayB, RoomD, HallwayC };

	// Sweep the lattice phase against the cell grid: the defect only showed for phases under
	// half a voxel on the +axes, and the seal gap only for phases over half a voxel on the -axes.
	for (int32 Step = 0; Step < 16; ++Step)
	{
		const double Phase = Step * (PlanVoxelSize / 16.0);
		const FVector Offset = PlanUnalignedOffset + FVector(Phase, -Phase * 0.5, Phase * 0.25);
		const FDungeonVoxelLattice Lattice(FVector(12345.0, 54321.0, 0.0), PlanVoxelSize);

		TSet<FIntVector> OpenSamples;
		FDungeonVoxelStampPlan::CollectOpenCellSamples(Grid, Lattice, Offset, PlanCellSize, OpenSamples);
		TestTrue(FString::Printf(TEXT("Phase %d: open cells yield samples"), Step), OpenSamples.Num() > 0);

		// Every collected sample really lies in an open cell, by the world-space oracle.
		for (const FIntVector& Index : OpenSamples)
		{
			const FIntVector Cell = CellOf(Lattice.SamplePosition(Index), Offset);
			if (!IsOpenAt(Grid, Cell))
			{
				AddError(FString::Printf(TEXT("Phase %d: open sample (%d,%d,%d) resolves to non-open cell (%d,%d,%d)"),
					Step, Index.X, Index.Y, Index.Z, Cell.X, Cell.Y, Cell.Z));
				return false;
			}
		}

		TSet<FIntVector> Sealed;
		int32 TotalWritten = 0;
		int32 WrittenOnSharedFace = -1;

		for (const FIntVector& Coord : OpenCells)
		{
			const FVector CellWorldMin = Offset + FVector(Coord) * PlanCellSize;

			for (int32 Face = 0; Face < FDungeonVoxelStampPlan::NumFaces; ++Face)
			{
				if (!FaceNeedsBoundary(Grid, Coord, Face))
				{
					continue;
				}

				TArray<FIntVector> ToWrite;
				FDungeonVoxelStampPlan::CollectOuterSealSamples(
					Lattice, CellWorldMin, PlanCellSize, Face, PlanSealThickness, OpenSamples, Sealed, ToWrite);
				TotalWritten += ToWrite.Num();

				if (Coord == RoomA && Face == 2)
				{
					// Only the slab directly in front of the face (A's own X / Z span): the
					// lateral rim of the widened slab lies outside B and is legitimately sealed.
					WrittenOnSharedFace = 0;
					const FVector CellWorldMax = CellWorldMin + FVector(PlanCellSize);
					for (const FIntVector& Index : ToWrite)
					{
						const FVector S = Lattice.SamplePosition(Index);
						if (S.X >= CellWorldMin.X && S.X < CellWorldMax.X && S.Z >= CellWorldMin.Z && S.Z < CellWorldMax.Z)
						{
							++WrittenOnSharedFace;
						}
					}
				}

				for (const FIntVector& Index : ToWrite)
				{
					// By index: the exact set Pass 1 carved.
					if (OpenSamples.Contains(Index))
					{
						AddError(FString::Printf(TEXT("Phase %d: cell (%d,%d,%d) face %d seals carved sample (%d,%d,%d)"),
							Step, Coord.X, Coord.Y, Coord.Z, Face, Index.X, Index.Y, Index.Z));
						return false;
					}

					// By world position: the sample's own point must not be in any open cell.
					const FVector Sample = Lattice.SamplePosition(Index);
					const FIntVector Cell = CellOf(Sample, Offset);
					if (IsOpenAt(Grid, Cell))
					{
						AddError(FString::Printf(TEXT("Phase %d: cell (%d,%d,%d) face %d seals sample (%d,%d,%d) at (%.1f,%.1f,%.1f) inside open cell (%d,%d,%d)"),
							Step, Coord.X, Coord.Y, Coord.Z, Face, Index.X, Index.Y, Index.Z,
							Sample.X, Sample.Y, Sample.Z, Cell.X, Cell.Y, Cell.Z));
						return false;
					}
				}
			}
		}

		TestTrue(FString::Printf(TEXT("Phase %d: the seal writes something"), Step), TotalWritten > 0);

		// The slab in front of the face shared with hallway B lies entirely inside B: nothing to
		// write there. The wall between two open cells is the tile module's, never voxels.
		TestEqual(FString::Printf(TEXT("Phase %d: A's +Y seal writes nothing in front of the face (inside hallway B)"), Step),
			WrittenOnSharedFace, 0);

		// Completeness: every slab sample that is NOT inside an open cell was sealed. This is the
		// mirror-image failure of the same rounding (the layer just outside a -X / -Y / -Z plane
		// was attributed to the cell and left unsealed).
		for (const FIntVector& Coord : OpenCells)
		{
			const FVector CellWorldMin = Offset + FVector(Coord) * PlanCellSize;
			for (int32 Face = 0; Face < FDungeonVoxelStampPlan::NumFaces; ++Face)
			{
				if (!FaceNeedsBoundary(Grid, Coord, Face))
				{
					continue;
				}

				FVector BoxMin, BoxMax;
				FDungeonVoxelStampPlan::FaceSlabBox(CellWorldMin, PlanCellSize, Face, PlanSealThickness, true, BoxMin, BoxMax);
				FIntVector Min, Max;
				Lattice.RangeForBox(BoxMin, BoxMax, Min, Max);

				for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
				{
					for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
					{
						for (int32 IX = Min.X; IX <= Max.X; ++IX)
						{
							const FIntVector Index(IX, IY, IZ);
							if (IsOpenAt(Grid, CellOf(Lattice.SamplePosition(Index), Offset)))
							{
								continue;
							}
							if (!Sealed.Contains(Index))
							{
								AddError(FString::Printf(TEXT("Phase %d: cell (%d,%d,%d) face %d left slab sample (%d,%d,%d) unsealed"),
									Step, Coord.X, Coord.Y, Coord.Z, Face, IX, IY, IZ));
								return false;
							}
						}
					}
				}
			}
		}
	}

	return true;
}
