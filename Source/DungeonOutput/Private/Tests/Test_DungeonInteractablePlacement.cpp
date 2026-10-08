// Test_DungeonInteractablePlacement.cpp — Environment E3: the mapper's openings (door leaves hang
// from Doorway openings only), wall-fixture placement from the tileset's rules, and the stable
// interactable ids the per-dungeon state record is keyed by.
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "DungeonDoorActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonInteractableTestHelpers
{
	UDungeonTileSet* MakeInteractableTileSet()
	{
		UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
		TS->AddToRoot();
		return TS;
	}

	/** 11x5x1: room A (1..3,1..3), Door (4,2), hallway (5..7,2), everything around the room RoomWall. */
	FDungeonResult MakeDoorway()
	{
		FDungeonResult R;
		R.GridSize = FIntVector(11, 5, 1); R.CellWorldSize = 400.0f; R.Grid.Initialize(R.GridSize);
		for (int32 Y = 0; Y < 5; ++Y) for (int32 X = 0; X < 5; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1; }
		for (int32 Y = 1; Y <= 3; ++Y) for (int32 X = 1; X <= 3; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::Room; C.RoomIndex = 1; }
		{ FDungeonCell& C = R.Grid.GetCell(4, 2, 0); C.CellType = EDungeonCellType::Door; C.RoomIndex = 1; }
		for (int32 X = 5; X <= 7; ++X) { FDungeonCell& C = R.Grid.GetCell(X, 2, 0); C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; C.RoomIndex = 0; }
		R.EntranceRoomIndex = -1;
		return R;
	}

	/** 5x5x1: 3x3 room at (1,1) ringed by RoomWall. */
	FDungeonResult MakeInteractableRoom()
	{
		FDungeonResult R;
		R.GridSize = FIntVector(5, 5, 1); R.CellWorldSize = 400.0f; R.Grid.Initialize(R.GridSize);
		for (int32 Y = 0; Y < 5; ++Y) for (int32 X = 0; X < 5; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1; }
		for (int32 Y = 1; Y <= 3; ++Y) for (int32 X = 1; X <= 3; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::Room; C.RoomIndex = 1; }
		R.EntranceRoomIndex = -1;
		return R;
	}

	int32 CountFixturesAt(const FDungeonTileMapResult& Map, const FVector& P)
	{
		int32 N = 0;
		for (const FDungeonFixture& F : Map.Fixtures)
		{
			if (F.Anchor.GetLocation().Equals(P, 1.0f)) { ++N; }
		}
		return N;
	}
}

using namespace DungeonInteractableTestHelpers;

#define INTERACTABLE_TEST(ClassName, PrettyName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, PrettyName, EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Openings: one Doorway per framed Door face with the frame on the face plane at floor level and
// the hinge on the finished-face plane at the -Y jamb; Entrance frames are EntranceOpening.
// ---------------------------------------------------------------------------
INTERACTABLE_TEST(FInteractableOpenings, "Dungeon.Interactables.OpeningsFromFrames")
bool FInteractableOpenings::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeInteractableTileSet();
	TS->FixtureRules.MaxWallLights = 0;

	// Doorway: the Door cell (4,2) frames its +X face toward the hallway; the room side is open.
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("doorway: exactly one opening"), Map.Openings.Num(), 1);
		if (Map.Openings.Num() == 1)
		{
			const FDungeonOpening& O = Map.Openings[0];
			TestTrue(TEXT("doorway: kind"), O.Kind == EDungeonOpeningKind::Doorway);
			TestEqual(TEXT("doorway: cell"), O.Cell, FIntVector(4, 2, 0));
			TestEqual(TEXT("doorway: face +X"), O.FaceDX, 1);
			TestEqual(TEXT("doorway: face y"), O.FaceDY, 0);
			// Frame: face centre (2000, 1000) on the floor, +X into the hallway, scale 1 at 400/400.
			TestTrue(TEXT("doorway: frame at the face centre on the floor"), O.Frame.GetLocation().Equals(FVector(2000, 1000, 0), 0.5f));
			TestTrue(TEXT("doorway: frame +X into the neighbour"), O.Frame.GetUnitAxis(EAxis::X).Equals(FVector(1, 0, 0), 0.001f));
			TestTrue(TEXT("doorway: unit scale"), O.Frame.GetScale3D().Equals(FVector(1.0f), 0.001f));
			// Leaf: profile defaults 151 x 289; hinge 40 in from the plane (FaceInset), half a leaf
			// toward -Y: (1960, 1000 - 75.5, 0); local +Y toward the opening centre.
			TestTrue(TEXT("doorway: leaf width"), FMath::IsNearlyEqual(O.LeafWidth, 151.0f, 0.01f));
			TestTrue(TEXT("doorway: leaf height"), FMath::IsNearlyEqual(O.LeafHeight, 289.0f, 0.01f));
			TestTrue(TEXT("doorway: hinge on the finished-face plane at the -Y jamb"), O.LeafHinge.GetLocation().Equals(FVector(1960, 924.5, 0), 0.5f));
			TestTrue(TEXT("doorway: hinge +Y along the wall toward the centre"), O.LeafHinge.GetUnitAxis(EAxis::Y).Equals(FVector(0, 1, 0), 0.001f));
			// The id is a pure function of (cell, face, kind).
			const uint32 Id = FDungeonTileMapper::MakeInteractableId(O.Cell, O.FaceDX, O.FaceDY, FDungeonTileMapper::InteractableKindDoor);
			TestEqual(TEXT("id: stable"), Id, FDungeonTileMapper::MakeInteractableId(FIntVector(4, 2, 0), 1, 0, FDungeonTileMapper::InteractableKindDoor));
			TestNotEqual(TEXT("id: differs by face"), Id, FDungeonTileMapper::MakeInteractableId(FIntVector(4, 2, 0), -1, 0, FDungeonTileMapper::InteractableKindDoor));
			TestNotEqual(TEXT("id: differs by kind namespace"), Id, FDungeonTileMapper::MakeInteractableId(FIntVector(4, 2, 0), 1, 0, FDungeonTileMapper::InteractableKindFixture));
			TestNotEqual(TEXT("id: differs by cell"), Id, FDungeonTileMapper::MakeInteractableId(FIntVector(5, 2, 0), 1, 0, FDungeonTileMapper::InteractableKindDoor));
		}
	}

	// A cell-scaled profile: at cell 800 with ReferenceCellSize 400 everything doubles.
	{
		FDungeonResult R = MakeDoorway();
		R.CellWorldSize = 800.0f;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		TestEqual(TEXT("scaled: one opening"), Map.Openings.Num(), 1);
		if (Map.Openings.Num() == 1)
		{
			const FDungeonOpening& O = Map.Openings[0];
			TestTrue(TEXT("scaled: leaf width doubles"), FMath::IsNearlyEqual(O.LeafWidth, 302.0f, 0.01f));
			TestTrue(TEXT("scaled: frame scale 2"), O.Frame.GetScale3D().Equals(FVector(2.0f), 0.001f));
			TestTrue(TEXT("scaled: hinge 80 in, 151 over"), O.LeafHinge.GetLocation().Equals(FVector(3920, 1849, 0), 0.5f));
		}
	}

	// Entrance cell instead of a Door cell: the same frame site, EntranceOpening kind (no leaf).
	{
		FDungeonResult R = MakeDoorway();
		R.Grid.GetCell(4, 2, 0).CellType = EDungeonCellType::Entrance;
		R.EntranceRoomIndex = 1; R.EntranceCell = FIntVector(4, 2, 0);
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		TestEqual(TEXT("entrance: one opening"), Map.Openings.Num(), 1);
		if (Map.Openings.Num() == 1)
		{
			TestTrue(TEXT("entrance: kind"), Map.Openings[0].Kind == EDungeonOpeningKind::EntranceOpening);
		}
	}

	// No DoorFrame slot: the opening is still reported (gameplay does not depend on the frame mesh).
	{
		TS->Slots.Remove(EDungeonTileType::DoorFrame);
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("no frame slot: opening still emitted"), Map.Openings.Num(), 1);
		TestEqual(TEXT("no frame slot: no frame instance"), Map.Transforms[static_cast<int32>(EDungeonTileType::DoorFrame)].Num(), 0);
	}

	TS->RemoveFromRoot();
	return true;
}

// ---------------------------------------------------------------------------
// Fixtures: doorway lights on the Door cell's two side walls toward the room; every Nth
// rock-backed room wall face; cap and switches honoured; anchors on the finished face.
// ---------------------------------------------------------------------------
INTERACTABLE_TEST(FInteractableFixtures, "Dungeon.Interactables.WallLightPlacement")
bool FInteractableFixtures::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeInteractableTileSet();

	// Doorway only (room walls off): the Door cell (4,2), centre (1800,1000), opening +X. Side
	// walls -Y (face y=800) and +Y (face y=1200), finished faces 40 in, mount height 190, and the
	// lights sit 0.25 * 400 = 100 toward the room (-X): (1700, 840, 190) and (1700, 1160, 190).
	{
		TS->FixtureRules.RoomWallLightEvery = 0;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("doorway: two lights"), Map.Fixtures.Num(), 2);
		TestEqual(TEXT("doorway: -Y jamb light"), CountFixturesAt(Map, FVector(1700, 840, 190)), 1);
		TestEqual(TEXT("doorway: +Y jamb light"), CountFixturesAt(Map, FVector(1700, 1160, 190)), 1);
		for (const FDungeonFixture& F : Map.Fixtures)
		{
			TestTrue(TEXT("doorway: kind"), F.Kind == EDungeonFixtureKind::WallLight);
			TestEqual(TEXT("doorway: on the door cell"), F.Cell, FIntVector(4, 2, 0));
			// +X points off the wall into the cell: +Y for the -Y wall, -Y for the +Y wall.
			const FVector Expected(0.0f, F.FaceDY < 0 ? 1.0f : -1.0f, 0.0f);
			TestTrue(TEXT("doorway: +X off the wall"), F.Anchor.GetUnitAxis(EAxis::X).Equals(Expected, 0.001f));
		}
		// Switches: doorway lights off -> none; cap 1 -> one; cap 0 -> none at all.
		TS->FixtureRules.bLightDoorways = false;
		TestEqual(TEXT("doorway off: none"), FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector).Fixtures.Num(), 0);
		TS->FixtureRules.bLightDoorways = true;
		TS->FixtureRules.MaxWallLights = 1;
		TestEqual(TEXT("cap 1: one"), FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector).Fixtures.Num(), 1);
		TS->FixtureRules.MaxWallLights = 0;
		TestEqual(TEXT("cap 0: none"), FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector).Fixtures.Num(), 0);
		TS->FixtureRules.MaxWallLights = 24;
	}

	// Room walls: the 3x3 room has 12 rock-backed faces; every 3rd -> 4 lights, on the finished
	// face (40 in) at height 190; every 1st -> 12; a cap of 5 stops at 5; every 0 -> none.
	{
		TS->FixtureRules.RoomWallLightEvery = 3;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeInteractableRoom(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("room: every 3rd of 12 -> 4"), Map.Fixtures.Num(), 4);
		for (const FDungeonFixture& F : Map.Fixtures)
		{
			const FVector P = F.Anchor.GetLocation();
			TestTrue(TEXT("room: mount height"), FMath::IsNearlyEqual(P.Z, 190.0f, 0.5f));
			// On one of the four finished face planes: x = 440 / 1560 or y = 440 / 1560.
			const bool bOnFace = FMath::IsNearlyEqual(P.X, 440.0f, 0.5f) || FMath::IsNearlyEqual(P.X, 1560.0f, 0.5f)
				|| FMath::IsNearlyEqual(P.Y, 440.0f, 0.5f) || FMath::IsNearlyEqual(P.Y, 1560.0f, 0.5f);
			TestTrue(TEXT("room: anchor on a finished face"), bOnFace);
		}
		TS->FixtureRules.RoomWallLightEvery = 1;
		TestEqual(TEXT("room: every face -> 12"), FDungeonTileMapper::MapToTiles(MakeInteractableRoom(), *TS, FVector::ZeroVector).Fixtures.Num(), 12);
		TS->FixtureRules.MaxWallLights = 5;
		TestEqual(TEXT("room: cap 5"), FDungeonTileMapper::MapToTiles(MakeInteractableRoom(), *TS, FVector::ZeroVector).Fixtures.Num(), 5);
		TS->FixtureRules.MaxWallLights = 24;
		TS->FixtureRules.RoomWallLightEvery = 0;
		TestEqual(TEXT("room: every 0 -> none"), FDungeonTileMapper::MapToTiles(MakeInteractableRoom(), *TS, FVector::ZeroVector).Fixtures.Num(), 0);
	}

	// Determinism: the same layout maps to the identical fixture list twice.
	{
		TS->FixtureRules.RoomWallLightEvery = 3;
		const FDungeonTileMapResult A = FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector);
		const FDungeonTileMapResult B = FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("deterministic: same count"), A.Fixtures.Num(), B.Fixtures.Num());
		for (int32 i = 0; i < FMath::Min(A.Fixtures.Num(), B.Fixtures.Num()); ++i)
		{
			TestTrue(TEXT("deterministic: same anchor"), A.Fixtures[i].Anchor.GetLocation().Equals(B.Fixtures[i].Anchor.GetLocation(), 0.01f));
			TestEqual(TEXT("deterministic: same id"),
				FDungeonTileMapper::MakeInteractableId(A.Fixtures[i].Cell, A.Fixtures[i].FaceDX, A.Fixtures[i].FaceDY, FDungeonTileMapper::InteractableKindFixture),
				FDungeonTileMapper::MakeInteractableId(B.Fixtures[i].Cell, B.Fixtures[i].FaceDX, B.Fixtures[i].FaceDY, FDungeonTileMapper::InteractableKindFixture));
		}
		// Doorway lights come first, so the cap always keeps them.
		TestTrue(TEXT("deterministic: doorway lights first"), A.Fixtures.Num() >= 2 && A.Fixtures[0].Cell == FIntVector(4, 2, 0) && A.Fixtures[1].Cell == FIntVector(4, 2, 0));
	}

	TS->RemoveFromRoot();
	return true;
}

// The plugin's door actor fits a leaf mesh to an opening by scaling the mesh BOUNDS to the
// profile's leaf size about the hinge origin: a leaf authored to the profile numbers fits 1:1,
// a differently sized one is stretched (and, since the base is not at the origin, moved).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractableLeafFit, "Dungeon.Interactables.LeafFit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInteractableLeafFit::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeInteractableTileSet();
	TS->WallProfile.DoorLeafWidth = 236.0f;
	TS->WallProfile.DoorLeafHeight = 232.0f;
	const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeDoorway(), *TS, FVector::ZeroVector);
	TestEqual(TEXT("one doorway"), Map.Openings.Num(), 1);
	if (Map.Openings.Num() != 1) { TS->RemoveFromRoot(); return false; }
	const FDungeonOpening& O = Map.Openings[0];

	// SM_DoorLeaf as authored: hinge at the origin, 236 along +Y, floor top 80 -> 312, 19 thick.
	const FBox Authored(FVector(-8.0f, 0.0f, 80.0f), FVector(11.0f, 236.0f, 312.0f));
	const FVector Fit = ADungeonDoorActor::ComputeLeafScale(O, Authored, FRotator::ZeroRotator);
	TestTrue(TEXT("authored to the profile: scale 1"), Fit.Equals(FVector(1.0f), 0.001f));

	// The pack's leaf (151 x 289) in the same opening is stretched to the profile size.
	const FBox PackLeaf(FVector(-5.0f, 0.0f, 0.0f), FVector(5.0f, 151.0f, 289.0f));
	const FVector Stretched = ADungeonDoorActor::ComputeLeafScale(O, PackLeaf, FRotator::ZeroRotator);
	TestTrue(TEXT("pack leaf: width 236/151"), FMath::IsNearlyEqual(Stretched.Y, 236.0f / 151.0f, 0.001f));
	TestTrue(TEXT("pack leaf: height 232/289"), FMath::IsNearlyEqual(Stretched.Z, 232.0f / 289.0f, 0.001f));
	TestTrue(TEXT("pack leaf: thickness keeps the cell scale"), FMath::IsNearlyEqual(Stretched.X, 1.0f, 0.001f));

	// A leaf authored along -Y with a yaw-180 offset (the pack convention) measures the same.
	const FBox AlongMinusY(FVector(-5.0f, -151.0f, 0.0f), FVector(5.0f, 0.0f, 289.0f));
	const FVector Offset = ADungeonDoorActor::ComputeLeafScale(O, AlongMinusY, FRotator(0.0f, 180.0f, 0.0f));
	TestTrue(TEXT("yaw-180 leaf: same fit"), Offset.Equals(Stretched, 0.001f));

	// Cell scale: at cell 800 the opening doubles, the authored leaf doubles with it.
	FDungeonResult Big = MakeDoorway();
	Big.CellWorldSize = 800.0f;
	const FDungeonTileMapResult BigMap = FDungeonTileMapper::MapToTiles(Big, *TS, FVector::ZeroVector);
	if (BigMap.Openings.Num() == 1)
	{
		const FVector BigFit = ADungeonDoorActor::ComputeLeafScale(BigMap.Openings[0], Authored, FRotator::ZeroRotator);
		TestTrue(TEXT("cell 800: scale 2"), BigFit.Equals(FVector(2.0f), 0.001f));
	}

	// No mesh: the cell scale alone.
	TestTrue(TEXT("no bounds: cell scale"), ADungeonDoorActor::ComputeLeafScale(O, FBox(ForceInit), FRotator::ZeroRotator).Equals(FVector(1.0f), 0.001f));

	TS->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
