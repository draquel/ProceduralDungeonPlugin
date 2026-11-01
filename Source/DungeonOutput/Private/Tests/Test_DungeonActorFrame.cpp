// Test_DungeonActorFrame.cpp — ADungeonActor::bMapInActorFrame (spherical Phase 5, SP-G6).
//
// A planet POI's tile actor stands in the dungeon's pad space, a rotated frame, and lays its tiles out relative to itself
// (Plugins/VoxelWorlds/Documentation/WORLD_MODES_REVIEW_PLAN.md §19). Off (the default) the tiles are world space from the
// actor's location and its rotation is ignored, as before.
//
// - Dungeon.TileMapper.RotatedFrame: with bMapInActorFrame off every instance is the tile map built at the actor's location
//   (rotated actor or not); with it on every instance is the tile map built at a zero origin, carried by the actor's
//   rotated transform, and the entrance position goes with it.

#include "Misc/AutomationTest.h"
#include "DungeonActor.h"
#include "DungeonConfiguration.h"
#include "DungeonTileMapper.h"
#include "DungeonTileSet.h"
#include "DungeonTypes.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonActorFrameTest
{
	/** Every tile instance of an actor, in world space. */
	TArray<FTransform> GetInstances(const ADungeonActor& Actor)
	{
		TArray<FTransform> Out;
		TArray<UHierarchicalInstancedStaticMeshComponent*> Components;
		Actor.GetComponents(Components);
		for (const UHierarchicalInstancedStaticMeshComponent* Component : Components)
		{
			for (int32 i = 0; i < Component->GetInstanceCount(); ++i)
			{
				FTransform Xf;
				if (Component->GetInstanceTransform(i, Xf, /*bWorldSpace=*/true))
				{
					Out.Add(Xf);
				}
			}
		}
		return Out;
	}

	/** Every tile transform of a tile map (the default tileset's single-mesh pieces: one instance each). */
	TArray<FTransform> GetTiles(const FDungeonTileMapResult& TileMap, const FTransform* Carrier)
	{
		TArray<FTransform> Out;
		for (int32 Type = 0; Type < FDungeonTileMapResult::TypeCount; ++Type)
		{
			for (const FTransform& Xf : TileMap.Transforms[Type])
			{
				Out.Add(Carrier ? Xf * *Carrier : Xf);
			}
		}
		return Out;
	}

	bool Near(const FTransform& A, const FTransform& B)
	{
		return A.GetLocation().Equals(B.GetLocation(), 0.5)
			&& A.GetRotation().AngularDistance(B.GetRotation()) < 1.0e-3
			&& A.GetScale3D().Equals(B.GetScale3D(), 1.0e-3);
	}

	/** Expected transforms with no match among the actual ones (each actual used once), plus the count difference. */
	int32 CountUnmatched(const TArray<FTransform>& Expected, const TArray<FTransform>& Actual)
	{
		TArray<bool> Used;
		Used.SetNumZeroed(Actual.Num());
		int32 Unmatched = FMath::Abs(Expected.Num() - Actual.Num());
		for (const FTransform& E : Expected)
		{
			bool bFound = false;
			for (int32 i = 0; i < Actual.Num() && !bFound; ++i)
			{
				if (!Used[i] && Near(E, Actual[i]))
				{
					Used[i] = true;
					bFound = true;
				}
			}
			Unmatched += bFound ? 0 : 1;
		}
		return Unmatched;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonTileMapperRotatedFrameTest, "Dungeon.TileMapper.RotatedFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonTileMapperRotatedFrameTest::RunTest(const FString& Parameters)
{
	using namespace DungeonActorFrameTest;

	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(10, 10, 2);
	Config->RoomCount = 3;
	Config->MinRoomSize = FIntVector(2, 2, 1);
	Config->MaxRoomSize = FIntVector(3, 3, 1);
	Config->RoomBuffer = 1;
	Config->bUseFixedSeed = true;
	Config->FixedSeed = 777;
	UDungeonTileSet* TileSet = NewObject<UDungeonTileSet>(); // the engine cube in every base slot
	TileSet->AddToRoot();

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DungeonActorFrame"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);

	const FVector Location(-12345.6, 7890.1, -3456.7);
	const FTransform Rotated(FRotator(20.0f, 35.0f, -15.0f), Location);

	// The tile map at a zero origin and at the actor's location (BuildDungeon: the actor's own build).
	FDungeonResult Result;
	FDungeonTileMapResult AtZero, AtLocation;
	ADungeonActor::BuildDungeon(Config, TileSet, 777, false, FDungeonEntranceSpec(), false, FVector::ZeroVector, Result, AtZero);
	ADungeonActor::BuildDungeon(Config, TileSet, 777, false, FDungeonEntranceSpec(), false, Location, Result, AtLocation);

	struct FCase
	{
		const TCHAR* Name;
		FTransform ActorXf;
		bool bInFrame;
	};
	const FCase Cases[] = {
		{ TEXT("unrotated, world space"), FTransform(Location), false },
		{ TEXT("rotated, world space (rotation ignored)"), Rotated, false },
		{ TEXT("rotated, in the actor's frame"), Rotated, true },
	};
	for (const FCase& Case : Cases)
	{
		ADungeonActor* Actor = World->SpawnActorDeferred<ADungeonActor>(ADungeonActor::StaticClass(), Case.ActorXf);
		if (!TestNotNull(FString::Printf(TEXT("%s: the actor spawns"), Case.Name), Actor))
		{
			continue;
		}
		Actor->DungeonConfig = Config;
		Actor->TileSet = TileSet;
		Actor->Seed = 777;
		Actor->bSpawnInteractables = false;
		Actor->bMapInActorFrame = Case.bInFrame;
		Actor->FinishSpawning(Case.ActorXf);
		Actor->GenerateDungeon();

		const TArray<FTransform> Instances = GetInstances(*Actor);
		const TArray<FTransform> Expected = Case.bInFrame ? GetTiles(AtZero, &Case.ActorXf) : GetTiles(AtLocation, nullptr);
		const int32 Unmatched = CountUnmatched(Expected, Instances);
		AddInfo(FString::Printf(TEXT("%s: %d instances, %d expected, %d unmatched"), Case.Name, Instances.Num(), Expected.Num(), Unmatched));
		TestTrue(FString::Printf(TEXT("%s: tiles (non-vacuous)"), Case.Name), Instances.Num() > 50);
		TestEqual(FString::Printf(TEXT("%s: every instance where the tile map puts it"), Case.Name), Unmatched, 0);

		const FVector EntranceInMap = Result.GridToWorld(Result.EntranceCell) + FVector(Result.CellWorldSize * 0.5f, Result.CellWorldSize * 0.5f, 0.0f);
		const FVector ExpectedEntrance = Case.bInFrame ? Case.ActorXf.TransformPosition(EntranceInMap) : EntranceInMap + Location;
		TestTrue(FString::Printf(TEXT("%s: the entrance position"), Case.Name), Actor->GetEntranceWorldPosition().Equals(ExpectedEntrance, 0.5));
		Actor->Destroy();
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	Config->RemoveFromRoot();
	TileSet->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
