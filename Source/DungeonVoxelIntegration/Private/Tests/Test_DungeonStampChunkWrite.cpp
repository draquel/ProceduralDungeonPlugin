// Test_DungeonStampChunkWrite.cpp — a planned stamp's chunk written in one call is the chunk written a voxel at a time (G291).
//
// A planned dungeon stamp writes a chunk a tick (G280); each voxel went through UVoxelEditManager::ApplyEdit, and
// VoxelDemo's densest chunk was a tick of 10 to 14 ms. ApplyPlannedChunks now gives each chunk to
// UVoxelEditManager::ApplySetVoxelsInChunk with no change record; voxel.Edit.ChunkSetWrite 0 puts the voxels through
// ApplyEdit one at a time again.
//
// - Dungeon.Stamp.ChunkWrite: on a plane, a small planet (the dungeon in a tangent frame) and a block world (cubic
//   meshing), the same plan applied in two worlds, one on each path, as the POI bridge applies it (a chunk an operation,
//   Local, System, outside the history): the first half of the chunks before the world streams anything (chunks not
//   loaded), the world settled, a player's replicated dig into a chunk of the second half, the rest, the dirty marks, the
//   world settled again. Both worlds end with the same edits file (every chunk's edits in their order and the marked
//   voxels: the dig kept where the stamp met it, the Local rule), the same OnChunkEdited broadcasts during the stamp
//   (chunk, source, bounds), the same chunk meshes of the stamp's chunks (a CRC of each), the same voxels taken; no undo
//   step and the System source left on both.

#include "Misc/AutomationTest.h"
#include "DungeonVoxelTestFixtures.h"
#include "DungeonConfiguration.h"
#include "DungeonTypes.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelStamper.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "VoxelChunkManager.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWorldConfiguration.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "Testing/VoxelTestScopedCVar.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonStampChunkWriteTest
{
	using namespace DungeonVoxelTesting;

	enum class EWorld : uint8 { Plane, Planet, Blocks };

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

	/** What one path's world ended with. */
	struct FOutcome
	{
		TArray<uint8> EditsFile;
		TArray<FString> Broadcasts;
		TMap<FIntVector, uint32> MeshCrcs;
		int32 Taken = 0;
		int32 Meshed = 0;
		int32 DugKept = 0;
		bool bUndo = false;
		EEditSource Source = EEditSource::Player;
		bool bSettled = false;
	};

	uint32 MeshCrc(const FChunkMeshData& Mesh)
	{
		uint32 Crc = FCrc::MemCrc32(Mesh.Positions.GetData(), Mesh.Positions.Num() * Mesh.Positions.GetTypeSize());
		Crc = FCrc::MemCrc32(Mesh.Normals.GetData(), Mesh.Normals.Num() * Mesh.Normals.GetTypeSize(), Crc);
		Crc = FCrc::MemCrc32(Mesh.Indices.GetData(), Mesh.Indices.Num() * Mesh.Indices.GetTypeSize(), Crc);
		Crc = FCrc::MemCrc32(Mesh.MaterialBlends.GetData(), Mesh.MaterialBlends.Num() * Mesh.MaterialBlends.GetTypeSize(), Crc);
		return FCrc::MemCrc32(Mesh.Colors.GetData(), Mesh.Colors.Num() * Mesh.Colors.GetTypeSize(), Crc);
	}

	/** The plan's chunks a to b written as the POI bridge writes them: a chunk an operation, Local, System. */
	int32 ApplyRange(UVoxelEditManager* Edits, const FDungeonPlannedStamp& Plan, int32 First, int32 End)
	{
		const EEditSource PrevSource = Edits->GetEditSource();
		const EVoxelEditReplication PrevReplication = Edits->GetEditReplication();
		Edits->SetEditReplication(EVoxelEditReplication::Local);
		int32 Taken = 0;
		for (int32 Index = First; Index < End; ++Index)
		{
			Taken += UDungeonVoxelStamper::ApplyPlannedChunks(Edits, Plan, Index, 1);
		}
		Edits->SetEditReplication(PrevReplication);
		(void)PrevSource; // the stamp leaves the System source set, as the one-frame stamp does
		return Taken;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStampChunkWriteTest, "Dungeon.Stamp.ChunkWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStampChunkWriteTest::RunTest(const FString& Parameters)
{
	using namespace DungeonStampChunkWriteTest;
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

	for (const EWorld Kind : { EWorld::Plane, EWorld::Planet, EWorld::Blocks })
	{
		const TCHAR* Name = Kind == EWorld::Plane ? TEXT("plane") : (Kind == EWorld::Planet ? TEXT("planet") : TEXT("block world"));
		const FString What = Name;
		const bool bPlanet = Kind == EWorld::Planet;
		const FTransform Frame = bPlanet ? TangentFrame(40.0, 25.0, Radius) : FTransform::Identity;
		const FVector Offset = bPlanet
			? FVector(-Result.GridSize.X * CS * 0.5f, -Result.GridSize.Y * CS * 0.5f, -1500.0f - Result.GridSize.Z * CS)
			: FVector(-1713.37, 942.61, -2611.29);
		const EDungeonStampMode Mode = EDungeonStampMode::CarveOnly;

		FDungeonPlannedStamp Plan;
		FOutcome Outcomes[2];
		bool bRan[2] = { false, false };
		for (const int32 Path : { 0, 1 })
		{
			VoxelTesting::FScopedCVar PathCVar(TEXT("voxel.Edit.ChunkSetWrite"), Path);
			VoxelTesting::FTestChunkWorld W;
			W.Create(Path == 0 ? TEXT("DungeonChunkWriteA") : TEXT("DungeonChunkWriteB"));
			if (bPlanet)
			{
				W.ConfigureSmallPlanet(Radius);
			}
			else
			{
				W.ConfigureSmallPlane();
				if (Kind == EWorld::Blocks)
				{
					W.Config->MeshingMode = EMeshingMode::Cubic;
				}
			}
			W.StartManager();
			UVoxelEditManager* Edits = W.Manager->GetEditManager();
			FDungeonStampWorldLayout Layout;
			if (!TestTrue(What + TEXT(": the world's layout"), Edits && UDungeonVoxelStamper::GetWorldLayout(W.Manager, Layout)))
			{
				W.Stop();
				break;
			}
			if (Path == 0)
			{
				TestTrue(What + TEXT(": the plan"), UDungeonVoxelStamper::PlanStampInFrame(Result, Layout, Offset, Mode, VoxelConfig, Frame, Plan)
					&& Plan.bValid && Plan.Chunks.Num() > 2);
			}
			if (!Plan.bValid || Plan.Chunks.Num() < 3)
			{
				W.Stop();
				break;
			}

			FOutcome& Out = Outcomes[Path];
			Edits->OnChunkEdited.AddLambda([&Out, Edits](const FIntVector& Chunk, EEditSource Source, const FVector& Centre, float EditRadius)
			{
				FIntVector Min, Max;
				const bool bBounds = Edits->GetBroadcastEditBounds(Min, Max);
				Out.Broadcasts.Add(FString::Printf(TEXT("%s src=%d r=%.1f b=%d %s..%s"), *Chunk.ToString(), static_cast<int32>(Source), EditRadius,
					bBounds ? 1 : 0, *Min.ToString(), *Max.ToString()));
			});

			// The first half before the world streams anything: its chunks are not loaded.
			const int32 Half = Plan.Chunks.Num() / 2;
			Out.Taken += ApplyRange(Edits, Plan, 0, Half);

			// The world streams around the dungeon's middle (an edited chunk is held resident and meshed wherever it is).
			const FVector Middle = Plan.Lattice.ToWorld(Offset + FVector(Result.GridSize) * CS * 0.5f);
			const FVector Up = bPlanet ? Middle.GetSafeNormal() : FVector::UpVector;
			W.SetViewer(Middle + Up * 2500.0f);
			const bool bFirstSettle = W.Settle(180.0);

			// A player's replicated dig into a chunk of the second half: the stamp's Local writes leave its voxels.
			const FDungeonStampChunkWrites& Dug = Plan.Chunks[Half];
			const FVector DigAt = Plan.Lattice.EditPosition(Dug.Writes[Dug.Writes.Num() / 2].Index);
			Edits->SetEditSource(EEditSource::Player);
			Edits->SetEditReplication(EVoxelEditReplication::Replicated);
			FVoxelBrushParams Brush;
			Brush.Radius = Layout.VoxelSize * 2.5f;
			Brush.DensityDelta = 200;
			Edits->ApplyBrushEdit(DigAt, Brush, EEditMode::Subtract);
			Edits->ClearHistory();

			Out.Taken += ApplyRange(Edits, Plan, Half, Plan.Chunks.Num());
			UDungeonVoxelStamper::MarkPlannedChunksDirty(W.Manager, Plan);
			Out.bSettled = bFirstSettle && W.Settle(180.0);

			Out.EditsFile.Reset();
			const FString File = FPaths::Combine(FPaths::AutomationTransientDir(), FString::Printf(TEXT("DungeonChunkWrite_%d.vedits"), Path));
			if (Edits->SaveEditsToFile(File))
			{
				FFileHelper::LoadFileToArray(Out.EditsFile, *File);
				IFileManager::Get().Delete(*File);
			}
			for (const FDungeonStampChunkWrites& Chunk : Plan.Chunks)
			{
				if (const FChunkMeshData* Mesh = W.Renderer.ChunkMeshes.Find(Chunk.ChunkCoord))
				{
					Out.MeshCrcs.Add(Chunk.ChunkCoord, MeshCrc(*Mesh));
					Out.Meshed += Mesh->Indices.Num() > 0 ? 1 : 0;
				}
			}
			// The dig's voxels the stamp writes: marked, so the stamp left them as the dig made them.
			if (const FChunkEditLayer* Layer = Edits->GetEditLayer(Dug.ChunkCoord))
			{
				const FIntVector First = Dug.ChunkCoord * Layout.ChunkSize;
				for (const FDungeonStampWrite& Write : Dug.Writes)
				{
					const FIntVector Local = Write.Index - First;
					const FVoxelEdit* Edit = Layer->GetEdit(Local);
					Out.DugKept += Layer->IsReplicatedVoxel(Layer->ToIndex(Local)) && !(Edit && Edit->EditMode == EEditMode::Set
						&& Edit->NewData.Pack() == Write.PackedVoxel) ? 1 : 0;
				}
			}
			Out.bUndo = Edits->CanUndo();
			Out.Source = Edits->GetEditSource();
			bRan[Path] = true;
			W.Stop();
		}

		const FOutcome* A = &Outcomes[0];
		const FOutcome* B = &Outcomes[1];
		if (!TestTrue(What + TEXT(": both paths ran"), bRan[0] && bRan[1]))
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("%s: %d chunks, %d voxels; %d taken; %d left as the dig made them; %d of the stamp's chunks meshed; %d broadcasts"),
			*What, Plan.Chunks.Num(), Plan.Voxels, B->Taken, B->DugKept, B->Meshed, B->Broadcasts.Num()));
		TestTrue(What + TEXT(": both worlds settle"), A->bSettled && B->bSettled);
		TestTrue(What + TEXT(": the stamp is meshed (non-vacuous)"), B->Meshed > 0);
		TestTrue(What + TEXT(": the edits file is written (non-vacuous)"), B->EditsFile.Num() > 10000);
		TestTrue(What + TEXT(": the same edits file (the edits in their order, the dig's marked voxels kept)"), A->EditsFile == B->EditsFile);
		TestTrue(What + TEXT(": the dig's voxels kept where the stamp met them (non-vacuous)"), B->DugKept > 0);
		TestEqual(What + TEXT(": ... the same on both paths"), B->DugKept, A->DugKept);
		TestEqual(What + TEXT(": the same voxels taken"), B->Taken, A->Taken);
		TestEqual(What + TEXT(": the voxels taken are the plan's"), B->Taken, Plan.Voxels);
		TestEqual(What + TEXT(": the same broadcasts"), FString::Join(B->Broadcasts, TEXT("\n")), FString::Join(A->Broadcasts, TEXT("\n")));
		TestEqual(What + TEXT(": the same chunk meshes of the stamp's chunks"), B->MeshCrcs.OrderIndependentCompareEqual(A->MeshCrcs), true);
		TestFalse(What + TEXT(": no undo step"), A->bUndo || B->bUndo);
		TestEqual(What + TEXT(": the System source left set"), B->Source, EEditSource::System);
	}

	Config->RemoveFromRoot();
	VoxelConfig->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
