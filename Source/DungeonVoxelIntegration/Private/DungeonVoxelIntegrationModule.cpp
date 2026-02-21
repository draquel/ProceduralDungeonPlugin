#include "DungeonVoxelIntegration.h"
#include "DungeonVoxelStamper.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelTypes.h"
#include "DungeonGenerator.h"
#include "DungeonConfig.h"
#include "DungeonTypes.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldTestActor.h"
#include "VoxelData.h"
#include "VoxelWorldConfiguration.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogDungeonVoxelIntegration);

static void TestDungeonStamp(const TArray<FString>& Args)
{
	// Find PIE world
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			World = Context.World();
			break;
		}
	}

	if (!World)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("TestDungeonStamp: No PIE world found — start PIE first"));
		return;
	}

	// Find VoxelWorldTestActor
	AVoxelWorldTestActor* VoxelActor = nullptr;
	for (TActorIterator<AVoxelWorldTestActor> It(World); It; ++It)
	{
		VoxelActor = *It;
		break;
	}

	if (!VoxelActor)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("TestDungeonStamp: No AVoxelWorldTestActor found in PIE world"));
		return;
	}

	UVoxelChunkManager* ChunkManager = VoxelActor->GetChunkManager();
	if (!ChunkManager)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("TestDungeonStamp: ChunkManager is null"));
		return;
	}

	UE_LOG(LogDungeonVoxelIntegration, Log, TEXT("TestDungeonStamp: Found VoxelWorldTestActor and ChunkManager"));

	// Create dungeon configuration — small grid for quick visual testing
	UDungeonConfiguration* DungeonConfig = NewObject<UDungeonConfiguration>();
	DungeonConfig->GridSize = FIntVector(15, 15, 3);
	DungeonConfig->RoomCount = 5;
	DungeonConfig->CellWorldSize = 400.0f;
	DungeonConfig->MinRoomSize = FIntVector(3, 3, 1);
	DungeonConfig->MaxRoomSize = FIntVector(5, 5, 2);
	DungeonConfig->RoomBuffer = 1;
	DungeonConfig->StaircaseRiseToRun = 2;
	DungeonConfig->StaircaseHeadroom = 2;
	DungeonConfig->bGuaranteeEntrance = true;
	DungeonConfig->bGuaranteeBossRoom = true;

	// Parse optional seed from args
	int64 Seed = 42;
	if (Args.Num() > 0)
	{
		Seed = FCString::Atoi64(*Args[0]);
	}

	// Generate dungeon
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	FDungeonResult Result = Generator->Generate(DungeonConfig, Seed);

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("TestDungeonStamp: Generated dungeon — %d rooms, %d hallways, %d staircases, entrance=(%d,%d,%d), gen=%.1fms"),
		Result.Rooms.Num(), Result.Hallways.Num(), Result.Staircases.Num(),
		Result.EntranceCell.X, Result.EntranceCell.Y, Result.EntranceCell.Z,
		Result.GenerationTimeMs);

	if (Result.Rooms.Num() < 2)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("TestDungeonStamp: Dungeon generation failed (< 2 rooms)"));
		return;
	}

	// Create voxel config
	UDungeonVoxelConfig* VoxelConfig = NewObject<UDungeonVoxelConfig>();
	VoxelConfig->WallMaterialID = 2;    // Stone
	VoxelConfig->FloorMaterialID = 2;
	VoxelConfig->CeilingMaterialID = 2;
	VoxelConfig->StaircaseMaterialID = 1; // Dirt for contrast
	VoxelConfig->DoorFrameMaterialID = 2;
	VoxelConfig->WallThickness = 1;
	VoxelConfig->DungeonBiomeID = 0;

	// Place dungeon underground: offset below terrain
	// Parse optional Z offset from second arg (default -500 to stay near loaded chunks)
	float ZOffset = -500.0f;
	if (Args.Num() > 1)
	{
		ZOffset = FCString::Atof(*Args[1]);
	}
	const FVector WorldOffset(0.0f, 0.0f, ZOffset);

	// Parse stamp mode from third arg (default=CarveUnderground, 1=ReplaceRegion)
	EDungeonStampMode StampMode = EDungeonStampMode::CarveUnderground;
	if (Args.Num() > 2)
	{
		int32 ModeInt = FCString::Atoi(*Args[2]);
		if (ModeInt >= 0 && ModeInt <= 2)
		{
			StampMode = static_cast<EDungeonStampMode>(ModeInt);
		}
	}

	// Log chunk states before stamping
	UE_LOG(LogDungeonVoxelIntegration, Log, TEXT("--- Chunk States Before Stamp ---"));
	for (int32 CZ = -2; CZ <= 2; ++CZ)
	{
		for (int32 CX = 0; CX <= 2; ++CX)
		{
			const FIntVector CC(CX, 0, CZ);
			const EChunkState CS = ChunkManager->GetChunkState(CC);
			UE_LOG(LogDungeonVoxelIntegration, Log, TEXT("  Chunk(%d,0,%d) = %d"), CX, CZ, static_cast<int32>(CS));
		}
	}

	// Stamp!
	UDungeonVoxelStamper* Stamper = NewObject<UDungeonVoxelStamper>();
	FDungeonStampResult StampResult = Stamper->StampDungeon(
		Result, ChunkManager, WorldOffset,
		StampMode, VoxelConfig);

	if (!StampResult.bSuccess)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error,
			TEXT("TestDungeonStamp: FAILED — %s"), *StampResult.ErrorMessage);
		return;
	}

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("TestDungeonStamp: SUCCESS — %d voxels modified, %d chunks affected, %.1fms"),
		StampResult.VoxelsModified, StampResult.ChunksAffected, StampResult.StampTimeMs);

	// Find a room on floor 0 to use as verification/teleport target
	const FDungeonRoom* TargetRoom = nullptr;
	for (const FDungeonRoom& Room : Result.Rooms)
	{
		if (Room.FloorLevel == 0)
		{
			TargetRoom = &Room;
			break;
		}
	}
	if (!TargetRoom && Result.Rooms.Num() > 0)
	{
		TargetRoom = &Result.Rooms[0];
	}

	const FIntVector RoomCenter = TargetRoom->Position + TargetRoom->Size / 2;
	const FVector RoomWorldCenter = WorldOffset + Result.GridToWorld(RoomCenter)
		+ FVector(Result.CellWorldSize * 0.5f);

	// Verify voxel edits by reading back data at room interior positions
	UE_LOG(LogDungeonVoxelIntegration, Log, TEXT("--- Voxel Verification ---"));
	const float CWS = Result.CellWorldSize;
	const float VS = ChunkManager->GetConfiguration()->VoxelSize;

	// Check 3 positions: room center, a wall boundary, and outside the grid
	auto QueryVoxel = [&](const FVector& Pos, const TCHAR* Label)
	{
		const FVoxelData V = ChunkManager->GetVoxelAtWorldPosition(Pos);
		const FIntVector CC = ChunkManager->WorldToChunkCoord(Pos);
		UE_LOG(LogDungeonVoxelIntegration, Log,
			TEXT("  %s at (%.0f,%.0f,%.0f) chunk(%d,%d,%d): Density=%d MatID=%d %s"),
			Label, Pos.X, Pos.Y, Pos.Z, CC.X, CC.Y, CC.Z,
			V.Density, V.MaterialID, V.IsAir() ? TEXT("AIR") : TEXT("SOLID"));
	};

	QueryVoxel(RoomWorldCenter, TEXT("RoomCenter"));
	QueryVoxel(RoomWorldCenter + FVector(0, 0, CWS * 0.5f - VS * 0.5f), TEXT("NearCeiling"));
	QueryVoxel(WorldOffset + FVector(-100, -100, 0), TEXT("OutsideGrid"));

	// Log room positions
	for (const FDungeonRoom& Room : Result.Rooms)
	{
		const FVector RC = WorldOffset + Result.GridToWorld(Room.Position + Room.Size / 2)
			+ FVector(CWS * 0.5f);
		UE_LOG(LogDungeonVoxelIntegration, Log,
			TEXT("  Room %d (%s) floor=%d center=(%.0f,%.0f,%.0f)"),
			Room.RoomIndex, *UEnum::GetValueAsString(Room.RoomType),
			Room.FloorLevel, RC.X, RC.Y, RC.Z);
	}

	// Log staircase positions
	for (int32 i = 0; i < Result.Staircases.Num(); ++i)
	{
		const FDungeonStaircase& S = Result.Staircases[i];
		const FVector BotWorld = WorldOffset + Result.GridToWorld(S.BottomCell) + FVector(CWS * 0.5f);
		const FVector TopWorld = WorldOffset + Result.GridToWorld(S.TopCell) + FVector(CWS * 0.5f);
		UE_LOG(LogDungeonVoxelIntegration, Log,
			TEXT("  Staircase %d: Bottom=(%d,%d,%d)(%.0f,%.0f,%.0f) Top=(%d,%d,%d)(%.0f,%.0f,%.0f) Dir=%d Run=%d"),
			i, S.BottomCell.X, S.BottomCell.Y, S.BottomCell.Z, BotWorld.X, BotWorld.Y, BotWorld.Z,
			S.TopCell.X, S.TopCell.Y, S.TopCell.Z, TopWorld.X, TopWorld.Y, TopWorld.Z,
			S.Direction, S.RiseRunRatio);
	}

	// Teleport player character into the room
	APlayerController* PC = World->GetFirstPlayerController();
	if (PC && PC->GetCharacter())
	{
		ACharacter* Character = PC->GetCharacter();

		// Disable collision so we don't get stuck in terrain
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->SetMovementMode(MOVE_Flying);
		}
		Character->SetActorEnableCollision(false);

		const FVector TeleportPos = RoomWorldCenter + FVector(0, 0, 50.0f);
		Character->TeleportTo(TeleportPos, FRotator::ZeroRotator);

		UE_LOG(LogDungeonVoxelIntegration, Log,
			TEXT("TestDungeonStamp: Teleported player to room center (%.0f, %.0f, %.0f)"),
			TeleportPos.X, TeleportPos.Y, TeleportPos.Z);
	}
	else
	{
		UE_LOG(LogDungeonVoxelIntegration, Warning,
			TEXT("TestDungeonStamp: No player character found to teleport"));
	}

	const FVector EntranceWorld = WorldOffset + Result.GridToWorld(Result.EntranceCell)
		+ FVector(CWS * 0.5f);
	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("TestDungeonStamp: Entrance at world pos (%.0f, %.0f, %.0f)"),
		EntranceWorld.X, EntranceWorld.Y, EntranceWorld.Z);
}

void FDungeonVoxelIntegrationModule::StartupModule()
{
	TestStampCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("Dungeon.TestStamp"),
		TEXT("Generate a test dungeon and stamp it into the voxel world (run during PIE). Optional arg: seed."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&TestDungeonStamp));

	UE_LOG(LogDungeonVoxelIntegration, Log, TEXT("DungeonVoxelIntegration module loaded. Console: Dungeon.TestStamp [seed]"));
}

void FDungeonVoxelIntegrationModule::ShutdownModule()
{
	TestStampCommand.Reset();
}

IMPLEMENT_MODULE(FDungeonVoxelIntegrationModule, DungeonVoxelIntegration)
