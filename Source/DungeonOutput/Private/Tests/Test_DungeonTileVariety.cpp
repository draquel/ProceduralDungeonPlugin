// Test_DungeonTileVariety.cpp — Environment E4: seeded slot variants, per-room-type overrides,
// density-driven decor, and the determinism the streamed tile actor relies on (same seed, same
// dungeon on every machine and every rebuild).
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "Engine/StaticMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonTileVarietyTestHelpers
{
	const FSoftObjectPath VarietyCube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	const FSoftObjectPath VarietySphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	const FSoftObjectPath VarietyCylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	UDungeonTileSet* MakeVarietyTileSet()
	{
		UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
		TS->AddToRoot();
		TS->FixtureRules.MaxWallLights = 0;
		return TS;
	}

	/** (N+2)x(N+2)x1: an NxN room at (1,1) of the given type ringed by RoomWall; room index 1. */
	FDungeonResult MakeRoom(int32 N, EDungeonRoomType Type, int64 Seed)
	{
		FDungeonResult R;
		R.Seed = Seed;
		R.GridSize = FIntVector(N + 2, N + 2, 1); R.CellWorldSize = 400.0f; R.Grid.Initialize(R.GridSize);
		for (int32 Y = 0; Y < N + 2; ++Y) for (int32 X = 0; X < N + 2; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1; }
		for (int32 Y = 1; Y <= N; ++Y) for (int32 X = 1; X <= N; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::Room; C.RoomIndex = 1; }
		FDungeonRoom Room;
		Room.RoomIndex = 1; Room.RoomType = Type; Room.Position = FIntVector(1, 1, 0); Room.Size = FIntVector(N, N, 1);
		R.Rooms.Add(Room);
		R.EntranceRoomIndex = -1;
		return R;
	}

	/** (N+2)x(N+2)xLevels: an NxN room at (1,1) spanning every level (a tall room: open vertically). */
	FDungeonResult MakeTallRoom(int32 N, int32 Levels, int64 Seed)
	{
		FDungeonResult R;
		R.Seed = Seed;
		R.GridSize = FIntVector(N + 2, N + 2, Levels); R.CellWorldSize = 400.0f; R.Grid.Initialize(R.GridSize);
		for (int32 Z = 0; Z < Levels; ++Z)
		{
			for (int32 Y = 0; Y < N + 2; ++Y) for (int32 X = 0; X < N + 2; ++X)
			{ FDungeonCell& C = R.Grid.GetCell(X, Y, Z); C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1; }
			for (int32 Y = 1; Y <= N; ++Y) for (int32 X = 1; X <= N; ++X)
			{ FDungeonCell& C = R.Grid.GetCell(X, Y, Z); C.CellType = EDungeonCellType::Room; C.RoomIndex = 1; }
		}
		FDungeonRoom Room;
		Room.RoomIndex = 1; Room.RoomType = EDungeonRoomType::Generic; Room.Position = FIntVector(1, 1, 0); Room.Size = FIntVector(N, N, Levels);
		R.Rooms.Add(Room);
		R.EntranceRoomIndex = -1;
		return R;
	}

	/** 7x5x1 corridor along y=2 from x=1..5. */
	FDungeonResult MakeCorridor(int64 Seed)
	{
		FDungeonResult R;
		R.Seed = Seed;
		R.GridSize = FIntVector(7, 5, 1); R.CellWorldSize = 400.0f; R.Grid.Initialize(R.GridSize);
		for (int32 X = 1; X <= 5; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, 2, 0); C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; }
		R.EntranceRoomIndex = -1;
		return R;
	}

	/** Distinct piece ids used by a type's instances. */
	TSet<int32> PiecesUsed(const FDungeonTileMapResult& Map, EDungeonTileType Type)
	{
		TSet<int32> S;
		for (int32 Id : Map.PieceIds[static_cast<int32>(Type)]) { S.Add(Id); }
		return S;
	}

	/** The mesh path a type's instance i resolves to (empty for modules / invalid). */
	FString MeshOf(const FDungeonTileMapResult& Map, EDungeonTileType Type, int32 i)
	{
		const TArray<int32>& Ids = Map.PieceIds[static_cast<int32>(Type)];
		if (!Ids.IsValidIndex(i) || !Map.Pieces.IsValidIndex(Ids[i])) { return FString(); }
		return Map.Pieces[Ids[i]].Mesh.ToSoftObjectPath().ToString();
	}
}

using namespace DungeonTileVarietyTestHelpers;

#define VARIETY_TEST(ClassName, PrettyName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, PrettyName, EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Variants: a slot's base geometry plus weighted variants are picked per placement by the seed;
// every instance carries a piece id; zero weight never places; determinism across rebuilds.
// ---------------------------------------------------------------------------
VARIETY_TEST(FTileVariants, "Dungeon.Variety.SeededVariants")
bool FTileVariants::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeVarietyTileSet();
	// No variants: every instance of a type shares one piece, and every instance has an id.
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeRoom(3, EDungeonRoomType::Generic, 7), *TS, FVector::ZeroVector);
		TestEqual(TEXT("ids parallel to transforms (floors)"), Map.PieceIds[static_cast<int32>(EDungeonTileType::RoomFloor)].Num(), Map.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Num());
		TestEqual(TEXT("ids parallel to transforms (walls)"), Map.PieceIds[static_cast<int32>(EDungeonTileType::WallSegment)].Num(), Map.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Num());
		TestEqual(TEXT("one piece for RoomFloor"), PiecesUsed(Map, EDungeonTileType::RoomFloor).Num(), 1);
		TestTrue(TEXT("pieces table populated"), Map.Pieces.Num() > 0);
		for (int32 Id : Map.PieceIds[static_cast<int32>(EDungeonTileType::RoomFloor)])
		{
			TestTrue(TEXT("floor piece is the cube"), Map.Pieces.IsValidIndex(Id) && Map.Pieces[Id].Type == EDungeonTileType::RoomFloor && Map.Pieces[Id].Mesh.ToSoftObjectPath() == VarietyCube);
		}
	}

	// Cube (weight 1) + sphere variant (weight 1) on a 5x5 room: both pieces appear, and the same
	// seed picks identically on a second map while another seed picks differently.
	{
		FDungeonTileSlot& Floor = TS->Slots.FindOrAdd(EDungeonTileType::RoomFloor);
		FDungeonTileVariant Sphere; Sphere.Mesh = TSoftObjectPtr<UStaticMesh>(VarietySphere); Sphere.Weight = 1.0f;
		Floor.Variants.Add(Sphere);
		const FDungeonTileMapResult A = FDungeonTileMapper::MapToTiles(MakeRoom(5, EDungeonRoomType::Generic, 42), *TS, FVector::ZeroVector);
		const FDungeonTileMapResult B = FDungeonTileMapper::MapToTiles(MakeRoom(5, EDungeonRoomType::Generic, 42), *TS, FVector::ZeroVector);
		const FDungeonTileMapResult C = FDungeonTileMapper::MapToTiles(MakeRoom(5, EDungeonRoomType::Generic, 43), *TS, FVector::ZeroVector);
		TestEqual(TEXT("25 floors"), A.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Num(), 25);
		TestEqual(TEXT("both pieces used across 25 cells"), PiecesUsed(A, EDungeonTileType::RoomFloor).Num(), 2);
		int32 Spheres = 0, DiffersFromC = 0;
		for (int32 i = 0; i < 25; ++i)
		{
			const FString MA = MeshOf(A, EDungeonTileType::RoomFloor, i);
			TestEqual(TEXT("same seed -> same pick"), MA, MeshOf(B, EDungeonTileType::RoomFloor, i));
			TestTrue(TEXT("same seed -> same transform"), A.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)][i].Equals(B.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)][i], 0.001f));
			if (MA == VarietySphere.ToString()) { ++Spheres; }
			if (MA != MeshOf(C, EDungeonTileType::RoomFloor, i)) { ++DiffersFromC; }
		}
		TestTrue(TEXT("roughly half spheres (weights 1:1)"), Spheres >= 5 && Spheres <= 20);
		TestTrue(TEXT("another seed picks differently somewhere"), DiffersFromC > 0);
		// The sphere is a single mesh: it is auto-fit like any floor (400 x 400 x 80 slab).
		for (int32 i = 0; i < 25; ++i)
		{
			if (MeshOf(A, EDungeonTileType::RoomFloor, i) == VarietySphere.ToString())
			{
				const FVector S = A.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)][i].GetScale3D();
				TestTrue(TEXT("variant single mesh auto-fit"), S.Equals(FVector(4.0f, 4.0f, 0.8f), 0.01f));
			}
		}
	}

	// Zero weight never places; a base of weight 0 with one live variant always places the variant.
	{
		FDungeonTileSlot& Floor = TS->Slots.FindOrAdd(EDungeonTileType::RoomFloor);
		Floor.Variants[0].Weight = 0.0f;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeRoom(5, EDungeonRoomType::Generic, 42), *TS, FVector::ZeroVector);
		for (int32 i = 0; i < 25; ++i) { TestEqual(TEXT("zero-weight variant never picked"), MeshOf(Map, EDungeonTileType::RoomFloor, i), VarietyCube.ToString()); }
		Floor.Variants[0].Weight = 1.0f;
		Floor.Weight = 0.0f;
		const FDungeonTileMapResult Map2 = FDungeonTileMapper::MapToTiles(MakeRoom(5, EDungeonRoomType::Generic, 42), *TS, FVector::ZeroVector);
		for (int32 i = 0; i < 25; ++i) { TestEqual(TEXT("zero-weight base -> always the variant"), MeshOf(Map2, EDungeonTileType::RoomFloor, i), VarietySphere.ToString()); }
		Floor.Weight = 1.0f;
	}

	TS->RemoveFromRoot();
	return true;
}

// ---------------------------------------------------------------------------
// Room-type overrides swap a typed room's geometry without touching other rooms or types.
// ---------------------------------------------------------------------------
VARIETY_TEST(FTileRoomTypeOverrides, "Dungeon.Variety.RoomTypeOverrides")
bool FTileRoomTypeOverrides::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeVarietyTileSet();
	FDungeonRoomTypeOverride Boss;
	FDungeonTileSlot BossFloor; BossFloor.Mesh = TSoftObjectPtr<UStaticMesh>(VarietyCylinder);
	Boss.Slots.Add(EDungeonTileType::RoomFloor, BossFloor);
	// An override for a type the base tileset does not place at all: ignored (the base decides
	// what renders; overrides only swap geometry).
	FDungeonTileSlot BossDecor; BossDecor.Mesh = TSoftObjectPtr<UStaticMesh>(VarietySphere);
	Boss.Slots.Add(EDungeonTileType::FloorDecor, BossDecor);
	TS->RoomTypeOverrides.Add(EDungeonRoomType::Boss, Boss);

	const FDungeonTileMapResult Generic = FDungeonTileMapper::MapToTiles(MakeRoom(3, EDungeonRoomType::Generic, 5), *TS, FVector::ZeroVector);
	const FDungeonTileMapResult BossMap = FDungeonTileMapper::MapToTiles(MakeRoom(3, EDungeonRoomType::Boss, 5), *TS, FVector::ZeroVector);
	for (int32 i = 0; i < 9; ++i)
	{
		TestEqual(TEXT("generic room keeps the base floor"), MeshOf(Generic, EDungeonTileType::RoomFloor, i), VarietyCube.ToString());
		TestEqual(TEXT("boss room floor overridden"), MeshOf(BossMap, EDungeonTileType::RoomFloor, i), VarietyCylinder.ToString());
	}
	TestEqual(TEXT("boss walls untouched (no wall override)"), MeshOf(BossMap, EDungeonTileType::WallSegment, 0), VarietyCube.ToString());
	TestEqual(TEXT("override cannot add a type the base lacks"), BossMap.Transforms[static_cast<int32>(EDungeonTileType::FloorDecor)].Num(), 0);
	// Same floor placement (the override changes what, not where).
	TestTrue(TEXT("override keeps placement"), Generic.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)][4].Equals(BossMap.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)][4], 0.001f));

	TS->RemoveFromRoot();
	return true;
}

// ---------------------------------------------------------------------------
// Decor: wall decor on rock-backed faces (never on fixture faces), floor decor on room cells,
// hallway decor on hallway cells; densities and seeds decide; anchors follow the conventions.
// ---------------------------------------------------------------------------
VARIETY_TEST(FTileDecor, "Dungeon.Variety.DecorByDensity")
bool FTileDecor::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeVarietyTileSet();
	FDungeonTileSlot Cube; Cube.Mesh = TSoftObjectPtr<UStaticMesh>(VarietyCube);
	TS->Slots.Add(EDungeonTileType::WallDecor, Cube);
	TS->Slots.Add(EDungeonTileType::FloorDecor, Cube);
	TS->Slots.Add(EDungeonTileType::HallwayDecor, Cube);

	// Density 1: every rock-backed face of the 3x3 room (12) and every room cell (9); nothing in
	// a room from the hallway slot. Anchors: wall decor on the finished face (40 in) at floor
	// level with +X into the cell; floor decor at the cell centre on the floor, uniform scale 1.
	{
		TS->DecorRules.WallDecorDensity = 1.0f; TS->DecorRules.FloorDecorDensity = 1.0f; TS->DecorRules.HallwayDecorDensity = 1.0f;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeRoom(3, EDungeonRoomType::Generic, 9), *TS, FVector::ZeroVector);
		TestEqual(TEXT("density 1: 12 wall decor"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)].Num(), 12);
		TestEqual(TEXT("density 1: 9 floor decor"), Map.Transforms[static_cast<int32>(EDungeonTileType::FloorDecor)].Num(), 9);
		TestEqual(TEXT("no hallway decor in a room"), Map.Transforms[static_cast<int32>(EDungeonTileType::HallwayDecor)].Num(), 0);
		for (const FTransform& Xf : Map.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)])
		{
			const FVector P = Xf.GetLocation();
			const bool bOnFace = FMath::IsNearlyEqual(P.X, 440.0f, 0.5f) || FMath::IsNearlyEqual(P.X, 1560.0f, 0.5f)
				|| FMath::IsNearlyEqual(P.Y, 440.0f, 0.5f) || FMath::IsNearlyEqual(P.Y, 1560.0f, 0.5f);
			TestTrue(TEXT("wall decor on a finished face"), bOnFace);
			TestTrue(TEXT("wall decor at floor level"), FMath::IsNearlyEqual(P.Z, 0.0f, 0.5f));
			TestTrue(TEXT("wall decor uniform profile scale"), Xf.GetScale3D().Equals(FVector(1.0f), 0.001f));
			// +X points into the cell: away from the face it sits on.
			const FVector Dir = Xf.GetUnitAxis(EAxis::X);
			const FVector ToCentre = (FVector(1000.0f, 1000.0f, 0.0f) - P).GetSafeNormal2D();
			TestTrue(TEXT("wall decor +X into the room"), FVector::DotProduct(Dir, ToCentre) > 0.5f);
		}
		int32 YawSteps[4] = {};
		for (const FTransform& Xf : Map.Transforms[static_cast<int32>(EDungeonTileType::FloorDecor)])
		{
			const FVector P = Xf.GetLocation();
			TestTrue(TEXT("floor decor at a cell centre"), FMath::IsNearlyEqual(FMath::Fmod(P.X - 200.0f, 400.0f), 0.0f, 0.5f) && FMath::IsNearlyEqual(FMath::Fmod(P.Y - 200.0f, 400.0f), 0.0f, 0.5f));
			TestTrue(TEXT("floor decor on the floor"), FMath::IsNearlyEqual(P.Z, 0.0f, 0.5f));
			const float Yaw = FRotator::NormalizeAxis(Xf.Rotator().Yaw);
			const int32 Step = FMath::RoundToInt(Yaw / 90.0f);
			TestTrue(TEXT("floor decor yaw is a quarter turn"), FMath::IsNearlyEqual(Yaw, Step * 90.0f, 0.01f));
			++YawSteps[(Step % 4 + 4) % 4];
		}
	}

	// A two-storey room: floor decor stands only where a floor is placed (the lower level); the
	// upper cells have no floor of their own and must not float decor at their plane. Wall decor
	// still dresses both levels' walls.
	{
		TS->DecorRules.WallDecorDensity = 1.0f; TS->DecorRules.FloorDecorDensity = 1.0f; TS->DecorRules.HallwayDecorDensity = 1.0f;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeTallRoom(3, 2, 9), *TS, FVector::ZeroVector);
		const TArray<FTransform>& Floor = Map.Transforms[static_cast<int32>(EDungeonTileType::FloorDecor)];
		TestEqual(TEXT("two-storey room: floor decor only on the floored level"), Floor.Num(), 9);
		for (const FTransform& Xf : Floor)
		{
			TestTrue(TEXT("two-storey room: floor decor at the lower floor"), FMath::IsNearlyEqual(Xf.GetLocation().Z, 0.0f, 0.5f));
		}
		TestEqual(TEXT("two-storey room: wall decor on both levels"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)].Num(), 24);
	}

	// Density 0: nothing. A fixture face never takes wall decor (bSkipFixtureFaces).
	{
		TS->DecorRules.WallDecorDensity = 0.0f; TS->DecorRules.FloorDecorDensity = 0.0f; TS->DecorRules.HallwayDecorDensity = 0.0f;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeRoom(3, EDungeonRoomType::Generic, 9), *TS, FVector::ZeroVector);
		TestEqual(TEXT("density 0: no wall decor"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)].Num(), 0);
		TestEqual(TEXT("density 0: no floor decor"), Map.Transforms[static_cast<int32>(EDungeonTileType::FloorDecor)].Num(), 0);

		TS->DecorRules.WallDecorDensity = 1.0f;
		TS->FixtureRules.MaxWallLights = 24; TS->FixtureRules.RoomWallLightEvery = 1; // every rock-backed face lit
		const FDungeonTileMapResult Lit = FDungeonTileMapper::MapToTiles(MakeRoom(3, EDungeonRoomType::Generic, 9), *TS, FVector::ZeroVector);
		TestEqual(TEXT("all 12 faces carry fixtures"), Lit.Fixtures.Num(), 12);
		TestEqual(TEXT("fixture faces take no wall decor"), Lit.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)].Num(), 0);
		TS->DecorRules.bSkipFixtureFaces = false;
		const FDungeonTileMapResult Shared = FDungeonTileMapper::MapToTiles(MakeRoom(3, EDungeonRoomType::Generic, 9), *TS, FVector::ZeroVector);
		TestEqual(TEXT("sharing allowed -> 12 wall decor beside fixtures"), Shared.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)].Num(), 12);
		TS->DecorRules.bSkipFixtureFaces = true;
		TS->FixtureRules.MaxWallLights = 0;
	}

	// Hallway decor: corridor of 5 cells at density 1 -> 5, on hallway cells only; a fractional
	// density is deterministic for a seed and lands between 0 and all.
	{
		TS->DecorRules.HallwayDecorDensity = 1.0f; TS->DecorRules.FloorDecorDensity = 1.0f;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeCorridor(3), *TS, FVector::ZeroVector);
		TestEqual(TEXT("hallway decor on every corridor cell"), Map.Transforms[static_cast<int32>(EDungeonTileType::HallwayDecor)].Num(), 5);
		TestEqual(TEXT("no floor decor in a corridor"), Map.Transforms[static_cast<int32>(EDungeonTileType::FloorDecor)].Num(), 0);
		TS->DecorRules.WallDecorDensity = 0.5f;
		const FDungeonTileMapResult H1 = FDungeonTileMapper::MapToTiles(MakeRoom(5, EDungeonRoomType::Generic, 11), *TS, FVector::ZeroVector);
		const FDungeonTileMapResult H2 = FDungeonTileMapper::MapToTiles(MakeRoom(5, EDungeonRoomType::Generic, 11), *TS, FVector::ZeroVector);
		const int32 N = H1.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)].Num();
		TestTrue(TEXT("half density: some but not all of 20 faces"), N > 0 && N < 20);
		TestEqual(TEXT("half density: deterministic count"), N, H2.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)].Num());
		for (int32 i = 0; i < N; ++i)
		{
			TestTrue(TEXT("half density: deterministic placement"), H1.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)][i].Equals(H2.Transforms[static_cast<int32>(EDungeonTileType::WallDecor)][i], 0.001f));
		}
	}

	TS->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
