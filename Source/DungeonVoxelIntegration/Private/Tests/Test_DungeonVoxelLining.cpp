// Test_DungeonVoxelLining.cpp — the voxel-lined stamp keeps every room's interior, and its floor holds (review finding G24).
//
// A voxel-carved dungeon (CarveUnderground) lined every closed face from INSIDE its cell, WallThickness voxels deep. The
// demo's voxel config has WallThickness 3 at VoxelSize 75 on 400 cm cells: the floor slab [0, 225) and the ceiling slab
// [175, 400) of a one-storey cell covered all of it, so every room and hallway was stamped solid and nothing could stand in
// them (the boss fell through its room's floor at spawn, vox.TPRoom dropped the player into the rock;
// Plugins/VoxelWorlds/Documentation/WORLD_MODES_REVIEW_PLAN.md §19.8). The jacket now lines each closed face with
// UDungeonVoxelConfig::InnerLiningLayers inside the cell and wraps it with the rest from outside.
//
// - Dungeon.Stamp.VoxelLining: with the demo's numbers every sample of every room and hallway cell is air but the inner
//   lining of its closed faces, which is solid; the jacket's other layers are solid outside every face toward rock; with
//   WallThickness 1 nothing is written outside the grid box.
// - Dungeon.Stamp.CarvedFloorCollision: the same dungeon stamped under a streamed plane with collision, in Dual Contouring
//   and in cubic meshing: a trace down from inside every room and hallway cell hits its floor's collision an inner lining
//   above the cell's bottom plane.

#include "Misc/AutomationTest.h"
#include "DungeonVoxelTestFixtures.h"
#include "Async/TaskGraphInterfaces.h"
#include "DungeonBoundaryRules.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonTypes.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelLattice.h"
#include "DungeonVoxelStampPlan.h"
#include "DungeonVoxelStamper.h"
#include "Engine/World.h"
#include "VoxelCollisionManager.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWorldConfiguration.h"
#include "Testing/VoxelTestChunkWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonLiningTest
{
	using namespace DungeonVoxelTesting;

	/** The demo's numbers (DA_DungeonVoxelConfig_POIDemo on VoxelDemo): 400 cm cells, 75 cm voxels, a 3-layer jacket. */
	constexpr float CellSize = 400.0f;
	constexpr float DemoVoxelSize = 75.0f;
	constexpr int32 DemoWallThickness = 3;

	/** A small plane with the demo's voxel size. */
	void ConfigureDemoPlane(VoxelTesting::FTestChunkWorld& W)
	{
		W.ConfigureSmallPlane();
		W.Config->VoxelSize = DemoVoxelSize;
	}

	/**
	 * A grid origin with no particular phase against the lattice, its top 1,500 under the datum (terrain within +-800). Every
	 * cell centre lies over 180 uu from a chunk face (chunks of 2,400 here): a chunk whose collision cooks before its +X / +Y
	 * neighbour has loaded keeps a voxel-wide strip without collision along that face until something re-cooks it (seen on
	 * plain terrain too, §19.8), and the probes here are about the stamp, not that.
	 */
	FVector GridOffset(const FDungeonResult& Result)
	{
		return FVector(-1613.37, 786.63, -1500.0 - Result.GridSize.Z * CellSize);
	}


	/** Rooms and hallways: their interiors hold nothing but air (stairs hold steps, doors and the entrance frames). */
	bool IsPlainSpace(const FDungeonResult& Result, const FIntVector& C)
	{
		const EDungeonCellType Type = Result.Grid.GetCell(C).CellType;
		return Type == EDungeonCellType::Room || Type == EDungeonCellType::Hallway;
	}

	/** The edit at a lattice index, or null. */
	const FVoxelEdit* EditAtIndex(const UVoxelEditManager& Edits, int32 ChunkSize, const FIntVector& Index)
	{
		const FIntVector Chunk(FMath::FloorToInt(static_cast<float>(Index.X) / ChunkSize),
			FMath::FloorToInt(static_cast<float>(Index.Y) / ChunkSize), FMath::FloorToInt(static_cast<float>(Index.Z) / ChunkSize));
		const FChunkEditLayer* Layer = Edits.GetEditLayer(Chunk);
		return Layer ? Layer->GetEdit(Index - Chunk * ChunkSize) : nullptr;
	}

	bool InBox(const FVector& P, const FVector& Min, const FVector& Max)
	{
		return P.X >= Min.X && P.Y >= Min.Y && P.Z >= Min.Z && P.X < Max.X && P.Y < Max.Y && P.Z < Max.Z;
	}

	/** The slab just outside a face over the face itself (no lateral widening), Depth deep. */
	void OuterFootprint(const FVector& CellMin, int32 Face, float Depth, FVector& OutMin, FVector& OutMax)
	{
		OutMin = CellMin;
		OutMax = CellMin + FVector(CellSize);
		const int32 Axis = Face / 2;
		if (Face % 2 == 0)
		{
			OutMin[Axis] = CellMin[Axis] + CellSize;
			OutMax[Axis] = OutMin[Axis] + Depth;
		}
		else
		{
			OutMax[Axis] = CellMin[Axis];
			OutMin[Axis] = OutMax[Axis] - Depth;
		}
	}

	/** Tick the manager until the collision at every point has a body (the stamp precedes every cook), or the deadline passes. */
	bool WaitForCollision(VoxelTesting::FTestChunkWorld& W, const TArray<FVector>& Points, double TimeoutSeconds)
	{
		UVoxelCollisionManager* Collision = W.Manager->GetCollisionManager();
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		auto AllReady = [&]()
		{
			for (const FVector& P : Points)
			{
				for (const float Dz : { -60.0f, 60.0f })
				{
					if (!Collision->HasCollision(W.Manager->WorldToChunkCoord(P + FVector(0.0f, 0.0f, Dz))))
					{
						return false;
					}
				}
			}
			return true;
		};
		while (Collision && !AllReady() && FPlatformTime::Seconds() < Deadline)
		{
			W.Manager->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::Sleep(0.005f);
		}
		return Collision && AllReady();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStampVoxelLiningTest, "Dungeon.Stamp.VoxelLining",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStampVoxelLiningTest::RunTest(const FString& Parameters)
{
	using namespace DungeonLiningTest;
	UDungeonConfiguration* Config = MakeConfig(CellSize);
	const FDungeonResult Result = Generate(Config);
	if (!TestTrue(TEXT("The dungeon generates (non-vacuous)"), Result.Rooms.Num() > 0))
	{
		Config->RemoveFromRoot();
		return false;
	}
	const FVector Offset = GridOffset(Result);

	for (const int32 WallThickness : { DemoWallThickness, 1 })
	{
		UDungeonVoxelConfig* VoxelConfig = MakeVoxelConfig(WallThickness);
		VoxelTesting::FTestChunkWorld W;
		W.Create(TEXT("DungeonVoxelLining"));
		ConfigureDemoPlane(W);
		W.StartManager();
		UDungeonVoxelStamper* Stamper = NewObject<UDungeonVoxelStamper>();
		const FDungeonStampResult Stamp = Stamper->StampDungeonInFrame(Result, W.Manager, Offset, EDungeonStampMode::CarveUnderground, VoxelConfig, FTransform::Identity);
		const UVoxelEditManager* Edits = W.Manager->GetEditManager();
		if (!TestTrue(FString::Printf(TEXT("WallThickness %d: the stamp succeeds"), WallThickness), Stamp.bSuccess && Edits != nullptr))
		{
			W.Stop();
			VoxelConfig->RemoveFromRoot();
			continue;
		}
		const float Voxel = W.Config->VoxelSize;
		const int32 ChunkSize = W.Config->ChunkSize;
		const float Lining = VoxelConfig->GetInnerLiningThickness(Voxel);
		const float Jacket = VoxelConfig->GetOuterJacketLayers() * Voxel;
		const FDungeonVoxelLattice Lattice(W.Config->WorldOrigin, Voxel);

		int32 Cells = 0, CellsWithoutInterior = 0;
		int32 InteriorSamples = 0, InteriorNotAir = 0, LiningSamples = 0, LiningNotSolid = 0, JacketSamples = 0, JacketNotSolid = 0;
		for (int32 Z = 0; Z < Result.GridSize.Z; ++Z)
		{
			for (int32 Y = 0; Y < Result.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < Result.GridSize.X; ++X)
				{
					const FIntVector C(X, Y, Z);
					if (!IsOpen(Result, C) || !IsPlainSpace(Result, C))
					{
						continue;
					}
					++Cells;
					const FVector CellMin = Offset + FVector(C) * CellSize;
					bool bClosed[6];
					for (int32 Face = 0; Face < 6; ++Face)
					{
						bClosed[Face] = NeedsBoundary(Result, C, Face);
					}

					// Inside the cell: the inner lining of a closed face is solid, everything else is air.
					int32 CellInterior = 0;
					Lattice.ForEachSampleInBox(CellMin, CellMin + FVector(CellSize), [&](const FIntVector& Index)
					{
						const FVector P = Lattice.SamplePosition(Index);
						bool bInLining = false;
						for (int32 Face = 0; Face < 6 && !bInLining; ++Face)
						{
							FVector Min, Max;
							FDungeonVoxelStampPlan::FaceSlabBox(CellMin, CellSize, Face, Lining, /*bOutward=*/false, Min, Max);
							bInLining = bClosed[Face] && InBox(P, Min, Max);
						}
						const FVoxelEdit* Edit = EditAtIndex(*Edits, ChunkSize, Index);
						if (bInLining)
						{
							++LiningSamples;
							LiningNotSolid += (Edit && Edit->NewData.Density >= VOXEL_SURFACE_THRESHOLD) ? 0 : 1;
						}
						else
						{
							++InteriorSamples;
							++CellInterior;
							InteriorNotAir += (Edit && Edit->NewData.Density < VOXEL_SURFACE_THRESHOLD) ? 0 : 1;
						}
					});
					CellsWithoutInterior += CellInterior == 0 ? 1 : 0;

					// Outside every face toward rock (or out of the grid): the rest of the jacket, solid.
					for (int32 Face = 0; Face < 6; ++Face)
					{
						if (!bClosed[Face] || IsOpen(Result, C + FDungeonGrid::FaceDirections[Face]) || Jacket <= 0.0f)
						{
							continue;
						}
						FVector Min, Max;
						OuterFootprint(CellMin, Face, Jacket, Min, Max);
						Lattice.ForEachSampleInBox(Min, Max, [&](const FIntVector& Index)
						{
							++JacketSamples;
							const FVoxelEdit* Edit = EditAtIndex(*Edits, ChunkSize, Index);
							JacketNotSolid += (Edit && Edit->NewData.Density >= VOXEL_SURFACE_THRESHOLD) ? 0 : 1;
						});
					}
				}
			}
		}

		// No edit outside the grid box and its jacket (a voxel of rounding).
		const FBox GridBox(Offset - FVector(Jacket + Voxel), Offset + FVector(Result.GridSize) * CellSize + FVector(Jacket + Voxel));
		int32 Outside = 0, EditCount = 0;
		TArray<FIntVector> Chunks;
		Edits->GetEditedChunkCoords(Chunks);
		for (const FIntVector& Chunk : Chunks)
		{
			if (const FChunkEditLayer* Layer = Edits->GetEditLayer(Chunk))
			{
				for (const TPair<int32, FVoxelEdit>& Pair : Layer->Edits)
				{
					++EditCount;
					const FVector P = W.Config->WorldOrigin + FVector(Chunk) * (ChunkSize * Voxel) + FVector(Pair.Value.LocalPosition) * Voxel;
					Outside += GridBox.IsInside(P) ? 0 : 1;
				}
			}
		}

		AddInfo(FString::Printf(TEXT("WallThickness %d (lining %.0f, jacket %.0f): %d edits, %d room and hallway cells (%d with no interior), %d interior samples (%d not air), %d lining samples (%d not solid), %d jacket samples (%d not solid), %d edits outside the jacket"),
			WallThickness, Lining, Jacket, EditCount, Cells, CellsWithoutInterior, InteriorSamples, InteriorNotAir, LiningSamples, LiningNotSolid,
			JacketSamples, JacketNotSolid, Outside));
		const FString Prefix = FString::Printf(TEXT("WallThickness %d"), WallThickness);
		TestTrue(Prefix + TEXT(": cells, linings and interiors to check (non-vacuous)"), Cells > 0 && LiningSamples > 0 && InteriorSamples > Cells);
		TestEqual(Prefix + TEXT(": every room and hallway cell keeps an interior"), CellsWithoutInterior, 0);
		TestEqual(Prefix + TEXT(": the interiors are air"), InteriorNotAir, 0);
		TestEqual(Prefix + TEXT(": the inner lining of every closed face is solid"), LiningNotSolid, 0);
		TestEqual(Prefix + TEXT(": the jacket outside every face toward rock is solid"), JacketNotSolid, 0);
		TestEqual(Prefix + TEXT(": nothing is written past the jacket"), Outside, 0);
		if (WallThickness > 1)
		{
			TestTrue(Prefix + TEXT(": the jacket has layers outside the cells (non-vacuous)"), JacketSamples > 0);
		}
		else
		{
			TestEqual(Prefix + TEXT(": a one-layer jacket is all lining"), JacketSamples, 0);
		}
		W.Stop();
		VoxelConfig->RemoveFromRoot();
	}

	Config->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStampCarvedFloorCollisionTest, "Dungeon.Stamp.CarvedFloorCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStampCarvedFloorCollisionTest::RunTest(const FString& Parameters)
{
	using namespace DungeonLiningTest;
	UDungeonConfiguration* Config = MakeConfig(CellSize);
	UDungeonVoxelConfig* VoxelConfig = MakeVoxelConfig(DemoWallThickness);
	const FDungeonResult Result = Generate(Config);
	if (!TestTrue(TEXT("The dungeon generates (non-vacuous)"), Result.Rooms.Num() > 0))
	{
		Config->RemoveFromRoot();
		VoxelConfig->RemoveFromRoot();
		return false;
	}
	const FVector Offset = GridOffset(Result);

	// How far the floor's collision may lie from the expected floor, in voxels: Dual Contouring's surface lies midway
	// between the lining's top sample and the air over it (the lattice's phase: half a voxel either way); a cubic block
	// spans its sample's voxel upward (up to a voxel above). Collision at LOD 0, as every world in the project cooks it (a
	// stride-2 collision mesh cracks along chunk faces, §19.8).
	struct FCase
	{
		EMeshingMode Mode;
		int32 CollisionLOD;
		float ToleranceVoxels;
		const TCHAR* Name;
	};
	const FCase Cases[] = {
		{ EMeshingMode::DualContouring, 0, 0.75f, TEXT("Dual Contouring") },
		{ EMeshingMode::Cubic, 0, 1.05f, TEXT("Cubic") },
	};
	for (const FCase& Case : Cases)
	{
		VoxelTesting::FTestChunkWorld W;
		W.Create(TEXT("DungeonCarvedFloor"));
		ConfigureDemoPlane(W);
		W.Config->MeshingMode = Case.Mode;
		W.Config->bGenerateCollision = true;
		W.Config->CollisionLODLevel = Case.CollisionLOD;
		// The collision radius is half the view distance: past the chunk under the viewer's centre (sqrt(3) * 1,200 here),
		// or the decision sweep drops that chunk's collision as fast as the path coverage under the focus re-cooks it.
		W.Config->ViewDistance = 6000.0f;
		W.Config->LODBands = { FLODBand(0.0f, W.Config->ViewDistance, 0, 1) };
		W.StartManager();
		UDungeonVoxelStamper* Stamper = NewObject<UDungeonVoxelStamper>();
		const FDungeonStampResult Stamp = Stamper->StampDungeonInFrame(Result, W.Manager, Offset, EDungeonStampMode::CarveUnderground, VoxelConfig, FTransform::Identity);
		TestTrue(FString::Printf(TEXT("%s: the stamp succeeds"), Case.Name), Stamp.bSuccess);

		const float Voxel = W.Config->VoxelSize;
		const float Lining = VoxelConfig->GetInnerLiningThickness(Voxel);
		// Every room and hallway cell with a floor: the cell centre in XY at the expected floor (the inner lining over the
		// cell's bottom plane).
		TArray<FVector> Floors;
		for (int32 Z = 0; Z < Result.GridSize.Z; ++Z)
		{
			for (int32 Y = 0; Y < Result.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < Result.GridSize.X; ++X)
				{
					const FIntVector C(X, Y, Z);
					if (IsOpen(Result, C) && IsPlainSpace(Result, C) && NeedsBoundary(Result, C, 5))
					{
						const FVector CellMin = Offset + FVector(C) * CellSize;
						Floors.Add(FVector(CellMin.X + CellSize * 0.5f, CellMin.Y + CellSize * 0.5f, CellMin.Z + Lining));
					}
				}
			}
		}
		W.SetViewer(Offset + FVector(Result.GridSize) * CellSize * 0.5f);
		const bool bSettled = W.Settle();
		const bool bCooked = bSettled && WaitForCollision(W, Floors, 60.0);
		TestTrue(FString::Printf(TEXT("%s: streaming settles and the collision under every floor cooks"), Case.Name), bSettled && bCooked);

		// Down from mid-room: the first world-static hit is the floor, an inner lining over the cell's bottom plane.
		const float Tolerance = Case.ToleranceVoxels * Voxel;
		int32 Hits = 0, Misses = 0, OffFloor = 0;
		FString MissedAt;
		double WorstOff = 0.0;
		FCollisionQueryParams Params(FName(TEXT("DungeonCarvedFloor")), /*bTraceComplex=*/true);
		for (const FVector& Floor : Floors)
		{
			FHitResult Hit;
			const FVector Start = Floor + FVector(0.0f, 0.0f, CellSize * 0.5f - Lining);
			const bool bHit = W.World->LineTraceSingleByObjectType(Hit, Start, Floor - FVector(0.0f, 0.0f, CellSize),
				FCollisionObjectQueryParams(ECC_WorldStatic), Params);
			if (!bHit)
			{
				++Misses;
				MissedAt += FString::Printf(TEXT(" (%.0f, %.0f, %.0f)"), Floor.X, Floor.Y, Floor.Z);
				continue;
			}
			++Hits;
			const double Off = Hit.ImpactPoint.Z - Floor.Z;
			WorstOff = FMath::Max(WorstOff, FMath::Abs(Off));
			OffFloor += (FMath::Abs(Off) <= Tolerance && Hit.ImpactNormal.Z > 0.7) ? 0 : 1;
		}
		AddInfo(FString::Printf(TEXT("%s: %d room and hallway floors, %d hits (%d misses, %d off the floor), worst %.1f uu from the expected floor (tolerance %.0f)%s%s"),
			Case.Name, Floors.Num(), Hits, Misses, OffFloor, WorstOff, Tolerance, MissedAt.IsEmpty() ? TEXT("") : TEXT("; missed at"), *MissedAt));
		TestTrue(FString::Printf(TEXT("%s: floors to probe (non-vacuous)"), Case.Name), Floors.Num() > 0);
		TestEqual(FString::Printf(TEXT("%s: a floor under every room and hallway"), Case.Name), Misses, 0);
		TestEqual(FString::Printf(TEXT("%s: every floor an inner lining over its cell, facing up"), Case.Name), OffFloor, 0);
		W.Stop();
	}

	Config->RemoveFromRoot();
	VoxelConfig->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
