// Test_DungeonEntrancePassagePlan.cpp — The stitcher's carve geometry stays inside the volume the
// generator kept clear (keep-out + opening cell), and a style that does not match the dungeon's
// approach is refused instead of carved blindly.
#include "Misc/AutomationTest.h"
#include "DungeonEntrancePassagePlan.h"
#include "DungeonTypes.h"

namespace
{
	constexpr float PassageCS = 400.0f;
	constexpr float PassageVS = 75.0f;
	const FVector PassageOffset(-142216.203f, 20979.24f, -4664.986f);

	/**
	 * 10x10x3 grid, entrance room 4x3x1 at (2,3,0) (cells X 2..5, Y 3..5), EntranceCell = the
	 * ground-floor centre (4,4,0). Approach set per test.
	 */
	FDungeonResult MakeResult()
	{
		FDungeonResult R;
		R.GridSize = FIntVector(10, 10, 3);
		R.CellWorldSize = PassageCS;
		R.Grid.Initialize(R.GridSize);
		FDungeonRoom Room;
		Room.RoomIndex = 1;
		Room.RoomType = EDungeonRoomType::Entrance;
		Room.Position = FIntVector(2, 3, 0);
		Room.Size = FIntVector(4, 3, 1);
		Room.Center = FIntVector(4, 4, 0);
		R.Rooms.Add(Room);
		R.EntranceRoomIndex = 0;
		R.EntranceCell = FIntVector(4, 4, 0);
		return R;
	}

	void SetFromAbove(FDungeonResult& R)
	{
		R.EntranceApproach.Approach = EDungeonEntranceApproach::FromAbove;
		R.EntranceApproach.bSatisfied = true;
		R.EntranceApproach.OpeningCell = FIntVector(4, 4, 0);
		R.EntranceApproach.KeepOutMin = FIntVector(4, 4, 1);
		R.EntranceApproach.KeepOutMax = FIntVector(4, 4, 2);
	}

	void SetFromSideMinX(FDungeonResult& R)
	{
		R.EntranceApproach.Approach = EDungeonEntranceApproach::FromSide;
		R.EntranceApproach.bSatisfied = true;
		R.EntranceApproach.Face = EDungeonGridFace::MinX;
		R.EntranceApproach.OpeningCell = FIntVector(2, 4, 0);
		R.EntranceApproach.KeepOutMin = FIntVector(0, 4, 0);
		R.EntranceApproach.KeepOutMax = FIntVector(1, 4, 0);
	}

	/** Flat terrain 4000 above the grid top. */
	float FlatSurface(float, float) { return PassageOffset.Z + 3 * PassageCS + 4000.0f; }

	bool InKeepOutOrOpening(const FDungeonResult& R, const FIntVector& C)
	{
		const FDungeonEntranceApproachInfo& A = R.EntranceApproach;
		if (C == A.OpeningCell) { return true; }
		return A.HasKeepOut()
			&& C.X >= A.KeepOutMin.X && C.X <= A.KeepOutMax.X
			&& C.Y >= A.KeepOutMin.Y && C.Y <= A.KeepOutMax.Y
			&& C.Z >= A.KeepOutMin.Z && C.Z <= A.KeepOutMax.Z;
	}

	FVector CellCenter(const FIntVector& C)
	{
		return FVector(PassageOffset.X + (C.X + 0.5f) * PassageCS, PassageOffset.Y + (C.Y + 0.5f) * PassageCS, 0.0f);
	}
}

#define PLAN_TEST(ClassName, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
PLAN_TEST(FPassagePlanCompatibility, "Dungeon.EntrancePassagePlan.StyleApproachCompatibility")
bool FPassagePlanCompatibility::RunTest(const FString& Parameters)
{
	using S = EDungeonEntranceStyle;
	using A = EDungeonEntranceApproach;
	const S AllStyles[] = { S::VerticalShaft, S::SlopedTunnel, S::CaveOpening, S::Trapdoor };

	for (S Style : AllStyles)
	{
		TestTrue(TEXT("None accepts every style"), FDungeonEntrancePassagePlan::IsStyleCompatible(Style, A::None));
		TestFalse(TEXT("FromBelow refuses every style"), FDungeonEntrancePassagePlan::IsStyleCompatible(Style, A::FromBelow));
	}
	TestTrue(TEXT("shaft/FromAbove"), FDungeonEntrancePassagePlan::IsStyleCompatible(S::VerticalShaft, A::FromAbove));
	TestTrue(TEXT("cave/FromAbove"), FDungeonEntrancePassagePlan::IsStyleCompatible(S::CaveOpening, A::FromAbove));
	TestTrue(TEXT("trapdoor/FromAbove"), FDungeonEntrancePassagePlan::IsStyleCompatible(S::Trapdoor, A::FromAbove));
	TestFalse(TEXT("tunnel/FromAbove refused"), FDungeonEntrancePassagePlan::IsStyleCompatible(S::SlopedTunnel, A::FromAbove));
	TestTrue(TEXT("tunnel/FromSide"), FDungeonEntrancePassagePlan::IsStyleCompatible(S::SlopedTunnel, A::FromSide));
	TestFalse(TEXT("shaft/FromSide refused"), FDungeonEntrancePassagePlan::IsStyleCompatible(S::VerticalShaft, A::FromSide));

	// Build refuses too, with a reason, and carves nothing.
	FDungeonResult R = MakeResult();
	SetFromAbove(R);
	const FDungeonEntrancePassagePlan Refused = FDungeonEntrancePassagePlan::Build(R, PassageOffset, S::SlopedTunnel, PassageVS, true, 0.0f, FlatSurface);
	TestFalse(TEXT("Build refused"), Refused.IsValid());
	TestEqual(TEXT("no segments"), Refused.Segments.Num(), 0);
	TestTrue(TEXT("reason given"), !Refused.Error.IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
PLAN_TEST(FPassagePlanVertical, "Dungeon.EntrancePassagePlan.VerticalStylesStayInOpeningColumn")
bool FPassagePlanVertical::RunTest(const FString& Parameters)
{
	using S = EDungeonEntranceStyle;
	FDungeonResult R = MakeResult();
	SetFromAbove(R);

	for (S Style : { S::VerticalShaft, S::CaveOpening, S::Trapdoor })
	{
		for (bool bStopAtTop : { true, false })
		{
			const FString Tag = FString::Printf(TEXT("style %d stopAtTop %d: "), static_cast<int32>(Style), bStopAtTop ? 1 : 0);
			const FDungeonEntrancePassagePlan Plan = FDungeonEntrancePassagePlan::Build(R, PassageOffset, Style, PassageVS, bStopAtTop, 0.0f, FlatSurface);
			TestTrue(Tag + TEXT("valid"), Plan.IsValid());
			TestEqual(Tag + TEXT("approach"), Plan.Approach, EDungeonEntranceApproach::FromAbove);
			if (!TestEqual(Tag + TEXT("one column"), Plan.Segments.Num(), 1)) { continue; }

			const FDungeonPassageSegment& Seg = Plan.Segments[0];
			TestEqual(Tag + TEXT("column over the opening cell"), Seg.Cell, R.EntranceApproach.OpeningCell);
			TestTrue(Tag + TEXT("centred on the opening cell"), Seg.Center.Equals(CellCenter(R.EntranceApproach.OpeningCell), 0.01f));
			TestTrue(Tag + TEXT("footprint within the cell"), Seg.HalfExtentXY <= PassageCS * 0.5f + 0.01f);
			TestTrue(Tag + TEXT("carves through the surface band"), FMath::IsNearlyEqual(Seg.TopZ, FlatSurface(0, 0) + 2.0f * PassageVS, 0.01f));
			const float ExpectedBottom = bStopAtTop ? PassageOffset.Z + 1 * PassageCS : PassageOffset.Z + 0 * PassageCS;
			TestTrue(Tag + TEXT("stops at the lid (tiles) / floor (voxel lining)"), FMath::IsNearlyEqual(Seg.BottomZ, ExpectedBottom, 0.01f));
			TestEqual(Tag + TEXT("walls except trapdoor"), Seg.bWalls, Style != S::Trapdoor);
		}
	}

	// A two-floor entrance room: the column stops at the room's lid, one floor higher.
	R.Rooms[0].Size.Z = 2;
	R.EntranceApproach.OpeningCell.Z = 1;
	R.EntranceApproach.KeepOutMin.Z = 2;
	const FDungeonEntrancePassagePlan Tall = FDungeonEntrancePassagePlan::Build(R, PassageOffset, S::VerticalShaft, PassageVS, true, 0.0f, FlatSurface);
	TestTrue(TEXT("tall room: lid plane is the top of floor 1"), FMath::IsNearlyEqual(Tall.Segments[0].BottomZ, PassageOffset.Z + 2 * PassageCS, 0.01f));
	return true;
}

// ---------------------------------------------------------------------------
PLAN_TEST(FPassagePlanSideTunnel, "Dungeon.EntrancePassagePlan.SideTunnelFollowsCorridorThenLeavesGrid")
bool FPassagePlanSideTunnel::RunTest(const FString& Parameters)
{
	FDungeonResult R = MakeResult();
	SetFromSideMinX(R);
	const float FloorZ = PassageOffset.Z; // opening cell on floor 0

	for (bool bStopAtTop : { true, false })
	{
		const FString Tag = FString::Printf(TEXT("stopAtTop %d: "), bStopAtTop ? 1 : 0);
		const FDungeonEntrancePassagePlan Plan = FDungeonEntrancePassagePlan::Build(
			R, PassageOffset, EDungeonEntranceStyle::SlopedTunnel, PassageVS, bStopAtTop, 0.0f, FlatSurface);
		TestTrue(Tag + TEXT("valid"), Plan.IsValid());
		TestEqual(Tag + TEXT("approach"), Plan.Approach, EDungeonEntranceApproach::FromSide);
		TestTrue(Tag + TEXT("tunnel floor is the room floor"), FMath::IsNearlyEqual(Plan.EntranceZ, FloorZ, 0.01f));

		int32 Inside = 0, Outside = 0;
		bool bSeenOpening = false;
		float PrevBottom = -FLT_MAX;
		float PrevX = FLT_MAX;
		for (const FDungeonPassageSegment& Seg : Plan.Segments)
		{
			if (Seg.bInsideGrid)
			{
				++Inside;
				TestTrue(Tag + FString::Printf(TEXT("in-grid column %s is keep-out or opening"), *Seg.Cell.ToString()),
					InKeepOutOrOpening(R, Seg.Cell));
				TestTrue(Tag + TEXT("corridor at floor level"), FMath::IsNearlyEqual(Seg.BottomZ, FloorZ, 0.01f));
				TestTrue(Tag + TEXT("corridor one cell high"), FMath::IsNearlyEqual(Seg.TopZ, FloorZ + PassageCS, 0.01f));
				TestTrue(Tag + TEXT("centred on its cell"), Seg.Center.Equals(CellCenter(Seg.Cell), 0.01f));
				if (Seg.Cell == R.EntranceApproach.OpeningCell)
				{
					bSeenOpening = true;
					TestFalse(Tag + TEXT("no shell inside the room"), Seg.bWalls);
				}
				else
				{
					TestTrue(Tag + TEXT("corridor has a shell"), Seg.bWalls);
				}
				TestEqual(Tag + TEXT("outside columns come after inside ones"), Outside, 0);
			}
			else
			{
				++Outside;
				TestTrue(Tag + TEXT("outside the MinX edge"), Seg.Center.X < PassageOffset.X);
				TestTrue(Tag + TEXT("ramp rises monotonically"), Seg.BottomZ >= PrevBottom - 0.01f);
				TestTrue(Tag + TEXT("ramp moves outward"), Seg.Center.X < PrevX + 0.01f);
				TestTrue(Tag + TEXT("same row"), FMath::IsNearlyEqual(Seg.Center.Y, CellCenter(R.EntranceApproach.OpeningCell).Y, 0.01f));
				TestTrue(Tag + TEXT("walkable slope: rise per column <= run * slope"),
					Seg.BottomZ - FMath::Max(PrevBottom, FloorZ) <= PassageVS * FDungeonEntrancePassagePlan::TunnelRiseOverRun + 0.01f);
				PrevBottom = Seg.BottomZ;
				PrevX = Seg.Center.X;
			}
		}
		// Tile-dressed: the mapper opens the wall, the carve starts outside the room (2 corridor
		// cells). Voxel-lined: the opening cell itself is carved too (3 in-grid columns).
		TestEqual(Tag + TEXT("in-grid columns"), Inside, bStopAtTop ? 2 : 3);
		TestEqual(Tag + TEXT("opening cell carved only for voxel lining"), bSeenOpening, !bStopAtTop);
		TestTrue(Tag + TEXT("ramp exists"), Outside > 0);
		TestTrue(Tag + TEXT("ramp breaks the surface"), PrevBottom >= Plan.CarveTopZ - PassageVS * FDungeonEntrancePassagePlan::TunnelRiseOverRun - 0.01f);
		TestTrue(Tag + TEXT("carve top overshoots the surface"), FMath::IsNearlyEqual(Plan.CarveTopZ, FlatSurface(0, 0) + 2.0f * PassageVS, 0.01f));
		TestTrue(Tag + TEXT("mouth is outside the grid"), Plan.MouthXY.X < PassageOffset.X);
	}
	return true;
}

// ---------------------------------------------------------------------------
PLAN_TEST(FPassagePlanLegacy, "Dungeon.EntrancePassagePlan.LegacyAndUnsatisfiedAreBestEffort")
bool FPassagePlanLegacy::RunTest(const FString& Parameters)
{
	using S = EDungeonEntranceStyle;

	// No approach: every style builds, nothing is refused.
	FDungeonResult R = MakeResult();
	for (S Style : { S::VerticalShaft, S::SlopedTunnel, S::CaveOpening, S::Trapdoor })
	{
		const FDungeonEntrancePassagePlan Plan = FDungeonEntrancePassagePlan::Build(R, PassageOffset, Style, PassageVS, false, 0.0f, FlatSurface);
		TestTrue(FString::Printf(TEXT("legacy style %d builds"), static_cast<int32>(Style)), Plan.IsValid());
		TestEqual(TEXT("approach None"), Plan.Approach, EDungeonEntranceApproach::None);
		TestTrue(TEXT("has segments"), Plan.Segments.Num() > 0);
		TestTrue(TEXT("no note"), Plan.Note.IsEmpty());
	}

	// Legacy sloped tunnel: the old in-grid diagonal from the entrance cell.
	{
		const FDungeonEntrancePassagePlan Plan = FDungeonEntrancePassagePlan::Build(R, PassageOffset, S::SlopedTunnel, PassageVS, false, 0.0f, FlatSurface);
		TestEqual(TEXT("legacy tunnel starts at the entrance cell"), Plan.Segments[0].Cell, R.EntranceCell);
		TestTrue(TEXT("legacy tunnel steps up one cell per column"),
			Plan.Segments.Num() > 1 && FMath::IsNearlyEqual(Plan.Segments[1].BottomZ - Plan.Segments[0].BottomZ, PassageCS, 0.01f));
	}

	// Requested but unsatisfied: treated as legacy, with a note for the log.
	SetFromAbove(R);
	R.EntranceApproach.bSatisfied = false;
	const FDungeonEntrancePassagePlan Unsat = FDungeonEntrancePassagePlan::Build(R, PassageOffset, S::SlopedTunnel, PassageVS, false, 0.0f, FlatSurface);
	TestTrue(TEXT("unsatisfied builds"), Unsat.IsValid());
	TestEqual(TEXT("treated as None"), Unsat.Approach, EDungeonEntranceApproach::None);
	TestFalse(TEXT("note explains the fallback"), Unsat.Note.IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
PLAN_TEST(FPassagePlanSideTunnelFloorLift, "Dungeon.EntrancePassagePlan.SideTunnelFloorLiftMeetsWalkableFloor")
bool FPassagePlanSideTunnelFloorLift::RunTest(const FString& Parameters)
{
	FDungeonResult R = MakeResult();
	SetFromSideMinX(R);
	const float CellBottomZ = PassageOffset.Z;
	const float Lift = 80.0f; // a 400-cell tile floor slab

	const FDungeonEntrancePassagePlan Plan = FDungeonEntrancePassagePlan::Build(
		R, PassageOffset, EDungeonEntranceStyle::SlopedTunnel, PassageVS, true, Lift, FlatSurface);
	TestTrue(TEXT("valid"), Plan.IsValid());
	TestTrue(TEXT("tunnel floor is the walkable floor"), FMath::IsNearlyEqual(Plan.EntranceZ, CellBottomZ + Lift, 0.01f));
	for (const FDungeonPassageSegment& Seg : Plan.Segments)
	{
		if (!Seg.bInsideGrid) { break; }
		TestTrue(TEXT("corridor floor lifted"), FMath::IsNearlyEqual(Seg.BottomZ, CellBottomZ + Lift, 0.01f));
		TestTrue(TEXT("corridor ceiling stays at the cell top"), FMath::IsNearlyEqual(Seg.TopZ, CellBottomZ + PassageCS, 0.01f));
	}
	// The ramp keeps the same clear height as the corridor.
	const FDungeonPassageSegment* FirstOutside = Plan.Segments.FindByPredicate([](const FDungeonPassageSegment& S) { return !S.bInsideGrid; });
	TestTrue(TEXT("ramp exists"), FirstOutside != nullptr);
	if (FirstOutside)
	{
		TestTrue(TEXT("ramp starts at the lifted floor"), FMath::IsNearlyEqual(FirstOutside->BottomZ, CellBottomZ + Lift, 0.01f));
		TestTrue(TEXT("ramp clear height = cell - lift"), FMath::IsNearlyEqual(FirstOutside->TopZ - FirstOutside->BottomZ, PassageCS - Lift, 0.01f));
	}
	return true;
}

// ---------------------------------------------------------------------------
PLAN_TEST(FPassagePlanShellFlags, "Dungeon.EntrancePassagePlan.SideTunnelHasFloorAndCeilingShell")
bool FPassagePlanShellFlags::RunTest(const FString& Parameters)
{
	// A horizontal passage through the cave layer needs a floor and a lid; a vertical shaft is
	// enclosed by its sides alone. The opening cell (voxel-lined mode) has no shell at all.
	FDungeonResult Side = MakeResult();
	SetFromSideMinX(Side);
	const FDungeonEntrancePassagePlan Tunnel = FDungeonEntrancePassagePlan::Build(
		Side, PassageOffset, EDungeonEntranceStyle::SlopedTunnel, PassageVS, false, 0.0f, FlatSurface);
	for (const FDungeonPassageSegment& Seg : Tunnel.Segments)
	{
		if (Seg.bInsideGrid && Seg.Cell == Side.EntranceApproach.OpeningCell)
		{
			TestFalse(TEXT("opening cell: no side shell"), Seg.bWalls);
			TestFalse(TEXT("opening cell: no cap shell"), Seg.bFloorCeilingShell);
		}
		else
		{
			TestTrue(TEXT("tunnel column: side shell"), Seg.bWalls);
			TestTrue(TEXT("tunnel column: floor + ceiling shell"), Seg.bFloorCeilingShell);
		}
	}

	FDungeonResult Above = MakeResult();
	SetFromAbove(Above);
	const FDungeonEntrancePassagePlan Shaft = FDungeonEntrancePassagePlan::Build(
		Above, PassageOffset, EDungeonEntranceStyle::VerticalShaft, PassageVS, true, 0.0f, FlatSurface);
	TestTrue(TEXT("shaft: side shell"), Shaft.Segments[0].bWalls);
	TestFalse(TEXT("shaft: no cap shell"), Shaft.Segments[0].bFloorCeilingShell);
	return true;
}
