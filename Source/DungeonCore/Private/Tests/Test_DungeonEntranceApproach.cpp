// Test_DungeonEntranceApproach.cpp — Approach-aware entrance placement: the entrance room is
// placed first to suit the declared approach and the approach volume stays clear of rooms,
// hallways and staircases through the whole pipeline.
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonSeed.h"
#include "DungeonValidator.h"
#include "RoomPlacement.h"

namespace DungeonEntranceApproachTestHelpers
{
	UDungeonConfiguration* CreateConfig(const FIntVector& GridSize, int32 RoomCount, const FIntVector& MaxRoomSize, int32 Buffer = 1)
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = GridSize;
		Config->RoomCount = RoomCount;
		Config->MinRoomSize = FIntVector(3, 3, 1);
		Config->MaxRoomSize = MaxRoomSize;
		Config->RoomBuffer = Buffer;
		Config->EntrancePlacement = EDungeonEntrancePlacement::Any;
		return Config;
	}

	void Cleanup(UDungeonConfiguration* Config, UDungeonGenerator* Generator)
	{
		if (Config) { Config->RemoveFromRoot(); }
		if (Generator) { Generator->RemoveFromRoot(); }
	}

	int32 CountReserved(const FDungeonResult& R)
	{
		int32 N = 0;
		for (const FDungeonCell& Cell : R.Grid.Cells)
		{
			N += (Cell.CellType == EDungeonCellType::Reserved) ? 1 : 0;
		}
		return N;
	}

	/** Every in-bounds keep-out cell is Empty. */
	bool KeepOutIsClear(const FDungeonResult& R, FIntVector& OutFirstBlocked)
	{
		const FDungeonEntranceApproachInfo& A = R.EntranceApproach;
		if (!A.HasKeepOut()) { return true; }
		for (int32 Z = A.KeepOutMin.Z; Z <= A.KeepOutMax.Z; ++Z)
			for (int32 Y = A.KeepOutMin.Y; Y <= A.KeepOutMax.Y; ++Y)
				for (int32 X = A.KeepOutMin.X; X <= A.KeepOutMax.X; ++X)
				{
					if (R.Grid.IsInBounds(X, Y, Z) && R.Grid.GetCell(X, Y, Z).CellType != EDungeonCellType::Empty)
					{
						OutFirstBlocked = FIntVector(X, Y, Z);
						return false;
					}
				}
		return true;
	}

	bool InRoom(const FDungeonRoom& Room, const FIntVector& C)
	{
		return C.X >= Room.Position.X && C.X < Room.Position.X + Room.Size.X
			&& C.Y >= Room.Position.Y && C.Y < Room.Position.Y + Room.Size.Y
			&& C.Z >= Room.Position.Z && C.Z < Room.Position.Z + Room.Size.Z;
	}

	int32 IssuesOfCategory(const FDungeonValidationResult& V, const TCHAR* Category)
	{
		int32 N = 0;
		for (const FDungeonValidationIssue& I : V.Issues) { N += (I.Category == Category) ? 1 : 0; }
		return N;
	}
}

using namespace DungeonEntranceApproachTestHelpers;

#define APPROACH_TEST(ClassName, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// FromAbove, entrance forced to the BOTTOM floor of a tall grid: the worst case for a shaft.
// Every cell of the column above the entrance must be Empty after hallway carving.
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachFromAboveBottom, "Dungeon.Generation.EntranceApproach.FromAboveBottomFloorColumnClear")
bool FApproachFromAboveBottom::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 5), 8, FIntVector(6, 6, 2));
	Config->Entrance.Approach = EDungeonEntranceApproach::FromAbove;
	Config->Entrance.Floor = EDungeonEntranceFloor::Bottom;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	int32 SeedsWithReservation = 0;
	for (int64 Seed = 1; Seed <= 40; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		const FString Tag = FString::Printf(TEXT("seed %lld: "), Seed);
		if (!TestTrue(Tag + TEXT("generated"), R.Rooms.Num() >= 2)) { continue; }

		const FDungeonEntranceApproachInfo& A = R.EntranceApproach;
		TestTrue(Tag + TEXT("satisfied"), A.bSatisfied);
		TestEqual(Tag + TEXT("approach recorded"), A.Approach, EDungeonEntranceApproach::FromAbove);
		TestEqual(Tag + TEXT("entrance is room 0"), R.EntranceRoomIndex, 0);
		const FDungeonRoom& Room = R.Rooms[0];
		TestEqual(Tag + TEXT("entrance type"), Room.RoomType, EDungeonRoomType::Entrance);
		TestEqual(Tag + TEXT("bottom floor"), Room.Position.Z, 0);
		TestEqual(Tag + TEXT("single floor"), Room.Size.Z, 1);
		TestTrue(Tag + TEXT("opening in room"), InRoom(Room, A.OpeningCell));
		TestEqual(Tag + TEXT("opening is the room's top cell"), A.OpeningCell.Z, Room.Position.Z + Room.Size.Z - 1);
		TestEqual(Tag + TEXT("opening column == entrance column"), FIntPoint(A.OpeningCell.X, A.OpeningCell.Y), FIntPoint(R.EntranceCell.X, R.EntranceCell.Y));
		TestTrue(Tag + TEXT("keep-out exists (4 floors above)"), A.HasKeepOut());
		TestEqual(Tag + TEXT("keep-out top is grid top"), A.KeepOutMax.Z, 4);
		TestEqual(Tag + TEXT("keep-out starts above the lid"), A.KeepOutMin.Z, A.OpeningCell.Z + 1);
		SeedsWithReservation += A.HasKeepOut() ? 1 : 0;

		FIntVector Blocked;
		TestTrue(Tag + FString::Printf(TEXT("column clear (blocked at %s)"), *Blocked.ToString()), KeepOutIsClear(R, Blocked));
		TestEqual(Tag + TEXT("no Reserved leaked"), CountReserved(R), 0);

		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}
	TestTrue(TEXT("reservation exercised"), SeedsWithReservation > 0);

	Cleanup(Config, Generator);
	return true;
}

// ---------------------------------------------------------------------------
// FromAbove on the TOP floor: nothing to reserve, the room reaches the grid top.
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachFromAboveTop, "Dungeon.Generation.EntranceApproach.FromAboveTopFloorNoKeepOut")
bool FApproachFromAboveTop::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 4), 8, FIntVector(6, 6, 2));
	Config->Entrance.Approach = EDungeonEntranceApproach::FromAbove;
	Config->Entrance.Floor = EDungeonEntranceFloor::Top;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	for (int64 Seed = 1; Seed <= 20; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		const FString Tag = FString::Printf(TEXT("seed %lld: "), Seed);
		if (!TestTrue(Tag + TEXT("generated"), R.Rooms.Num() >= 2)) { continue; }

		const FDungeonEntranceApproachInfo& A = R.EntranceApproach;
		TestTrue(Tag + TEXT("satisfied"), A.bSatisfied);
		const FDungeonRoom& Room = R.Rooms[R.EntranceRoomIndex];
		TestEqual(Tag + TEXT("room reaches the top floor"), Room.Position.Z + Room.Size.Z, 4);
		TestEqual(Tag + TEXT("opening on the top floor"), A.OpeningCell.Z, 3);
		TestFalse(Tag + TEXT("nothing to reserve"), A.HasKeepOut());
		TestEqual(Tag + TEXT("no Reserved leaked"), CountReserved(R), 0);

		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}

	Cleanup(Config, Generator);
	return true;
}

// ---------------------------------------------------------------------------
// bSingleFloorEntranceRoom: on by default for vertical approaches; off lets tall rooms through,
// and the opening is then the room's TOP cell with the keep-out above it.
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachSingleFloor, "Dungeon.Generation.EntranceApproach.SingleFloorEntranceRoomFlag")
bool FApproachSingleFloor::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 5), 6, FIntVector(6, 6, 2));
	Config->Entrance.Approach = EDungeonEntranceApproach::FromAbove;
	Config->Entrance.Floor = EDungeonEntranceFloor::Bottom;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	Config->Entrance.bSingleFloorEntranceRoom = true;
	for (int64 Seed = 1; Seed <= 15; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		if (R.Rooms.Num() < 2) { continue; }
		TestEqual(FString::Printf(TEXT("seed %lld: forced single floor"), Seed), R.Rooms[0].Size.Z, 1);
	}

	Config->Entrance.bSingleFloorEntranceRoom = false;
	int32 TallEntrances = 0;
	for (int64 Seed = 1; Seed <= 30; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		if (R.Rooms.Num() < 2) { continue; }
		const FString Tag = FString::Printf(TEXT("seed %lld: "), Seed);
		const FDungeonRoom& Room = R.Rooms[0];
		if (Room.Size.Z == 2)
		{
			++TallEntrances;
			TestEqual(Tag + TEXT("opening is the top cell of a tall room"), R.EntranceApproach.OpeningCell.Z, 1);
			TestEqual(Tag + TEXT("GetEntranceOpeningCell agrees"), R.GetEntranceOpeningCell(), R.EntranceApproach.OpeningCell);
			TestEqual(Tag + TEXT("keep-out starts at Z=2"), R.EntranceApproach.KeepOutMin.Z, 2);
		}
		FIntVector Blocked;
		TestTrue(Tag + TEXT("column clear"), KeepOutIsClear(R, Blocked));
		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}
	TestTrue(TEXT("some tall entrance rooms were generated (flag off)"), TallEntrances > 0);

	Cleanup(Config, Generator);
	return true;
}

// ---------------------------------------------------------------------------
// FromSide: the entrance room sits on the buffer line of the face and the corridor from its face
// to the grid edge is Empty. Explicit face, then Any (drawn from the seed).
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachFromSide, "Dungeon.Generation.EntranceApproach.FromSideCorridorClear")
bool FApproachFromSide::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 3), 8, FIntVector(6, 6, 1), /*Buffer=*/2);
	Config->Entrance.Approach = EDungeonEntranceApproach::FromSide;
	Config->Entrance.Face = EDungeonGridFace::MinX;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	for (int64 Seed = 1; Seed <= 30; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		const FString Tag = FString::Printf(TEXT("seed %lld: "), Seed);
		if (!TestTrue(Tag + TEXT("generated"), R.Rooms.Num() >= 2)) { continue; }

		const FDungeonEntranceApproachInfo& A = R.EntranceApproach;
		TestTrue(Tag + TEXT("satisfied"), A.bSatisfied);
		TestEqual(Tag + TEXT("face"), A.Face, EDungeonGridFace::MinX);
		const FDungeonRoom& Room = R.Rooms[0];
		TestEqual(Tag + TEXT("room on the MinX buffer line"), Room.Position.X, 2);
		TestEqual(Tag + TEXT("opening on the room's MinX face"), A.OpeningCell, FIntVector(2, R.EntranceCell.Y, R.EntranceCell.Z));
		TestTrue(Tag + TEXT("corridor reserved"), A.HasKeepOut());
		TestEqual(Tag + TEXT("corridor from the grid edge"), A.KeepOutMin, FIntVector(0, R.EntranceCell.Y, R.EntranceCell.Z));
		TestEqual(Tag + TEXT("corridor to the room face"), A.KeepOutMax, FIntVector(1, R.EntranceCell.Y, R.EntranceCell.Z));
		FIntVector Blocked;
		TestTrue(Tag + TEXT("corridor clear"), KeepOutIsClear(R, Blocked));
		TestEqual(Tag + TEXT("no Reserved leaked"), CountReserved(R), 0);

		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}

	// Any face: resolved from the seed, room on that face's buffer line.
	Config->Entrance.Face = EDungeonGridFace::Any;
	TSet<EDungeonGridFace> FacesSeen;
	for (int64 Seed = 1; Seed <= 24; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		const FString Tag = FString::Printf(TEXT("any-face seed %lld: "), Seed);
		if (R.Rooms.Num() < 2) { continue; }
		const FDungeonEntranceApproachInfo& A = R.EntranceApproach;
		TestTrue(Tag + TEXT("satisfied"), A.bSatisfied);
		TestNotEqual(Tag + TEXT("face resolved"), A.Face, EDungeonGridFace::Any);
		FacesSeen.Add(A.Face);
		const FDungeonRoom& Room = R.Rooms[0];
		bool bOnLine = false;
		switch (A.Face)
		{
		case EDungeonGridFace::MinX: bOnLine = Room.Position.X == 2; break;
		case EDungeonGridFace::MaxX: bOnLine = Room.Position.X + Room.Size.X == 18; break;
		case EDungeonGridFace::MinY: bOnLine = Room.Position.Y == 2; break;
		case EDungeonGridFace::MaxY: bOnLine = Room.Position.Y + Room.Size.Y == 18; break;
		default: break;
		}
		TestTrue(Tag + TEXT("room on the resolved face's buffer line"), bOnLine);
		FIntVector Blocked;
		TestTrue(Tag + TEXT("corridor clear"), KeepOutIsClear(R, Blocked));
		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}
	TestTrue(TEXT("Any draws more than one face across seeds"), FacesSeen.Num() > 1);

	Cleanup(Config, Generator);
	return true;
}

// ---------------------------------------------------------------------------
// FromBelow with the entrance on the TOP floor: the column beneath stays Empty.
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachFromBelow, "Dungeon.Generation.EntranceApproach.FromBelowColumnClear")
bool FApproachFromBelow::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 5), 8, FIntVector(6, 6, 2));
	Config->Entrance.Approach = EDungeonEntranceApproach::FromBelow;
	Config->Entrance.Floor = EDungeonEntranceFloor::Top;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	for (int64 Seed = 1; Seed <= 30; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		const FString Tag = FString::Printf(TEXT("seed %lld: "), Seed);
		if (!TestTrue(Tag + TEXT("generated"), R.Rooms.Num() >= 2)) { continue; }

		const FDungeonEntranceApproachInfo& A = R.EntranceApproach;
		TestTrue(Tag + TEXT("satisfied"), A.bSatisfied);
		const FDungeonRoom& Room = R.Rooms[0];
		TestEqual(Tag + TEXT("top floor (single-floor room)"), Room.Position.Z, 4);
		TestEqual(Tag + TEXT("opening is the room's floor cell"), A.OpeningCell, R.EntranceCell);
		TestTrue(Tag + TEXT("keep-out exists"), A.HasKeepOut());
		TestEqual(Tag + TEXT("keep-out from floor 0"), A.KeepOutMin.Z, 0);
		TestEqual(Tag + TEXT("keep-out to just under the room"), A.KeepOutMax.Z, 3);
		FIntVector Blocked;
		TestTrue(Tag + TEXT("column clear"), KeepOutIsClear(R, Blocked));

		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}

	Cleanup(Config, Generator);
	return true;
}

// ---------------------------------------------------------------------------
// Unsatisfiable request (explicit floor outside the grid): generation still succeeds via the
// legacy path, the result says so, and the validator reports exactly that.
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachFallback, "Dungeon.Generation.EntranceApproach.UnsatisfiableFallsBackAndReports")
bool FApproachFallback::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 3), 6, FIntVector(6, 6, 1));
	Config->Entrance.Approach = EDungeonEntranceApproach::FromAbove;
	Config->Entrance.Floor = EDungeonEntranceFloor::Explicit;
	Config->Entrance.ExplicitFloor = 7;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	// (The generator logs the fallback at Warning; warnings do not fail automation tests.)
	const FDungeonResult R = Generator->Generate(Config, 99);
	TestTrue(TEXT("generated"), R.Rooms.Num() >= 2);
	TestTrue(TEXT("has an entrance"), R.EntranceRoomIndex >= 0);
	TestEqual(TEXT("approach recorded"), R.EntranceApproach.Approach, EDungeonEntranceApproach::FromAbove);
	TestFalse(TEXT("not satisfied"), R.EntranceApproach.bSatisfied);
	TestFalse(TEXT("nothing reserved"), R.EntranceApproach.HasKeepOut());
	TestEqual(TEXT("no Reserved leaked"), CountReserved(R), 0);

	const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
	TestFalse(TEXT("validator fails"), V.bPassed);
	TestEqual(TEXT("exactly one EntranceApproach issue"), IssuesOfCategory(V, TEXT("EntranceApproach")), 1);
	TestEqual(TEXT("no other issues"), V.Issues.Num(), 1);

	Cleanup(Config, Generator);
	return true;
}

// ---------------------------------------------------------------------------
// Approach None: the legacy path, untouched — nothing recorded, nothing reserved.
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachNone, "Dungeon.Generation.EntranceApproach.NoneIsLegacyPath")
bool FApproachNone::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 3), 6, FIntVector(6, 6, 2));
	Config->EntrancePlacement = EDungeonEntrancePlacement::TopFloor;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	for (int64 Seed = 1; Seed <= 10; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		const FString Tag = FString::Printf(TEXT("seed %lld: "), Seed);
		if (R.Rooms.Num() < 2) { continue; }
		TestEqual(Tag + TEXT("approach None"), R.EntranceApproach.Approach, EDungeonEntranceApproach::None);
		TestFalse(Tag + TEXT("not satisfied"), R.EntranceApproach.bSatisfied);
		TestFalse(Tag + TEXT("no keep-out"), R.EntranceApproach.HasKeepOut());
		TestEqual(Tag + TEXT("no Reserved leaked"), CountReserved(R), 0);
		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}

	Cleanup(Config, Generator);
	return true;
}

// ---------------------------------------------------------------------------
// Room placement alone keeps out of Reserved cells.
// ---------------------------------------------------------------------------
APPROACH_TEST(FPlacementAvoidsReserved, "Dungeon.RoomPlacement.RoomsAvoidReservedCells")
bool FPlacementAvoidsReserved::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 3), 12, FIntVector(5, 5, 2));

	FDungeonGrid Grid;
	Grid.Initialize(Config->GridSize);
	const FIntVector Min(5, 5, 0), Max(14, 14, 2);
	FRoomPlacement::FillEmptyBox(Grid, Min, Max, EDungeonCellType::Reserved);

	TArray<FDungeonRoom> Rooms;
	FDungeonSeed Seed(7);
	TestTrue(TEXT("placed >= 2 rooms around the box"), FRoomPlacement::PlaceRooms(Grid, *Config, Seed, Rooms));
	for (const FDungeonRoom& Room : Rooms)
	{
		const bool bIntersects =
			Room.Position.X <= Max.X && Room.Position.X + Room.Size.X - 1 >= Min.X &&
			Room.Position.Y <= Max.Y && Room.Position.Y + Room.Size.Y - 1 >= Min.Y;
		TestFalse(FString::Printf(TEXT("room %d at (%d,%d) size (%d,%d) avoids the reserved box"),
			Room.RoomIndex, Room.Position.X, Room.Position.Y, Room.Size.X, Room.Size.Y), bIntersects);
	}
	// The box itself is intact: the stamp never overwrote a Reserved cell.
	int32 Reserved = 0;
	for (const FDungeonCell& Cell : Grid.Cells) { Reserved += (Cell.CellType == EDungeonCellType::Reserved) ? 1 : 0; }
	TestEqual(TEXT("reserved box intact"), Reserved, 10 * 10 * 3);

	Config->RemoveFromRoot();
	return true;
}

// ---------------------------------------------------------------------------
// GenerateWithEntrance: the override replaces the config's spec in both directions.
// ---------------------------------------------------------------------------
APPROACH_TEST(FApproachOverride, "Dungeon.Generation.EntranceApproach.GenerateWithEntranceOverridesConfig")
bool FApproachOverride::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = CreateConfig(FIntVector(20, 20, 4), 6, FIntVector(6, 6, 1));
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	// Config says None; override asks for FromAbove/Top.
	FDungeonEntranceSpec Override;
	Override.Approach = EDungeonEntranceApproach::FromAbove;
	Override.Floor = EDungeonEntranceFloor::Top;
	for (int64 Seed = 1; Seed <= 10; ++Seed)
	{
		const FDungeonResult R = Generator->GenerateWithEntrance(Config, Seed, Override);
		const FString Tag = FString::Printf(TEXT("seed %lld: "), Seed);
		if (R.Rooms.Num() < 2) { continue; }
		TestEqual(Tag + TEXT("override approach honoured"), R.EntranceApproach.Approach, EDungeonEntranceApproach::FromAbove);
		TestTrue(Tag + TEXT("satisfied"), R.EntranceApproach.bSatisfied);
		TestEqual(Tag + TEXT("top floor"), R.Rooms[0].Position.Z + R.Rooms[0].Size.Z, 4);
		const FDungeonValidationResult V = FDungeonValidator::ValidateAll(R, *Config);
		TestTrue(Tag + TEXT("validator: ") + V.GetSummary(), V.bPassed);
	}

	// Config says FromAbove; an override of None runs the legacy path.
	Config->Entrance.Approach = EDungeonEntranceApproach::FromAbove;
	const FDungeonEntranceSpec NoneSpec;
	const FDungeonResult Legacy = Generator->GenerateWithEntrance(Config, 5, NoneSpec);
	TestEqual(TEXT("override None -> legacy"), Legacy.EntranceApproach.Approach, EDungeonEntranceApproach::None);
	TestEqual(TEXT("no Reserved leaked"), CountReserved(Legacy), 0);

	// And Generate() itself still reads the config.
	const FDungeonResult FromConfig = Generator->Generate(Config, 5);
	TestEqual(TEXT("Generate uses Config->Entrance"), FromConfig.EntranceApproach.Approach, EDungeonEntranceApproach::FromAbove);

	Cleanup(Config, Generator);
	return true;
}
