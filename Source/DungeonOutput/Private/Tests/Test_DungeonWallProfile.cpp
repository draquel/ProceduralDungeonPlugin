// Test_DungeonWallProfile.cpp — Environment E2: the wall profile every wall-family piece is
// authored to (conformance measured from module bounds), the mapper's profile fit for single
// wall meshes, and corner posts.
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "DungeonTileModule.h"
#include "DungeonWallProfileConformance.h"
#include "DungeonBoundaryRules.h"
#include "Engine/StaticMesh.h"

namespace DungeonWallProfileTestHelpers
{
	const FSoftObjectPath EngineCube(TEXT("/Engine/BasicShapes/Cube.Cube")); // 100 x 100 x 100, centred

	/** A module of cube elements: (X offset, XYZ scale) each, at ReferenceCellSize 400. */
	UDungeonTileModule* MakeCubeModule(std::initializer_list<TPair<float, FVector>> Elements)
	{
		UDungeonTileModule* Module = NewObject<UDungeonTileModule>();
		Module->AddToRoot();
		Module->ReferenceCellSize = 400.0f;
		for (const TPair<float, FVector>& E : Elements)
		{
			FDungeonModuleElement El;
			El.Mesh = TSoftObjectPtr<UStaticMesh>(EngineCube);
			El.RelativeTransform = FTransform(FRotator::ZeroRotator, FVector(E.Key, 0.0f, 0.0f), E.Value);
			Module->Elements.Add(El);
		}
		return Module;
	}

	UDungeonTileSet* MakeTileSet()
	{
		UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
		TS->AddToRoot();
		return TS;
	}

	/** 5x5x1: 3x3 room at (1,1) ringed by RoomWall. */
	FDungeonResult MakeSingleRoom()
	{
		FDungeonResult R;
		R.GridSize = FIntVector(5, 5, 1);
		R.CellWorldSize = 400.0f;
		R.Grid.Initialize(R.GridSize);
		for (int32 Y = 0; Y < 5; ++Y) for (int32 X = 0; X < 5; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1; }
		for (int32 Y = 1; Y <= 3; ++Y) for (int32 X = 1; X <= 3; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::Room; C.RoomIndex = 1; }
		R.EntranceRoomIndex = -1;
		return R;
	}

	/** 7x5x1: corridor along y=2 from x=1..5 with a one-cell side branch at (3,3). Everything else Empty. */
	FDungeonResult MakeCorridorWithBranch()
	{
		FDungeonResult R;
		R.GridSize = FIntVector(7, 5, 1);
		R.CellWorldSize = 400.0f;
		R.Grid.Initialize(R.GridSize);
		for (int32 X = 1; X <= 5; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, 2, 0); C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; }
		{ FDungeonCell& C = R.Grid.GetCell(3, 3, 0); C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; }
		R.EntranceRoomIndex = -1;
		return R;
	}

	int32 CountAt(const FDungeonTileMapResult& Map, EDungeonTileType Type, const FVector& P)
	{
		int32 N = 0;
		for (const FTransform& Xf : Map.Transforms[static_cast<int32>(Type)])
		{
			if (Xf.GetLocation().Equals(P, 1.0f)) { ++N; }
		}
		return N;
	}
}

using namespace DungeonWallProfileTestHelpers;

#define PROFILE_TEST(ClassName, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Conformance: modules measured against the profile from their element bounds.
// ---------------------------------------------------------------------------
PROFILE_TEST(FWallProfileConformance, "Dungeon.WallProfile.ModuleConformance")
bool FWallProfileConformance::RunTest(const FString& Parameters)
{
	FDungeonWallProfile P; // defaults: FaceInset 40, PartitionThickness 80, MaxProtrusion 30, Tolerance 2
	TArray<FString> Issues;

	// Wall body: cube (±50) at X=+10 -> inner face at -40. Conforms.
	{
		UDungeonTileModule* M = MakeCubeModule({ { 10.0f, FVector(1.0f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::WallSegment;
		TestTrue(TEXT("measured"), FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me));
		TestTrue(TEXT("body inner -40"), FMath::IsNearlyEqual(Me.BodyInnerX, -40.0f, 0.01f));
		TestTrue(TEXT("body outer +60"), FMath::IsNearlyEqual(Me.BodyOuterX, 60.0f, 0.01f));
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("wall at the inset conforms"), Issues.Num(), 0);
		M->RemoveFromRoot();
	}
	// Wall body centred on the plane (inner -50): off by 10 -> issue.
	{
		UDungeonTileModule* M = MakeCubeModule({ { 0.0f, FVector(1.0f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::WallSegment;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("wall 10 off the inset is reported"), Issues.Num(), 1);
		M->RemoveFromRoot();
	}
	// Door: body at the inset + a jamb (scale 0.2 -> ±10) standing 25 proud (X=-55, inner -65): OK.
	{
		UDungeonTileModule* M = MakeCubeModule({ { 10.0f, FVector(1.0f) }, { -55.0f, FVector(0.2f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::DoorFrame;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		TestTrue(TEXT("body is the larger element"), FMath::IsNearlyEqual(Me.BodyInnerX, -40.0f, 0.01f));
		TestTrue(TEXT("innermost extent is the jamb"), FMath::IsNearlyEqual(Me.InnerX, -65.0f, 0.01f));
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("jamb within MaxProtrusion conforms"), Issues.Num(), 0);
		M->RemoveFromRoot();
	}
	// Door with a jamb 50 proud (X=-80): beyond MaxProtrusion -> issue.
	{
		UDungeonTileModule* M = MakeCubeModule({ { 10.0f, FVector(1.0f) }, { -80.0f, FVector(0.2f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::DoorFrame;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("jamb beyond MaxProtrusion is reported"), Issues.Num(), 1);
		M->RemoveFromRoot();
	}
	// Partition: cube scaled 0.8 in X (±40), centred -> symmetric 80 thick. Conforms.
	{
		UDungeonTileModule* M = MakeCubeModule({ { 0.0f, FVector(0.8f, 1.0f, 1.0f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::WallPartition;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("symmetric partition conforms"), Issues.Num(), 0);
		M->RemoveFromRoot();
	}
	// Partition shifted 20 outward: the -X face is not reached -> issue.
	{
		UDungeonTileModule* M = MakeCubeModule({ { 20.0f, FVector(0.8f, 1.0f, 1.0f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::WallPartition;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("off-centre partition is reported"), Issues.Num(), 1);
		M->RemoveFromRoot();
	}
	// Partition as two thin back-to-back slabs (the pack's single-sided quads): thin cubes (X 0.1
	// -> ±5) at -35 and +35 reach both faces of the 80 slab together. Conforms.
	{
		UDungeonTileModule* M = MakeCubeModule({ { -35.0f, FVector(0.1f, 1.0f, 1.0f) }, { 35.0f, FVector(0.1f, 1.0f, 1.0f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::WallPartition;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		TestTrue(TEXT("two-slab partition spans -40..40"), FMath::IsNearlyEqual(Me.InnerX, -40.0f, 0.01f) && FMath::IsNearlyEqual(Me.OuterX, 40.0f, 0.01f));
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("two-slab partition conforms"), Issues.Num(), 0);
		M->RemoveFromRoot();
	}
	// A partition that only covers one face (a single quad at -40): reported.
	{
		UDungeonTileModule* M = MakeCubeModule({ { -35.0f, FVector(0.1f, 1.0f, 1.0f) } });
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::WallPartition;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		Issues.Reset(); FDungeonWallProfileConformance::CheckMeasure(Me, P, Issues);
		TestEqual(TEXT("one-sided partition is reported"), Issues.Num(), 1);
		M->RemoveFromRoot();
	}
	// Reference-cell rescale: the same wall authored at 200 measures identically at profile 400.
	{
		UDungeonTileModule* M = MakeCubeModule({ { 5.0f, FVector(0.5f) } }); // at 200: cube ±25 at +5 -> inner -20
		M->ReferenceCellSize = 200.0f;
		FDungeonWallProfileMeasure Me; Me.Type = EDungeonTileType::WallSegment;
		FDungeonWallProfileConformance::MeasureModule(*M, 400.0f, Me);
		TestTrue(TEXT("rescaled inner face -40"), FMath::IsNearlyEqual(Me.BodyInnerX, -40.0f, 0.01f));
		M->RemoveFromRoot();
	}

	// Whole-tileset check through the tileset API: a conforming wall + an off door -> one issue.
	{
		UDungeonTileSet* TS = MakeTileSet();
		UDungeonTileModule* Wall = MakeCubeModule({ { 10.0f, FVector(1.0f) } });
		UDungeonTileModule* Door = MakeCubeModule({ { 0.0f, FVector(1.0f) } });
		FDungeonTileSlot WallSlot; WallSlot.Module = TSoftObjectPtr<UDungeonTileModule>(Wall);
		FDungeonTileSlot DoorSlot; DoorSlot.Module = TSoftObjectPtr<UDungeonTileModule>(Door);
		TS->Slots.Add(EDungeonTileType::WallSegment, WallSlot);
		TS->Slots.Add(EDungeonTileType::DoorFrame, DoorSlot);
		const TArray<FString> TileSetIssues = TS->CheckWallProfile();
		TestEqual(TEXT("tileset reports the off door only"), TileSetIssues.Num(), 1);
		TestTrue(TEXT("issue names the door"), TileSetIssues.Num() == 1 && TileSetIssues[0].Contains(TEXT("DoorFrame")));
		Wall->RemoveFromRoot(); Door->RemoveFromRoot(); TS->RemoveFromRoot();
	}
	return true;
}

// ---------------------------------------------------------------------------
// Mapper fit: single wall meshes land their finished face at FaceInset; partitions get
// PartitionThickness centred on the plane. Defaults reproduce the pre-profile placement.
// ---------------------------------------------------------------------------
PROFILE_TEST(FWallProfileSingleMeshFit, "Dungeon.WallProfile.SingleMeshFitFollowsProfile")
bool FWallProfileSingleMeshFit::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeTileSet(); // default slots: engine cube walls, TileThickness 80 at CS 400
	TS->Slots.Add(EDungeonTileType::WallPartition, TS->GetSlot(EDungeonTileType::WallSegment));
	FDungeonResult R = MakeSingleRoom();
	// Cell (1,1)'s -X face centre: X = 400, Y = 600, Z = 200.
	const FVector Face(400.0f, 600.0f, 200.0f);

	// Default profile (inset 40 = half of the 80 slab): the wall is centred on the plane.
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		TestEqual(TEXT("default: wall centred on the face plane"), CountAt(Map, EDungeonTileType::WallSegment, Face), 1);
	}
	// FaceInset 20: the 80 slab slides 20 OUTWARD (-X for this face) so its inner face sits at -20.
	TS->WallProfile.FaceInset = 20.0f;
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		TestEqual(TEXT("inset 20: wall centre moved 20 outward"), CountAt(Map, EDungeonTileType::WallSegment, Face + FVector(-20.0f, 0.0f, 0.0f)), 1);
		TestEqual(TEXT("inset 20: nothing left on the plane"), CountAt(Map, EDungeonTileType::WallSegment, Face), 0);
	}
	// Profile authored at 200 with inset 10 == inset 20 at cell 400 (scales with the cell).
	TS->WallProfile.ReferenceCellSize = 200.0f;
	TS->WallProfile.FaceInset = 10.0f;
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		TestEqual(TEXT("profile scales with the cell"), CountAt(Map, EDungeonTileType::WallSegment, Face + FVector(-20.0f, 0.0f, 0.0f)), 1);
	}

	// Partition thickness: room beside a corridor; the partition slab is PartitionThickness thick, centred.
	TS->WallProfile = FDungeonWallProfile();
	TS->WallProfile.PartitionThickness = 60.0f;
	{
		FDungeonResult RB;
		RB.GridSize = FIntVector(6, 3, 1); RB.CellWorldSize = 400.0f; RB.Grid.Initialize(RB.GridSize);
		for (int32 Y = 0; Y < 3; ++Y) for (int32 X = 0; X < 4; ++X)
		{ FDungeonCell& C = RB.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1; }
		for (int32 X = 1; X <= 2; ++X) { FDungeonCell& C = RB.Grid.GetCell(X, 1, 0); C.CellType = EDungeonCellType::Room; C.RoomIndex = 1; }
		for (int32 X = 3; X <= 4; ++X) { FDungeonCell& C = RB.Grid.GetCell(X, 1, 0); C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; C.RoomIndex = 0; }
		RB.EntranceRoomIndex = -1;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(RB, *TS, FVector::ZeroVector);
		const FVector Shared(1200.0f, 600.0f, 200.0f);
		bool bFound = false;
		for (const FTransform& Xf : Map.Transforms[static_cast<int32>(EDungeonTileType::WallPartition)])
		{
			if (Xf.GetLocation().Equals(Shared, 1.0f))
			{
				bFound = true;
				TestTrue(TEXT("partition stays centred on the plane"), true);
				TestTrue(TEXT("partition slab is 60 thick (cube 100 -> scale 0.6)"), FMath::IsNearlyEqual(Xf.GetScale3D().X, 0.6f, 0.001f));
			}
		}
		TestTrue(TEXT("partition placed on the shared face"), bFound);
	}

	TS->RemoveFromRoot();
	return true;
}

// ---------------------------------------------------------------------------
// Corners: inner posts where two walled faces of a cell meet, once per corner point; outer
// posts where a wall run ends at an unframed opening; none beside a door frame.
// ---------------------------------------------------------------------------
PROFILE_TEST(FWallProfileCorners, "Dungeon.TileMapper.Corners.InnerAndOuterPosts")
bool FWallProfileCorners::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeTileSet();
	FDungeonTileSlot Post; Post.Mesh = TSoftObjectPtr<UStaticMesh>(EngineCube);

	// No corner slots: nothing placed (today's behaviour).
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeSingleRoom(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("no slots: no inner posts"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerInner)].Num(), 0);
		TestEqual(TEXT("no slots: no outer posts"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerOuter)].Num(), 0);
	}

	TS->Slots.Add(EDungeonTileType::WallCornerInner, Post);
	TS->Slots.Add(EDungeonTileType::WallCornerOuter, Post);

	// Single 3x3 room: exactly its four corners, at floor level, once each; no outer posts. Every
	// neighbour is rock, so each post is pulled FaceInset (40) in from the corner point along
	// both walls: it stands on the two wall faces.
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeSingleRoom(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("room: 4 inner posts"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerInner)].Num(), 4);
		TestEqual(TEXT("room: 0 outer posts"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerOuter)].Num(), 0);
		for (const FVector& P : { FVector(440, 440, 0), FVector(1560, 440, 0), FVector(440, 1560, 0), FVector(1560, 1560, 0) })
		{
			TestEqual(FString::Printf(TEXT("room: one post at %s"), *P.ToString()), CountAt(Map, EDungeonTileType::WallCornerInner, P), 1);
		}
		// Uniform scale from the profile's reference cell (400 at 400 -> 1).
		for (const FTransform& Xf : Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerInner)])
		{
			TestTrue(TEXT("room: post scale uniform 1"), Xf.GetScale3D().Equals(FVector(1.0f), 0.001f));
		}
	}

	// Corridor with a side branch: the branch mouth ends the corridor's +Y wall twice (outer
	// posts at the corner points (1200,1200) and (1600,1200), pulled 40 in along the ending +Y
	// wall's normal -> y 1160); the branch cell's far corners are inner posts ((1200,1600) and
	// (1600,1600) -> (1240,1560), (1560,1560)); the corridor's two ends are inner corners too (4 more).
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeCorridorWithBranch(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("branch: outer post at the mouth's -X side"), CountAt(Map, EDungeonTileType::WallCornerOuter, FVector(1200, 1160, 0)), 1);
		TestEqual(TEXT("branch: outer post at the mouth's +X side"), CountAt(Map, EDungeonTileType::WallCornerOuter, FVector(1600, 1160, 0)), 1);
		TestEqual(TEXT("branch: exactly 2 outer posts"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerOuter)].Num(), 2);
		TestEqual(TEXT("branch: inner post at its far -X corner"), CountAt(Map, EDungeonTileType::WallCornerInner, FVector(1240, 1560, 0)), 1);
		TestEqual(TEXT("branch: inner post at its far +X corner"), CountAt(Map, EDungeonTileType::WallCornerInner, FVector(1560, 1560, 0)), 1);
		TestEqual(TEXT("branch: 2 + 4 corridor-end inner posts"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerInner)].Num(), 6);
		// Orientation: the outer post at (1200,1200) belongs to cell (2,2)'s +Y wall ending at its +X
		// open face -> +X points +X (yaw 0); at (1600,1200) it is cell (4,2)'s wall ending at -X -> yaw 180.
		for (const FTransform& Xf : Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerOuter)])
		{
			const float Yaw = FRotator::NormalizeAxis(Xf.Rotator().Yaw);
			if (Xf.GetLocation().Equals(FVector(1200, 1160, 0), 1.0f)) { TestTrue(TEXT("outer yaw 0"), FMath::IsNearlyEqual(Yaw, 0.0f, 0.5f)); }
			if (Xf.GetLocation().Equals(FVector(1600, 1160, 0), 1.0f)) { TestTrue(TEXT("outer yaw 180"), FMath::IsNearlyEqual(FMath::Abs(Yaw), 180.0f, 0.5f)); }
		}
	}

	// Doorway: room A (1..3,1..3), Door at (4,2), hallway (5..7,2), room B. No outer post at the
	// door's jambs (the frame covers them); the room's 4 corners are inner posts.
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
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		// The only wall ends in this map are the door's jambs (corner points (2000,800) and
		// (2000,1200), on both the room and the hallway side): the frame covers them -> no outer posts.
		TestEqual(TEXT("door: no outer posts anywhere"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallCornerOuter)].Num(), 0);
		// The hallway's far end (7,2): +X walled and both ±Y walled -> 2 inner posts there, pulled
		// 40 in from the corner points (3200,800)/(3200,1200) along both rock-backed faces.
		TestEqual(TEXT("hallway end: inner post -Y"), CountAt(Map, EDungeonTileType::WallCornerInner, FVector(3160, 840, 0)), 1);
		TestEqual(TEXT("hallway end: inner post +Y"), CountAt(Map, EDungeonTileType::WallCornerInner, FVector(3160, 1160, 0)), 1);
	}

	// Partition depth: room A (1..3,1..3) directly beside a hallway (4..6,2) with no door. The
	// shared face gets a partition (centred on the plane, PartitionThickness 120 -> face 60 in),
	// so the hallway's two inner posts at its -X end are pulled 60 along X (partition) and 40
	// along Y (rock): corner points (1600,800)/(1600,1200) -> (1660,840)/(1660,1160).
	{
		TS->Slots.Add(EDungeonTileType::WallPartition, TS->GetSlot(EDungeonTileType::WallSegment));
		TS->WallProfile.PartitionThickness = 120.0f;
		FDungeonResult R;
		R.GridSize = FIntVector(9, 5, 1); R.CellWorldSize = 400.0f; R.Grid.Initialize(R.GridSize);
		for (int32 Y = 0; Y < 5; ++Y) for (int32 X = 0; X < 5; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1; }
		for (int32 Y = 1; Y <= 3; ++Y) for (int32 X = 1; X <= 3; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, Y, 0); C.CellType = EDungeonCellType::Room; C.RoomIndex = 1; }
		for (int32 X = 4; X <= 6; ++X) { FDungeonCell& C = R.Grid.GetCell(X, 2, 0); C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; C.RoomIndex = 0; }
		R.EntranceRoomIndex = -1;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		TestEqual(TEXT("partition: one partition piece on the shared face"), Map.Transforms[static_cast<int32>(EDungeonTileType::WallPartition)].Num(), 1);
		TestEqual(TEXT("partition: hallway -Y post pulled 60 from the partition"), CountAt(Map, EDungeonTileType::WallCornerInner, FVector(1660, 840, 0)), 1);
		TestEqual(TEXT("partition: hallway +Y post pulled 60 from the partition"), CountAt(Map, EDungeonTileType::WallCornerInner, FVector(1660, 1160, 0)), 1);
		TestEqual(TEXT("partition: room corner unchanged (rock both sides)"), CountAt(Map, EDungeonTileType::WallCornerInner, FVector(1560, 440, 0)), 1);
		TS->WallProfile.PartitionThickness = 80.0f;
	}

	TS->RemoveFromRoot();
	return true;
}
