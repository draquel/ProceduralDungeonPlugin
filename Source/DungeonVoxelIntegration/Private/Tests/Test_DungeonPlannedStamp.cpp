// Test_DungeonPlannedStamp.cpp — a stamp planned on a worker and written a chunk at a time is the one-frame stamp (G280).
//
// A POI's dungeon stamp was one frame of 75 to 130 ms on VoxelDemo's game thread: 200,000 to 290,000 voxel writes through
// the edit manager. UDungeonVoxelStamper::PlanStampInFrame runs the stamp's passes without a voxel world (each voxel kept
// once, with its last write's value, by chunk) and ApplyPlannedChunks writes whole chunks of the plan, so the POI bridge
// plans on a worker and writes a few chunks a tick.
//
// - Dungeon.Stamp.Planned: in both voxel modes, on a plane (the identity frame, an origin with no phase against the lattice)
//   and in a tangent frame on a small planet, the plan made on a worker thread and applied a chunk at a time, last chunk
//   first, leaves exactly the one-frame stamp's edits (every edit's chunk, index, mode and data); the plan counts the
//   one-frame stamp's writes and one voxel per edit; each chunk of the plan holds only voxels of that chunk; half the chunks
//   applied leave exactly those chunks' voxels (no chunk partly written), and the rest applied after them the whole stamp;
//   the writes take no undo step and leave the System source.

#include "Misc/AutomationTest.h"
#include "DungeonVoxelTestFixtures.h"
#include "DungeonConfiguration.h"
#include "DungeonTypes.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelStamper.h"
#include "Tasks/Task.h"
#include "VoxelChunkManager.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWorldConfiguration.h"
#include "Testing/VoxelTestChunkWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonPlannedStampTest
{
	using namespace DungeonVoxelTesting;

	/** One edit as compared across worlds. */
	struct FEditKey
	{
		EEditMode Mode = EEditMode::Set;
		FVoxelData Data;
		int32 DensityDelta = 0;
		uint8 Material = 0;

		bool operator==(const FEditKey& Other) const
		{
			return Mode == Other.Mode && Data == Other.Data && DensityDelta == Other.DensityDelta && Material == Other.Material;
		}
	};

	using FEdits = TMap<FIntVector, TMap<int32, FEditKey>>;

	/** Every edit in the world by (chunk, index). */
	FEdits CollectEdits(const UVoxelEditManager& Edits)
	{
		FEdits Out;
		TArray<FIntVector> Chunks;
		Edits.GetEditedChunkCoords(Chunks);
		for (const FIntVector& Chunk : Chunks)
		{
			const FChunkEditLayer* Layer = Edits.GetEditLayer(Chunk);
			if (!Layer || Layer->IsEmpty())
			{
				continue;
			}
			TMap<int32, FEditKey>& InChunk = Out.Add(Chunk);
			for (const TPair<int32, FVoxelEdit>& Pair : Layer->Edits)
			{
				FEditKey Key;
				Key.Mode = Pair.Value.EditMode;
				Key.Data = Pair.Value.NewData;
				Key.DensityDelta = Pair.Value.DensityDelta;
				Key.Material = Pair.Value.BrushMaterialID;
				InChunk.Add(Pair.Key, Key);
			}
		}
		return Out;
	}

	/** Differences between two edit sets (missing on either side, or different data). */
	int32 CountDifferences(const FEdits& A, const FEdits& B)
	{
		int32 Differences = 0;
		for (const TPair<FIntVector, TMap<int32, FEditKey>>& Chunk : A)
		{
			const TMap<int32, FEditKey>* Other = B.Find(Chunk.Key);
			for (const TPair<int32, FEditKey>& Edit : Chunk.Value)
			{
				const FEditKey* Match = Other ? Other->Find(Edit.Key) : nullptr;
				Differences += (Match && *Match == Edit.Value) ? 0 : 1;
			}
		}
		for (const TPair<FIntVector, TMap<int32, FEditKey>>& Chunk : B)
		{
			const TMap<int32, FEditKey>* Other = A.Find(Chunk.Key);
			for (const TPair<int32, FEditKey>& Edit : Chunk.Value)
			{
				Differences += (Other && Other->Contains(Edit.Key)) ? 0 : 1;
			}
		}
		return Differences;
	}

	int32 CountEdits(const FEdits& Edits)
	{
		int32 Count = 0;
		for (const TPair<FIntVector, TMap<int32, FEditKey>>& Chunk : Edits)
		{
			Count += Chunk.Value.Num();
		}
		return Count;
	}

	/** The tangent frame at a latitude and longitude of a planet centred at the origin: x east, y north, z local up. */
	FTransform TangentFrame(double LatDeg, double LonDeg, double Radius)
	{
		const double Lat = FMath::DegreesToRadians(LatDeg);
		const double Lon = FMath::DegreesToRadians(LonDeg);
		const FVector Up(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
		const FVector East = FVector::CrossProduct(FVector::UpVector, Up).GetSafeNormal();
		const FVector North = FVector::CrossProduct(Up, East);
		return FTransform(East, North, Up, Up * Radius);
	}

	/** One stamp's world: a small plane or a small planet, its manager started. */
	struct FCase
	{
		const TCHAR* Name;
		bool bPlanet;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStampPlannedTest, "Dungeon.Stamp.Planned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStampPlannedTest::RunTest(const FString& Parameters)
{
	using namespace DungeonPlannedStampTest;
	UDungeonConfiguration* Config = MakeConfig();
	UDungeonVoxelConfig* VoxelConfig = MakeVoxelConfig(3);
	VoxelConfig->CarveMarginVoxels = 1.5f;
	const FDungeonResult Result = Generate(Config);
	if (!TestTrue(TEXT("The dungeon generates (non-vacuous)"), Result.Rooms.Num() > 0))
	{
		Config->RemoveFromRoot();
		VoxelConfig->RemoveFromRoot();
		return false;
	}
	const float CS = Result.CellWorldSize;
	const double Radius = 20000.0;

	for (const FCase& Case : { FCase{ TEXT("plane"), false }, FCase{ TEXT("planet"), true } })
	{
		// The plane: an origin with no particular phase against the lattice. The planet: the grid 1,500 under the datum in
		// the tangent frame at latitude 40, longitude 25.
		const FTransform Frame = Case.bPlanet ? TangentFrame(40.0, 25.0, Radius) : FTransform::Identity;
		const FVector Offset = Case.bPlanet
			? FVector(-Result.GridSize.X * CS * 0.5f, -Result.GridSize.Y * CS * 0.5f, -1500.0f - Result.GridSize.Z * CS)
			: FVector(-1713.37, 942.61, -2611.29);
		for (const EDungeonStampMode Mode : { EDungeonStampMode::CarveUnderground, EDungeonStampMode::CarveOnly })
		{
			const FString What = FString::Printf(TEXT("%s, %s"), Case.Name,
				Mode == EDungeonStampMode::CarveOnly ? TEXT("CarveOnly") : TEXT("CarveUnderground"));
			auto MakeWorld = [&](VoxelTesting::FTestChunkWorld& W, const TCHAR* WorldName)
			{
				W.Create(WorldName);
				if (Case.bPlanet)
				{
					W.ConfigureSmallPlanet(Radius);
				}
				else
				{
					W.ConfigureSmallPlane();
				}
				W.StartManager();
			};

			// The one-frame stamp.
			FEdits OneFrame;
			int32 OneFrameWrites = 0;
			{
				VoxelTesting::FTestChunkWorld W;
				MakeWorld(W, TEXT("DungeonPlannedStampA"));
				UDungeonVoxelStamper* Stamper = NewObject<UDungeonVoxelStamper>();
				const FDungeonStampResult Stamp = Stamper->StampDungeonInFrame(Result, W.Manager, Offset, Mode, VoxelConfig, Frame);
				TestTrue(What + TEXT(": the one-frame stamp succeeds"), Stamp.bSuccess);
				OneFrameWrites = Stamp.VoxelsModified;
				if (const UVoxelEditManager* Edits = W.Manager->GetEditManager())
				{
					OneFrame = CollectEdits(*Edits);
				}
				W.Stop();
			}

			// The planned stamp: planned on a worker, written a chunk at a time, last chunk first; half of it, then the rest.
			VoxelTesting::FTestChunkWorld W;
			MakeWorld(W, TEXT("DungeonPlannedStampB"));
			UVoxelEditManager* Edits = W.Manager->GetEditManager();
			FDungeonStampWorldLayout Layout;
			if (!TestTrue(What + TEXT(": the world's layout"), Edits && UDungeonVoxelStamper::GetWorldLayout(W.Manager, Layout)))
			{
				W.Stop();
				continue;
			}
			FDungeonPlannedStamp Plan;
			bool bPlanned = false;
			UE::Tasks::Launch(UE_SOURCE_LOCATION, [&]()
			{
				bPlanned = UDungeonVoxelStamper::PlanStampInFrame(Result, Layout, Offset, Mode, VoxelConfig, Frame, Plan);
			}).Wait();
			if (!TestTrue(What + TEXT(": the plan is made on a worker"), bPlanned && Plan.bValid && Plan.Chunks.Num() > 1))
			{
				W.Stop();
				continue;
			}
			AddInfo(FString::Printf(TEXT("%s: %d writes, %d voxels in %d chunks, planned in %.1f ms"),
				*What, Plan.VoxelWrites, Plan.Voxels, Plan.Chunks.Num(), Plan.PlanTimeMs));
			TestEqual(What + TEXT(": the plan counts the one-frame stamp's writes"), Plan.VoxelWrites, OneFrameWrites);

			// Each chunk of the plan holds voxels of that chunk only, once each.
			int32 Misfiled = 0;
			int32 Repeated = 0;
			for (const FDungeonStampChunkWrites& Chunk : Plan.Chunks)
			{
				TSet<FIntVector> Seen;
				for (const FDungeonStampWrite& Write : Chunk.Writes)
				{
					Misfiled += W.Manager->WorldToChunkCoord(Plan.Lattice.EditPosition(Write.Index)) == Chunk.ChunkCoord ? 0 : 1;
					bool bAlready = false;
					Seen.Add(Write.Index, &bAlready);
					Repeated += bAlready ? 1 : 0;
				}
			}
			TestEqual(What + TEXT(": every voxel is in its own chunk's share"), Misfiled, 0);
			TestEqual(What + TEXT(": every voxel once"), Repeated, 0);

			// Half the chunks (the last ones first): exactly those chunks' voxels, each chunk whole.
			const int32 Half = Plan.Chunks.Num() / 2;
			for (int32 Index = Plan.Chunks.Num() - 1; Index >= Half; --Index)
			{
				UDungeonVoxelStamper::ApplyPlannedChunks(Edits, Plan, Index, 1);
			}
			{
				const FEdits Partial = CollectEdits(*Edits);
				int32 Expected = 0;
				int32 Missing = 0;
				for (int32 Index = Half; Index < Plan.Chunks.Num(); ++Index)
				{
					const FDungeonStampChunkWrites& Chunk = Plan.Chunks[Index];
					Expected += Chunk.Writes.Num();
					const TMap<int32, FEditKey>* Layer = Partial.Find(Chunk.ChunkCoord);
					Missing += Layer ? FMath::Max(Chunk.Writes.Num() - Layer->Num(), 0) : Chunk.Writes.Num();
				}
				TestEqual(What + TEXT(": half applied holds exactly the applied chunks' voxels"), CountEdits(Partial), Expected);
				TestEqual(What + TEXT(": ... each of those chunks whole"), Missing, 0);
				int32 Early = 0;
				for (int32 Index = 0; Index < Half; ++Index)
				{
					Early += Partial.Contains(Plan.Chunks[Index].ChunkCoord) ? 1 : 0;
				}
				TestEqual(What + TEXT(": ... and nothing of the chunks not applied yet"), Early, 0);
			}
			for (int32 Index = Half - 1; Index >= 0; --Index)
			{
				UDungeonVoxelStamper::ApplyPlannedChunks(Edits, Plan, Index, 1);
			}
			UDungeonVoxelStamper::MarkPlannedChunksDirty(W.Manager, Plan);

			const FEdits Planned = CollectEdits(*Edits);
			TestTrue(What + TEXT(": the stamp edits (non-vacuous)"), CountEdits(OneFrame) > 1000);
			TestEqual(What + TEXT(": one voxel of the plan per edit"), CountEdits(Planned), Plan.Voxels);
			TestEqual(What + TEXT(": the planned stamp leaves exactly the one-frame stamp's edits"), CountDifferences(OneFrame, Planned), 0);
			TestFalse(What + TEXT(": no undo step"), Edits->CanUndo());
			TestEqual(What + TEXT(": the System source left set"), Edits->GetEditSource(), EEditSource::System);
			W.Stop();
		}
	}

	Config->RemoveFromRoot();
	VoxelConfig->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
