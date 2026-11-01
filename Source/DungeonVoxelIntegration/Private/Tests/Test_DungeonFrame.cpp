// Test_DungeonFrame.cpp — the dungeon stamp and entrance stitch in a frame (spherical Phase 5, SP-G6).
//
// A planet POI lays its dungeon out in its pad space, the tangent frame at the POI's centre, so the dungeon's floors lie
// across local up (Plugins/VoxelWorlds/Documentation/WORLD_MODES_REVIEW_PLAN.md §19). The voxel lattice stays the world's,
// so a rotated frame enumerates each frame-space box by testing the samples of its world bounds in frame space.
//
// - Dungeon.Stamp.RotatedFrame: StampDungeonInFrame with the identity frame writes exactly StampDungeon's edits (both voxel
//   modes, every edit's chunk, index, mode and data); in a tangent frame on a small planet the open cells are carved to
//   air through their interiors, the stone jacket stands across every closed face toward rock (CarveUnderground: the
//   inner lining inside the cell plane and the rest outside it; CarveOnly: the outer seal), and no edit lies outside the
//   grid box and its shell.
// - Dungeon.Stitch.PlanetSurface: StitchEntranceInFrame with the identity frame is StitchEntrance exactly; in a tangent
//   frame on the planet the vertical shaft runs along the frame's z from the entrance room's lid up to where that line
//   meets the generated surface, and stops there.

#include "Misc/AutomationTest.h"
#include "DungeonVoxelTestFixtures.h"
#include "DungeonBoundaryRules.h"
#include "DungeonConfiguration.h"
#include "DungeonGenerator.h"
#include "DungeonTypes.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelStamper.h"
#include "DungeonEntranceStitcher.h"
#include "IVoxelWorldMode.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWorldConfiguration.h"
#include "Testing/VoxelTestChunkWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonFrameTest
{
	using namespace DungeonVoxelTesting;

	/** Walls two voxels thick with half a voxel of carve margin (the frame tests' lattice). */
	UDungeonVoxelConfig* MakeVoxelConfig()
	{
		UDungeonVoxelConfig* VoxelConfig = DungeonVoxelTesting::MakeVoxelConfig(2);
		VoxelConfig->CarveMarginVoxels = 0.5f;
		return VoxelConfig;
	}

	/** One edit as compared across worlds (the timestamp and the generated original are left out). */
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

	/** Every edit in the world by (chunk, index). */
	TMap<FIntVector, TMap<int32, FEditKey>> CollectEdits(const UVoxelEditManager& Edits)
	{
		TMap<FIntVector, TMap<int32, FEditKey>> Out;
		TArray<FIntVector> Chunks;
		Edits.GetEditedChunkCoords(Chunks);
		for (const FIntVector& Chunk : Chunks)
		{
			const FChunkEditLayer* Layer = Edits.GetEditLayer(Chunk);
			if (!Layer)
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
	int32 CountDifferences(const TMap<FIntVector, TMap<int32, FEditKey>>& A, const TMap<FIntVector, TMap<int32, FEditKey>>& B)
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

	int32 CountEdits(const TMap<FIntVector, TMap<int32, FEditKey>>& Edits)
	{
		int32 Count = 0;
		for (const TPair<FIntVector, TMap<int32, FEditKey>>& Chunk : Edits)
		{
			Count += Chunk.Value.Num();
		}
		return Count;
	}

	/**
	 * The edit at the lattice sample nearest a world point, or null. Nearest, not the interval holding it: the lattice is
	 * corner-sampled, so the interval's sample is up to a voxel away on each world axis, and in a rotated frame that is up to
	 * sqrt(3) voxels along any frame axis.
	 */
	const FVoxelEdit* EditAt(const UVoxelEditManager& Edits, const UVoxelWorldConfiguration& World, const FVector& WorldPos)
	{
		const FVector Relative = (WorldPos - World.WorldOrigin) / World.VoxelSize;
		const FIntVector Index(FMath::RoundToInt(Relative.X), FMath::RoundToInt(Relative.Y), FMath::RoundToInt(Relative.Z));
		const FIntVector Chunk(FMath::FloorToInt(static_cast<float>(Index.X) / World.ChunkSize),
			FMath::FloorToInt(static_cast<float>(Index.Y) / World.ChunkSize), FMath::FloorToInt(static_cast<float>(Index.Z) / World.ChunkSize));
		const FChunkEditLayer* Layer = Edits.GetEditLayer(Chunk);
		return Layer ? Layer->GetEdit(Index - Chunk * World.ChunkSize) : nullptr;
	}

	/** The world position of an edited voxel's sample (corner-sampled lattice). */
	FVector SamplePositionOf(const UVoxelWorldConfiguration& World, const FIntVector& Chunk, const FIntVector& Local)
	{
		return World.WorldOrigin + FVector(Chunk) * (World.ChunkSize * World.VoxelSize) + FVector(Local) * World.VoxelSize;
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


}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStampRotatedFrameTest, "Dungeon.Stamp.RotatedFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStampRotatedFrameTest::RunTest(const FString& Parameters)
{
	using namespace DungeonFrameTest;
	UDungeonConfiguration* Config = MakeConfig();
	UDungeonVoxelConfig* VoxelConfig = MakeVoxelConfig();
	const FDungeonResult Result = Generate(Config);
	if (!TestTrue(TEXT("The dungeon generates (non-vacuous)"), Result.Rooms.Num() > 0))
	{
		Config->RemoveFromRoot();
		VoxelConfig->RemoveFromRoot();
		return false;
	}
	const float CS = Result.CellWorldSize;

	// ---- The identity frame is StampDungeon exactly, in both voxel modes ----
	// An origin with no particular phase against the lattice.
	const FVector PlaneOffset(-1713.37, 942.61, -2611.29);
	for (const EDungeonStampMode Mode : { EDungeonStampMode::CarveUnderground, EDungeonStampMode::CarveOnly })
	{
		TMap<FIntVector, TMap<int32, FEditKey>> Edits[2];
		int32 Modified[2] = { 0, 0 };
		for (int32 Run = 0; Run < 2; ++Run)
		{
			VoxelTesting::FTestChunkWorld W;
			W.Create(Run == 0 ? TEXT("DungeonFrameStampA") : TEXT("DungeonFrameStampB"));
			W.ConfigureSmallPlane();
			W.StartManager();
			UDungeonVoxelStamper* Stamper = NewObject<UDungeonVoxelStamper>();
			const FDungeonStampResult Stamp = Stamper->StampDungeonInFrame(Result, W.Manager, PlaneOffset, Mode, VoxelConfig, FTransform::Identity);
			TestTrue(FString::Printf(TEXT("Mode %d run %d: the stamp succeeds"), static_cast<int32>(Mode), Run), Stamp.bSuccess);
			Modified[Run] = Stamp.VoxelsModified;
			if (const UVoxelEditManager* EditManager = W.Manager->GetEditManager())
			{
				Edits[Run] = CollectEdits(*EditManager);
			}
			W.Stop();
		}
		AddInfo(FString::Printf(TEXT("Mode %d: %d edits, %d voxels modified with StampDungeon"),
			static_cast<int32>(Mode), CountEdits(Edits[0]), Modified[0]));
		TestTrue(FString::Printf(TEXT("Mode %d: the stamp edits (non-vacuous)"), static_cast<int32>(Mode)), CountEdits(Edits[0]) > 1000);
		TestEqual(FString::Printf(TEXT("Mode %d: the same voxel count"), static_cast<int32>(Mode)), Modified[1], Modified[0]);
		TestEqual(FString::Printf(TEXT("Mode %d: the identity frame writes exactly StampDungeon's edits"), static_cast<int32>(Mode)),
			CountDifferences(Edits[0], Edits[1]), 0);
	}

	// ---- A tangent frame on a small planet ----
	// The grid hangs 1,500 below the datum at latitude 40, longitude 25, well under the terrain (+-1,000).
	const double Radius = 20000.0;
	const FTransform Frame = TangentFrame(40.0, 25.0, Radius);
	const FVector FrameOffset(-Result.GridSize.X * CS * 0.5f, -Result.GridSize.Y * CS * 0.5f, -1500.0f - Result.GridSize.Z * CS);
	for (const EDungeonStampMode Mode : { EDungeonStampMode::CarveUnderground, EDungeonStampMode::CarveOnly })
	{
		VoxelTesting::FTestChunkWorld W;
		W.Create(TEXT("DungeonFramePlanet"));
		W.ConfigureSmallPlanet(Radius);
		W.StartManager();
		const UVoxelEditManager* Edits = W.Manager->GetEditManager();
		const float Voxel = W.Config->VoxelSize;
		UDungeonVoxelStamper* Stamper = NewObject<UDungeonVoxelStamper>();
		const FDungeonStampResult Stamp = Stamper->StampDungeonInFrame(Result, W.Manager, FrameOffset, Mode, VoxelConfig, Frame);
		const TCHAR* ModeName = Mode == EDungeonStampMode::CarveOnly ? TEXT("CarveOnly") : TEXT("CarveUnderground");
		TestTrue(FString::Printf(TEXT("%s: the rotated stamp succeeds"), ModeName), Stamp.bSuccess && Edits != nullptr);
		if (!Edits)
		{
			W.Stop();
			continue;
		}

		// Rooms and hallways: air through the interior, the centre and the eight points 1.5 voxels inside the stone
		// lining's corners (CarveUnderground: the inner lining inside every lined face) or the cell's (CarveOnly). Stairs
		// and doors hold step and frame voxels.
		const float Lining = Mode == EDungeonStampMode::CarveOnly ? 0.0f : VoxelConfig->GetInnerLiningThickness(Voxel);
		const float Inset = CS * 0.5f - Lining - 1.5f * Voxel;
		int32 OpenCells = 0, InteriorProbes = 0, NotAir = 0;
		// Closed faces toward rock (the jacket never writes into another open cell), at the face's centre: the cell plane,
		// between the inner lining and the jacket's other layers (CarveUnderground: one voxel each side here, so the sample
		// nearest the plane, under sqrt(3)/2 voxel away in a rotated frame, lies in it), or the middle of the outer seal past
		// the carve margin (CarveOnly: WallThickness + 1 voxels beyond a margin of 0.5 + 0.5 voxels).
		const float SealMiddle = CS * 0.5f + Voxel + (VoxelConfig->WallThickness + 1) * Voxel * 0.5f;
		const float JacketMiddle = CS * 0.5f;
		int32 FaceProbes = 0, NotSolid = 0;
		for (int32 Z = 0; Z < Result.GridSize.Z; ++Z)
		{
			for (int32 Y = 0; Y < Result.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < Result.GridSize.X; ++X)
				{
					const FIntVector C(X, Y, Z);
					if (!IsOpen(Result, C))
					{
						continue;
					}
					++OpenCells;
					const FVector Centre = FrameOffset + (FVector(C) + FVector(0.5)) * CS;
					const EDungeonCellType Type = Result.Grid.GetCell(C).CellType;
					if (Type == EDungeonCellType::Room || Type == EDungeonCellType::Hallway)
					{
						TArray<FVector, TInlineAllocator<9>> Probes = { Centre };
						for (int32 Corner = 0; Corner < 8; ++Corner)
						{
							Probes.Add(Centre + FVector((Corner & 1) ? Inset : -Inset, (Corner & 2) ? Inset : -Inset, (Corner & 4) ? Inset : -Inset));
						}
						for (const FVector& P : Probes)
						{
							++InteriorProbes;
							const FVoxelEdit* Edit = EditAt(*Edits, *W.Config, Frame.TransformPosition(P));
							NotAir += (Edit && Edit->NewData.Density < VOXEL_SURFACE_THRESHOLD) ? 0 : 1;
						}
					}
					for (int32 Face = 0; Face < 6; ++Face)
					{
						if (!NeedsBoundary(Result, C, Face) || IsOpen(Result, C + FDungeonGrid::FaceDirections[Face]))
						{
							continue;
						}
						const FVector Normal(FDungeonGrid::FaceDirections[Face]);
						const FVector P = Centre + Normal * (Mode == EDungeonStampMode::CarveOnly ? SealMiddle : JacketMiddle);
						++FaceProbes;
						const FVoxelEdit* Edit = EditAt(*Edits, *W.Config, Frame.TransformPosition(P));
						NotSolid += (Edit && Edit->NewData.Density >= VOXEL_SURFACE_THRESHOLD) ? 0 : 1;
					}
				}
			}
		}

		// No edit outside the grid box and its shell (WallThickness + 1 voxels, the carve margin and a voxel's diagonal).
		const float Shell = (VoxelConfig->WallThickness + 3) * Voxel;
		const FBox GridBox(FrameOffset - FVector(Shell), FrameOffset + FVector(Result.GridSize) * CS + FVector(Shell));
		int32 EditCount = 0, Outside = 0;
		TArray<FIntVector> Chunks;
		Edits->GetEditedChunkCoords(Chunks);
		for (const FIntVector& Chunk : Chunks)
		{
			if (const FChunkEditLayer* Layer = Edits->GetEditLayer(Chunk))
			{
				for (const TPair<int32, FVoxelEdit>& Pair : Layer->Edits)
				{
					++EditCount;
					const FVector World = SamplePositionOf(*W.Config, Chunk, Pair.Value.LocalPosition);
					Outside += GridBox.IsInside(Frame.InverseTransformPosition(World)) ? 0 : 1;
				}
			}
		}

		AddInfo(FString::Printf(TEXT("%s on the planet: %d edits (%d modified), %d open cells, %d interior probes (%d not air), %d face probes (%d not solid), %d edits outside the shell"),
			ModeName, EditCount, Stamp.VoxelsModified, OpenCells, InteriorProbes, NotAir, FaceProbes, NotSolid, Outside));
		TestTrue(FString::Printf(TEXT("%s: open cells and faces to probe (non-vacuous)"), ModeName), OpenCells > 0 && FaceProbes > 0);
		TestEqual(FString::Printf(TEXT("%s: open cells are air through their interiors"), ModeName), NotAir, 0);
		TestEqual(FString::Printf(TEXT("%s: every closed face is lined or sealed"), ModeName), NotSolid, 0);
		TestEqual(FString::Printf(TEXT("%s: no edit outside the grid box and its shell"), ModeName), Outside, 0);
		W.Stop();
	}

	Config->RemoveFromRoot();
	VoxelConfig->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStitchPlanetSurfaceTest, "Dungeon.Stitch.PlanetSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStitchPlanetSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace DungeonFrameTest;
	UDungeonConfiguration* Config = MakeConfig();
	UDungeonVoxelConfig* VoxelConfig = MakeVoxelConfig();
	const FDungeonResult Result = Generate(Config);
	if (!TestTrue(TEXT("The dungeon generates with its entrance from above (non-vacuous)"),
		Result.Rooms.Num() > 0 && Result.EntranceApproach.bSatisfied))
	{
		Config->RemoveFromRoot();
		VoxelConfig->RemoveFromRoot();
		return false;
	}
	const float CS = Result.CellWorldSize;

	// ---- The identity frame is StitchEntrance exactly (vertical shaft, tile-dressed and voxel-carved stops) ----
	const FVector PlaneOffset(-(Result.EntranceCell.X + 0.5f) * CS + 37.2f, -(Result.EntranceCell.Y + 0.5f) * CS - 11.9f,
		-2000.0f - Result.GridSize.Z * CS);
	for (const bool bStopAtTop : { true, false })
	{
		TMap<FIntVector, TMap<int32, FEditKey>> Edits[2];
		int32 Voxels[2] = { 0, 0 };
		for (int32 Run = 0; Run < 2; ++Run)
		{
			VoxelTesting::FTestChunkWorld W;
			W.Create(Run == 0 ? TEXT("DungeonFrameStitchA") : TEXT("DungeonFrameStitchB"));
			W.ConfigureSmallPlane();
			W.StartManager();
			UDungeonEntranceStitcher* Stitcher = NewObject<UDungeonEntranceStitcher>();
			Voxels[Run] = Stitcher->StitchEntranceInFrame(Result, W.Manager, PlaneOffset, EDungeonEntranceStyle::VerticalShaft, VoxelConfig,
				bStopAtTop, 0.0f, FTransform::Identity);
			if (const UVoxelEditManager* EditManager = W.Manager->GetEditManager())
			{
				Edits[Run] = CollectEdits(*EditManager);
			}
			W.Stop();
		}
		AddInfo(FString::Printf(TEXT("Stop at the lid %d: %d voxels stitched with StitchEntrance"), bStopAtTop ? 1 : 0, Voxels[0]));
		TestTrue(FString::Printf(TEXT("Stop at the lid %d: the shaft is carved (non-vacuous)"), bStopAtTop ? 1 : 0), Voxels[0] > 100);
		TestEqual(FString::Printf(TEXT("Stop at the lid %d: the same voxel count"), bStopAtTop ? 1 : 0), Voxels[1], Voxels[0]);
		TestEqual(FString::Printf(TEXT("Stop at the lid %d: the identity frame writes exactly StitchEntrance's edits"), bStopAtTop ? 1 : 0),
			CountDifferences(Edits[0], Edits[1]), 0);
	}

	// ---- A tangent frame on a small planet: the shaft runs along the frame's z up to the generated surface ----
	const double Radius = 20000.0;
	const FTransform Frame = TangentFrame(-35.0, 140.0, Radius);
	VoxelTesting::FTestChunkWorld W;
	W.Create(TEXT("DungeonFrameStitchPlanet"));
	W.ConfigureSmallPlanet(Radius);
	W.StartManager();
	const UVoxelEditManager* Edits = W.Manager->GetEditManager();
	const IVoxelWorldMode* WorldMode = W.Manager->GetWorldMode();
	const float Voxel = W.Config->VoxelSize;
	if (TestTrue(TEXT("The planet's edit manager and world mode"), Edits != nullptr && WorldMode != nullptr))
	{
		// The mouth at the frame's origin; the grid top 900 under the surface there.
		const FVector Datum = Frame.GetLocation();
		const float SurfaceAltitude = static_cast<float>(WorldMode->GetAltitude(W.Manager->GetGeneratedSurfacePoint(Datum)));
		const FVector FrameOffset(-(Result.EntranceCell.X + 0.5f) * CS, -(Result.EntranceCell.Y + 0.5f) * CS,
			SurfaceAltitude - 900.0f - Result.GridSize.Z * CS);
		UDungeonEntranceStitcher* Stitcher = NewObject<UDungeonEntranceStitcher>();
		const int32 Stitched = Stitcher->StitchEntranceInFrame(Result, W.Manager, FrameOffset, EDungeonEntranceStyle::VerticalShaft,
			VoxelConfig, /*bStopAtEntranceCellTop=*/true, 0.0f, Frame);

		// Along the frame's z through the mouth: air from a voxel over the entrance room's lid up to a voxel under the
		// surface the line meets (its frame height: where its altitude equals the generated surface's along local up).
		const float LidZ = FrameOffset.Z + (Result.GetEntranceOpeningCell().Z + 1) * CS;
		double SurfaceZ = 0.0;
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const FVector P = Frame.TransformPosition(FVector(0.0, 0.0, SurfaceZ));
			SurfaceZ -= WorldMode->GetAltitude(P) - WorldMode->GetAltitude(W.Manager->GetGeneratedSurfacePoint(P));
		}
		int32 Probes = 0, NotAir = 0;
		for (float Z = LidZ + Voxel; Z <= SurfaceZ - Voxel; Z += 0.5f * Voxel)
		{
			++Probes;
			const FVoxelEdit* Edit = EditAt(*Edits, *W.Config, Frame.TransformPosition(FVector(0.0f, 0.0f, Z)));
			NotAir += (Edit && Edit->NewData.Density < VOXEL_SURFACE_THRESHOLD) ? 0 : 1;
		}
		// And nothing carved along it more than three voxels over the surface.
		int32 AboveCarved = 0;
		for (float Z = SurfaceZ + 3.0f * Voxel; Z <= SurfaceZ + 20.0f * Voxel; Z += 0.5f * Voxel)
		{
			const FVoxelEdit* Edit = EditAt(*Edits, *W.Config, Frame.TransformPosition(FVector(0.0f, 0.0f, Z)));
			AboveCarved += Edit ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("Planet shaft: %d voxels stitched, lid at frame z %.0f, surface at %.1f (altitude %.1f at the datum), %d probes (%d not air), %d edits over the surface"),
			Stitched, LidZ, SurfaceZ, SurfaceAltitude, Probes, NotAir, AboveCarved));
		TestTrue(TEXT("The planet shaft is carved (non-vacuous)"), Stitched > 100 && Probes > 10);
		TestEqual(TEXT("Air along the frame's z from the lid to the surface"), NotAir, 0);
		TestEqual(TEXT("The shaft stops at the surface"), AboveCarved, 0);
	}
	W.Stop();

	Config->RemoveFromRoot();
	VoxelConfig->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
