// Test_DungeonBoundaryRules.cpp — One test per boundary rule, on hand-built two-cell grids.
//
// Every case here encodes a rule that previously drifted between the tile mapper and the voxel
// stamper. If a rule needs to change, change it in FDungeonBoundaryRules and update the matching
// test; never re-implement the predicate in a backend.
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonBoundaryRules.h"

namespace DungeonBoundaryRulesTestHelpers
{
	/** A 3x3x3 grid, all Empty, with (1,1,1) as the "current" cell under test. */
	struct FPair
	{
		FDungeonGrid Grid;
		static constexpr int32 CX = 1, CY = 1, CZ = 1;

		FPair()
		{
			Grid.Initialize(FIntVector(3, 3, 3));
		}

		FDungeonCell& Current() { return Grid.GetCell(CX, CY, CZ); }

		/** Set the current cell. */
		FPair& Cur(EDungeonCellType Type, uint8 RoomIndex = 0, uint8 HallwayIndex = 0)
		{
			FDungeonCell& C = Current();
			C.CellType = Type; C.RoomIndex = RoomIndex; C.HallwayIndex = HallwayIndex;
			return *this;
		}

		/** Set the +X neighbour (horizontal); used for NeedsWall cases. */
		FPair& Side(EDungeonCellType Type, uint8 RoomIndex = 0, uint8 HallwayIndex = 0)
		{
			FDungeonCell& C = Grid.GetCell(CX + 1, CY, CZ);
			C.CellType = Type; C.RoomIndex = RoomIndex; C.HallwayIndex = HallwayIndex;
			return *this;
		}

		/** Set the +Z neighbour (vertical); used for NeedsVerticalBoundary cases. */
		FPair& Above(EDungeonCellType Type, uint8 RoomIndex = 0, uint8 HallwayIndex = 0)
		{
			FDungeonCell& C = Grid.GetCell(CX, CY, CZ + 1);
			C.CellType = Type; C.RoomIndex = RoomIndex; C.HallwayIndex = HallwayIndex;
			return *this;
		}

		bool Wall() const { return FDungeonBoundaryRules::NeedsWall(Grid, FIntVector(CX, CY, CZ), CX + 1, CY, CZ); }
		bool Ceiling() const { return FDungeonBoundaryRules::NeedsVerticalBoundary(Grid, FIntVector(CX, CY, CZ), CX, CY, CZ + 1); }
	};
}

using namespace DungeonBoundaryRulesTestHelpers;
using ECT = EDungeonCellType;

#define BOUNDARY_TEST(ClassName, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Rule 1: out-of-bounds and solid neighbours
// ---------------------------------------------------------------------------

BOUNDARY_TEST(FBoundaryOOB, "Dungeon.BoundaryRules.Wall.OutOfBoundsIsWall")
bool FBoundaryOOB::RunTest(const FString& Parameters)
{
	FPair P; P.Cur(ECT::Room, 1);
	TestTrue(TEXT("+X OOB"), FDungeonBoundaryRules::NeedsWall(P.Grid, FIntVector(1, 1, 1), 3, 1, 1));
	TestTrue(TEXT("-X OOB"), FDungeonBoundaryRules::NeedsWall(P.Grid, FIntVector(1, 1, 1), -1, 1, 1));
	TestTrue(TEXT("+Z OOB"), FDungeonBoundaryRules::NeedsVerticalBoundary(P.Grid, FIntVector(1, 1, 1), 1, 1, 3));
	return true;
}

BOUNDARY_TEST(FBoundarySolid, "Dungeon.BoundaryRules.Wall.SolidNeighborIsWall")
bool FBoundarySolid::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Room->Empty"), FPair().Cur(ECT::Room, 1).Side(ECT::Empty).Wall());
	TestTrue(TEXT("Room->RoomWall"), FPair().Cur(ECT::Room, 1).Side(ECT::RoomWall, 1).Wall());
	TestTrue(TEXT("Hallway->Empty"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::Empty).Wall());
	TestTrue(TEXT("Room above Empty"), FPair().Cur(ECT::Room, 1).Above(ECT::Empty).Ceiling());
	TestFalse(TEXT("Empty is not open"), FDungeonBoundaryRules::IsOpenCell(ECT::Empty));
	TestFalse(TEXT("RoomWall is not open"), FDungeonBoundaryRules::IsOpenCell(ECT::RoomWall));
	TestTrue(TEXT("Door is open"), FDungeonBoundaryRules::IsOpenCell(ECT::Door));
	return true;
}

// ---------------------------------------------------------------------------
// Rules 2 + 3: doorways
// ---------------------------------------------------------------------------

BOUNDARY_TEST(FBoundaryDoorNeighbor, "Dungeon.BoundaryRules.Wall.DoorNeighborIsOpen")
bool FBoundaryDoorNeighbor::RunTest(const FString& Parameters)
{
	// The Door owns its frame: nobody walls it from the outside, whatever space they belong to.
	TestFalse(TEXT("Room->Door same room"), FPair().Cur(ECT::Room, 1).Side(ECT::Door, 1).Wall());
	TestFalse(TEXT("Room->Door other room"), FPair().Cur(ECT::Room, 1).Side(ECT::Door, 2).Wall());
	TestFalse(TEXT("Hallway->Door"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::Door, 2).Wall());
	TestFalse(TEXT("Hallway->Entrance"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::Entrance, 2).Wall());
	return true;
}

BOUNDARY_TEST(FBoundaryDoorway, "Dungeon.BoundaryRules.Wall.DoorOpensTowardHallway")
bool FBoundaryDoorway::RunTest(const FString& Parameters)
{
	// The doorway itself: the rule the mapper was missing (every doorway had a wall tile across it).
	TestFalse(TEXT("Door->Hallway"), FPair().Cur(ECT::Door, 1).Side(ECT::Hallway, 0, 3).Wall());
	TestFalse(TEXT("Door->Staircase"), FPair().Cur(ECT::Door, 1).Side(ECT::Staircase, 0, 3).Wall());
	TestFalse(TEXT("Door->StaircaseHead"), FPair().Cur(ECT::Door, 1).Side(ECT::StaircaseHead, 0, 3).Wall());
	TestFalse(TEXT("Entrance->Hallway"), FPair().Cur(ECT::Entrance, 1).Side(ECT::Hallway, 0, 3).Wall());
	// A door facing a DIFFERENT room directly (no hallway) is still a wall; only hallways get the opening.
	TestTrue(TEXT("Door->other Room"), FPair().Cur(ECT::Door, 1).Side(ECT::Room, 2).Wall());
	// Vertical variant: a doorway dropping straight onto a staircase is open.
	TestFalse(TEXT("Door above Staircase"), FPair().Cur(ECT::Door, 1).Above(ECT::Staircase, 0, 3).Ceiling());
	return true;
}

// ---------------------------------------------------------------------------
// Rule 4: rooms
// ---------------------------------------------------------------------------

BOUNDARY_TEST(FBoundaryRooms, "Dungeon.BoundaryRules.Wall.RoomsByIndex")
bool FBoundaryRooms::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Same room"), FPair().Cur(ECT::Room, 1).Side(ECT::Room, 1).Wall());
	TestTrue(TEXT("Different rooms"), FPair().Cur(ECT::Room, 1).Side(ECT::Room, 2).Wall());
	TestTrue(TEXT("Room->Hallway (no door)"), FPair().Cur(ECT::Room, 1).Side(ECT::Hallway, 0, 1).Wall());
	TestTrue(TEXT("Hallway->Room (no door)"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::Room, 1).Wall());
	// Vertical: multi-floor room interior is open; stacked different rooms are not.
	TestFalse(TEXT("Same room stacked"), FPair().Cur(ECT::Room, 1).Above(ECT::Room, 1).Ceiling());
	TestTrue(TEXT("Different rooms stacked"), FPair().Cur(ECT::Room, 1).Above(ECT::Room, 2).Ceiling());
	return true;
}

// ---------------------------------------------------------------------------
// Rule 5 (horizontal): hallway family merging + StaircaseHead restriction
// ---------------------------------------------------------------------------

BOUNDARY_TEST(FBoundaryHallwaysMerge, "Dungeon.BoundaryRules.Wall.HallwaysMerge")
bool FBoundaryHallwaysMerge::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Hallway->Hallway same"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::Hallway, 0, 1).Wall());
	TestFalse(TEXT("Hallway->Hallway different"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::Hallway, 0, 2).Wall());
	TestFalse(TEXT("Hallway->Staircase different"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::Staircase, 0, 2).Wall());
	TestFalse(TEXT("Staircase->Staircase same"), FPair().Cur(ECT::Staircase, 0, 1).Side(ECT::Staircase, 0, 1).Wall());
	return true;
}

BOUNDARY_TEST(FBoundaryStaircaseHead, "Dungeon.BoundaryRules.Wall.StaircaseHeadOpensToOwnHallway")
bool FBoundaryStaircaseHead::RunTest(const FString& Parameters)
{
	// The rule the mapper was missing: the head must open into the plain Hallway it exits into.
	TestFalse(TEXT("Head->Hallway same index"), FPair().Cur(ECT::StaircaseHead, 0, 1).Side(ECT::Hallway, 0, 1).Wall());
	TestFalse(TEXT("Hallway->Head same index"), FPair().Cur(ECT::Hallway, 0, 1).Side(ECT::StaircaseHead, 0, 1).Wall());
	TestFalse(TEXT("Head->Staircase same index"), FPair().Cur(ECT::StaircaseHead, 0, 1).Side(ECT::Staircase, 0, 1).Wall());
	// ...but not into some other hallway that happens to pass by.
	TestTrue(TEXT("Head->Hallway other index"), FPair().Cur(ECT::StaircaseHead, 0, 1).Side(ECT::Hallway, 0, 2).Wall());
	TestTrue(TEXT("Hallway->Head other index"), FPair().Cur(ECT::Hallway, 0, 2).Side(ECT::StaircaseHead, 0, 1).Wall());
	return true;
}

// ---------------------------------------------------------------------------
// Rule 5 (vertical): only the staircase shaft is open; stacked flat hallways keep their floors
// ---------------------------------------------------------------------------

BOUNDARY_TEST(FBoundaryStackedHallways, "Dungeon.BoundaryRules.Vertical.StackedFlatHallwaysKeepFloor")
bool FBoundaryStackedHallways::RunTest(const FString& Parameters)
{
	// The floor-hole bug: two flat Hallway cells of the same hallway at different Z are separate
	// walkable levels and MUST keep their floor/ceiling.
	TestTrue(TEXT("Hallway over Hallway same index"), FPair().Cur(ECT::Hallway, 0, 1).Above(ECT::Hallway, 0, 1).Ceiling());
	TestTrue(TEXT("Hallway over Hallway other index"), FPair().Cur(ECT::Hallway, 0, 1).Above(ECT::Hallway, 0, 2).Ceiling());
	return true;
}

BOUNDARY_TEST(FBoundaryShaft, "Dungeon.BoundaryRules.Vertical.StaircaseShaftIsOpen")
bool FBoundaryShaft::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Staircase under Hallway same"), FPair().Cur(ECT::Staircase, 0, 1).Above(ECT::Hallway, 0, 1).Ceiling());
	TestFalse(TEXT("Hallway under Staircase same"), FPair().Cur(ECT::Hallway, 0, 1).Above(ECT::Staircase, 0, 1).Ceiling());
	TestFalse(TEXT("Staircase under Head same"), FPair().Cur(ECT::Staircase, 0, 1).Above(ECT::StaircaseHead, 0, 1).Ceiling());
	// A different staircase passing overhead is still floored.
	TestTrue(TEXT("Staircase under Staircase other"), FPair().Cur(ECT::Staircase, 0, 1).Above(ECT::Staircase, 0, 2).Ceiling());
	return true;
}

// ---------------------------------------------------------------------------
// Family helpers
// ---------------------------------------------------------------------------

BOUNDARY_TEST(FBoundaryFamilies, "Dungeon.BoundaryRules.Families")
bool FBoundaryFamilies::RunTest(const FString& Parameters)
{
	for (ECT T : { ECT::Room, ECT::Door, ECT::Entrance })
	{
		TestTrue(TEXT("room family"), FDungeonBoundaryRules::IsRoomFamily(T));
		TestFalse(TEXT("not hallway family"), FDungeonBoundaryRules::IsHallwayFamily(T));
	}
	for (ECT T : { ECT::Hallway, ECT::Staircase, ECT::StaircaseHead })
	{
		TestTrue(TEXT("hallway family"), FDungeonBoundaryRules::IsHallwayFamily(T));
		TestFalse(TEXT("not room family"), FDungeonBoundaryRules::IsRoomFamily(T));
	}
	for (ECT T : { ECT::Empty, ECT::RoomWall })
	{
		TestFalse(TEXT("solid: no family"), FDungeonBoundaryRules::IsRoomFamily(T) || FDungeonBoundaryRules::IsHallwayFamily(T));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Rule 5 (horizontal): staircase flanks are walls from both sides
// ---------------------------------------------------------------------------

namespace DungeonBoundaryRulesTestHelpers
{
	/** Set the current cell as a stair-family cell with a climb direction (0=+X, 1=-X, 2=+Y, 3=-Y). */
	FPair& CurStair(FPair& P, EDungeonCellType Type, uint8 HallwayIndex, uint8 Direction)
	{
		P.Cur(Type, 0, HallwayIndex);
		P.Current().StaircaseDirection = Direction;
		return P;
	}

	/** Set the +X neighbour as a stair-family cell with a climb direction. */
	FPair& SideStair(FPair& P, EDungeonCellType Type, uint8 HallwayIndex, uint8 Direction)
	{
		P.Side(Type, 0, HallwayIndex);
		P.Grid.GetCell(FPair::CX + 1, FPair::CY, FPair::CZ).StaircaseDirection = Direction;
		return P;
	}
}

BOUNDARY_TEST(FBoundaryStairSideFace, "Dungeon.BoundaryRules.Wall.StairSideFacePredicate")
bool FBoundaryStairSideFace::RunTest(const FString& Parameters)
{
	FDungeonCell C;
	C.CellType = ECT::Staircase;
	for (uint8 Dir = 0; Dir < 4; ++Dir)
	{
		C.StaircaseDirection = Dir;
		const bool bAlongX = (Dir <= 1);
		TestEqual(FString::Printf(TEXT("dir %d: +X face is flank iff climb along Y"), Dir), FDungeonBoundaryRules::IsStairSideFace(C, 1, 0), !bAlongX);
		TestEqual(FString::Printf(TEXT("dir %d: -X face is flank iff climb along Y"), Dir), FDungeonBoundaryRules::IsStairSideFace(C, -1, 0), !bAlongX);
		TestEqual(FString::Printf(TEXT("dir %d: +Y face is flank iff climb along X"), Dir), FDungeonBoundaryRules::IsStairSideFace(C, 0, 1), bAlongX);
		TestEqual(FString::Printf(TEXT("dir %d: -Y face is flank iff climb along X"), Dir), FDungeonBoundaryRules::IsStairSideFace(C, 0, -1), bAlongX);
	}
	C.CellType = ECT::StaircaseHead;
	C.StaircaseDirection = 0;
	TestTrue(TEXT("head: +Y is a flank"), FDungeonBoundaryRules::IsStairSideFace(C, 0, 1));
	C.CellType = ECT::Hallway;
	TestFalse(TEXT("hallway has no flanks"), FDungeonBoundaryRules::IsStairSideFace(C, 0, 1));
	return true;
}

BOUNDARY_TEST(FBoundaryStairFlanks, "Dungeon.BoundaryRules.Wall.StaircaseFlanksAreWalls")
bool FBoundaryStairFlanks::RunTest(const FString& Parameters)
{
	// The +X face of a stair that climbs along Y is a flank: walled against any hallway, from
	// either side, whatever the index.
	{ FPair P; TestTrue(TEXT("Staircase(+Y) -> Hallway same"), CurStair(P, ECT::Staircase, 1, 2).Side(ECT::Hallway, 0, 1).Wall()); }
	{ FPair P; TestTrue(TEXT("Staircase(+Y) -> Hallway other"), CurStair(P, ECT::Staircase, 1, 2).Side(ECT::Hallway, 0, 2).Wall()); }
	{ FPair P; P.Cur(ECT::Hallway, 0, 1); TestTrue(TEXT("Hallway -> Staircase(-Y) flank"), SideStair(P, ECT::Staircase, 1, 3).Wall()); }
	{ FPair P; TestTrue(TEXT("Head(+Y) -> Hallway same index"), CurStair(P, ECT::StaircaseHead, 1, 2).Side(ECT::Hallway, 0, 1).Wall()); }
	{ FPair P; P.Cur(ECT::Hallway, 0, 1); TestTrue(TEXT("Hallway -> Head(+Y) same index"), SideStair(P, ECT::StaircaseHead, 1, 2).Wall()); }
	// Two staircases side by side (never generated, but the rule must hold): wall.
	{ FPair P; TestTrue(TEXT("Staircase(+Y) -> Staircase(+Y) same index"), SideStair(CurStair(P, ECT::Staircase, 1, 2), ECT::Staircase, 1, 2).Wall()); }
	// A landing cell (plain Hallway) beside a shaft of the same hallway: still walled.
	{ FPair P; P.Cur(ECT::Hallway, 0, 5); TestTrue(TEXT("landing -> Head(-Y) flank"), SideStair(P, ECT::StaircaseHead, 5, 3).Wall()); }
	return true;
}

BOUNDARY_TEST(FBoundaryStairAxisFaces, "Dungeon.BoundaryRules.Wall.StaircaseAxisFacesUnchanged")
bool FBoundaryStairAxisFaces::RunTest(const FString& Parameters)
{
	// The +X face of a stair climbing along X is its climb or entry face: rule 6 still applies.
	{ FPair P; TestFalse(TEXT("Staircase(+X) -> Hallway (climb face, merge)"), CurStair(P, ECT::Staircase, 1, 0).Side(ECT::Hallway, 0, 2).Wall()); }
	{ FPair P; TestFalse(TEXT("Staircase(-X) -> Staircase(-X) same (continuation)"), SideStair(CurStair(P, ECT::Staircase, 1, 1), ECT::Staircase, 1, 1).Wall()); }
	{ FPair P; TestFalse(TEXT("Head(+X) -> Hallway same index (exit)"), CurStair(P, ECT::StaircaseHead, 1, 0).Side(ECT::Hallway, 0, 1).Wall()); }
	{ FPair P; TestTrue(TEXT("Head(+X) -> Hallway other index"), CurStair(P, ECT::StaircaseHead, 1, 0).Side(ECT::Hallway, 0, 2).Wall()); }
	{ FPair P; TestTrue(TEXT("Staircase(+X) -> Room (different space)"), CurStair(P, ECT::Staircase, 1, 0).Side(ECT::Room, 3, 0).Wall()); }
	return true;
}
