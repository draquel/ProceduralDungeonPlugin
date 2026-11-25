#include "DungeonVoxelStamper.h"
#include "DungeonVoxelStampPlan.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelIntegrationModule.h"
#include "DungeonTypes.h"
#include "DungeonBoundaryRules.h"
#include "VoxelData.h"
#include "VoxelCoordinates.h"
#include "VoxelEditManager.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// ============================================================================
// The stamp's passes
// ============================================================================

// Which faces of a carved cell get a solid shell is decided by FDungeonBoundaryRules
// (DungeonCore), the same predicates the tile mapper uses, so tiles and voxels always agree.
//
// The passes below are the one description of what a stamp writes. They hand each write (a lattice sample and its voxel)
// to a sink: the one-frame stamp's sink writes it into the edit manager at once (StampDungeonInFrame), a plan's sink keeps
// it (PlanStampInFrame, G280), so both stamps write the same voxels in the same order.

namespace DungeonStampPasses
{
	/** The world-derived numbers of one stamp. */
	struct FSetup
	{
		FDungeonStampWorldLayout Layout;
		FDungeonVoxelLattice Lattice;
		float VoxelSize = 100.0f;
		float CellWorldSize = 400.0f;
		int32 VoxelsPerCell = 4;
		uint8 BiomeID = 0;
		bool bCarveOnly = false;
		float LiningWorldThickness = 0.0f;
		float SealWorldThickness = 0.0f;
		float CarveMargin = 0.0f;
		EDungeonStampMode StampMode = EDungeonStampMode::CarveUnderground;
	};

	/** The chunk a frame-space point falls in: the chunk manager's and the edit manager's WorldToChunkCoord. */
	static FIntVector ChunkOf(const FSetup& S, const FVector& FramePoint)
	{
		return FVoxelCoordinates::WorldToChunk(S.Lattice.ToWorld(FramePoint) - S.Layout.WorldOrigin, S.Layout.ChunkSize, S.Layout.VoxelSize);
	}

	static EDungeonRoomType GetRoomTypeForCell(const FDungeonCell& Cell, const FDungeonResult& Result)
	{
		if (Cell.RoomIndex > 0 && Cell.RoomIndex <= static_cast<uint8>(Result.Rooms.Num()))
		{
			// Rooms array is 0-indexed, RoomIndex is 1-based
			return Result.Rooms[Cell.RoomIndex - 1].RoomType;
		}
		return EDungeonRoomType::Generic;
	}

	/** Work out a stamp's numbers from the world's layout and the configuration. */
	static FSetup MakeSetup(const FDungeonResult& Result, const FDungeonStampWorldLayout& Layout, EDungeonStampMode StampMode,
		const UDungeonVoxelConfig& Config, const FTransform& Frame)
	{
		FSetup S;
		S.Layout = Layout;
		S.StampMode = StampMode;
		S.VoxelSize = Layout.VoxelSize;
		S.CellWorldSize = Result.CellWorldSize;
		S.VoxelsPerCell = Config.GetEffectiveVoxelsPerCell(S.CellWorldSize, S.VoxelSize);
		S.BiomeID = Config.DungeonBiomeID;

		// Every voxel write below resolves through this lattice rather than stepping in cell space:
		// the dungeon origin has an arbitrary phase against the voxel grid and CellWorldSize is not
		// generally a multiple of VoxelSize, so cell-space stepping leaves an uncarved rind. Cells,
		// slabs and stairs are laid out in the dungeon's frame (identity: the world; a planet POI's
		// pad space, spherical Phase 5), which the lattice maps samples into.
		S.Lattice = FDungeonVoxelLattice(Layout.WorldOrigin, S.VoxelSize);
		S.Lattice.SetFrame(Frame);

		// A plane at an angle to the lattice aliases by up to half a voxel's diagonal (sqrt(3)/2 of a
		// voxel) where the axis-aligned lattice resolves it to a plane within half a voxel: a rotated
		// tile-dressed dungeon gets one more voxel of outer seal and half a voxel more carve margin, so
		// the rock stays behind the tiles. The voxel lining keeps its thickness: its surface is the
		// floor content stands on, and a slab a voxel thick has a sample on every lattice line through it.
		const float RotatedExtra = S.Lattice.bRotated ? 1.0f : 0.0f;

		// The stone jacket around every closed face, WallThickness voxel layers (authored in layers). CarveOnly: all of it is
		// the outer seal (the tiles are the lining). Voxel-lined modes: the inner lining stands inside the face (its surface is
		// the floor, wall or ceiling) and the rest of the jacket wraps the face from outside, never into an open cell. Lining
		// every layer from inside filled each one-storey cell once the jacket reached half the cell (G24).
		S.bCarveOnly = (StampMode == EDungeonStampMode::CarveOnly);
		S.LiningWorldThickness = S.bCarveOnly ? 0.0f : Config.GetInnerLiningThickness(S.VoxelSize);
		S.SealWorldThickness = S.bCarveOnly
			? (Config.WallThickness + RotatedExtra) * S.VoxelSize
			: Config.GetOuterJacketLayers() * S.VoxelSize;

		// CarveOnly only: the void is carved CarveMargin past every cell plane so the meshed rock
		// surface (midway between the last carved sample and the first solid one) can never stand
		// inside the cell behind the tiles. The voxel-lined modes keep the exact cell box — their
		// stone lining IS the wall. The open-sample set and the seal slabs use the same expanded box.
		S.CarveMargin = S.bCarveOnly
			? (FMath::Clamp(Config.CarveMarginVoxels, 0.0f, 3.0f) + 0.5f * RotatedExtra) * S.VoxelSize
			: 0.0f;
		return S;
	}

	/** Carve a single cell's voxel volume to air. Returns number of voxels written. */
	template <typename FWrite>
	static int32 CarveCell(const FSetup& S, const FVector& CellWorldMin, float CellWorldSize, FWrite& Write)
	{
		int32 Count = 0;
		const FVoxelData AirVoxel = FVoxelData::Air();
		// The cell box is in the dungeon's frame (the world for an unrotated dungeon).
		S.Lattice.ForEachSampleInBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), [&](const FIntVector& Index)
		{
			Count += Write(Index, AirVoxel) ? 1 : 0;
		});
		return Count;
	}

	/**
	 * Place boundary voxels on a cell face, INSIDE the open cell (the stone lining that makes a
	 * VoxelCarved dungeon read as rock; UDungeonVoxelConfig::GetInnerLiningThickness deep, the rest
	 * of the jacket goes outside through PlaceOuterSeal). Returns number of voxels written.
	 */
	template <typename FWrite>
	static int32 PlaceBoundary(const FSetup& S, const FVector& CellWorldMin, int32 Face, uint8 MaterialID, FWrite& Write)
	{
		int32 Count = 0;
		const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, S.BiomeID);
		FVector BoxMin, BoxMax;
		FDungeonVoxelStampPlan::FaceSlabBox(CellWorldMin, S.CellWorldSize, Face, S.LiningWorldThickness, /*bOutward=*/false, BoxMin, BoxMax);
		S.Lattice.ForEachSampleInBox(BoxMin, BoxMax, [&](const FIntVector& Index)
		{
			Count += Write(Index, SolidVoxel) ? 1 : 0;
		});
		return Count;
	}

	/**
	 * Place seal voxels on a cell face, OUTSIDE the open cell.
	 *
	 * CarveOnly dungeons are lined by tile meshes, so the inward boundary pass is skipped — but
	 * that also removed the only thing asserting the surrounding voxel field is solid. Dungeons
	 * are deliberately anchored at the cave layer, so procedural cave voids intersect the volume
	 * and breach it: terrain reads straight through the thin tiles. This writes the shell just
	 * outside each boundary face instead, sealing the volume without occupying the cell where the
	 * tiles stand. The voxel-lined modes write the jacket's layers past their inner lining here too.
	 *
	 * Which lattice samples the slab writes is decided by FDungeonVoxelStampPlan: every sample
	 * that Pass 1 carved (any open cell) is excluded by index, so a seal can never plug a
	 * neighbouring room or hallway, nor re-solidify a layer of the cell it belongs to. The six face
	 * slabs of a cell overlap at its edges and corners and neighbouring cells re-seal each other's
	 * shells, so SealedVoxels (the samples already sealed) keeps each shell voxel to one write.
	 */
	template <typename FWrite>
	static int32 PlaceOuterSeal(const FSetup& S, const FVector& CellWorldMin, float CellWorldSize, int32 Face, uint8 MaterialID,
		const TSet<FIntVector>& OpenCellSamples, TSet<FIntVector>& SealedVoxels, FWrite& Write)
	{
		int32 Count = 0;
		const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, S.BiomeID);
		TArray<FIntVector> ToWrite;
		FDungeonVoxelStampPlan::CollectOuterSealSamples(
			S.Lattice, CellWorldMin, CellWorldSize, Face, S.SealWorldThickness, OpenCellSamples, SealedVoxels, ToWrite);
		for (const FIntVector& Index : ToWrite)
		{
			Count += Write(Index, SolidVoxel) ? 1 : 0;
		}
		return Count;
	}

	/** Staircase step geometry inside a staircase's body cells. Returns number of voxels written. */
	template <typename FWrite>
	static int32 PlaceStaircaseSteps(const FSetup& S, const FDungeonStaircase& Staircase, const FVector& WorldOffset, uint8 MaterialID,
		TSet<FIntVector>& AffectedChunks, FWrite& Write)
	{
		int32 Count = 0;
		const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, S.BiomeID);
		const int32 StepsPerCell = S.VoxelsPerCell;
		const float CellWorldSize = S.CellWorldSize;

		const int32 RiseToRun = FMath::Max(1, Staircase.RiseRunRatio);

		// Climb direction: 0=+X, 1=-X, 2=+Y, 3=-Y
		static const int32 CDX[] = {1, -1, 0, 0};
		static const int32 CDY[] = {0, 0, 1, -1};
		const int32 ClimbDX = CDX[Staircase.Direction];
		const int32 ClimbDY = CDY[Staircase.Direction];

		// Determine climb axis and sign
		const bool bClimbAlongX = (ClimbDX != 0);
		const bool bPositiveClimb = bClimbAlongX ? (ClimbDX > 0) : (ClimbDY > 0);

		// The tread/riser grid stays defined in steps-per-cell (the authored stair profile); only the
		// voxels realising it are resolved on the lattice, so the profile survives a CellWorldSize
		// that is not an integer multiple of VoxelSize.
		const double StepWorldSize = static_cast<double>(CellWorldSize) / StepsPerCell;

		// Iterate body cells in climb order (ci=0 is nearest entry)
		for (int32 ci = 0; ci < RiseToRun; ++ci)
		{
			const FIntVector CellCoord(
				Staircase.BottomCell.X + ClimbDX * (ci + 1),
				Staircase.BottomCell.Y + ClimbDY * (ci + 1),
				Staircase.BottomCell.Z);

			const FVector CellWorldMin = WorldOffset + FVector(CellCoord) * CellWorldSize;

			S.Lattice.ForEachSampleInBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), [&](const FIntVector& Index)
			{
				// Measured in the dungeon's frame (the world for an unrotated dungeon).
				const FVector FramePos = S.Lattice.FrameEditPosition(Index);

				// Distance along the climb axis measured from the entry side of the cell.
				const double AlongAxis = bClimbAlongX
					? FramePos.X - CellWorldMin.X
					: FramePos.Y - CellWorldMin.Y;
				const double AlongFromEntry = bPositiveClimb
					? AlongAxis
					: CellWorldSize - AlongAxis;

				const int32 RunWithinCell = FMath::Clamp(
					FMath::FloorToInt32(AlongFromEntry / StepWorldSize), 0, StepsPerCell - 1);
				const int32 GlobalRunIdx = ci * StepsPerCell + RunWithinCell;

				// Step height: rises one tread every RiseToRun runs.
				const int32 StepTop = GlobalRunIdx / RiseToRun;
				const double StepTopZ = CellWorldMin.Z + (StepTop + 1) * StepWorldSize;

				if (FramePos.Z >= StepTopZ)
				{
					return;
				}
				Count += Write(Index, SolidVoxel) ? 1 : 0;
			});

			AffectedChunks.Add(ChunkOf(S, CellWorldMin + FVector(CellWorldSize * 0.5f)));
		}
		return Count;
	}

	/**
	 * Every write of a stamp, in order: the open cells carved, then each closed face's lining and seal, then the
	 * staircases' steps.
	 * @param Write          The sink, bool(const FIntVector& Index, const FVoxelData& Voxel): whether the voxel was written
	 * @param AffectedChunks Receives the chunks of the cells' centres (the chunks the stamp marks dirty)
	 * @return The writes the sink accepted
	 */
	template <typename FWrite>
	static int32 Run(const FDungeonResult& Result, const FSetup& S, const FVector& WorldOffset, const UDungeonVoxelConfig& Config,
		FWrite& Write, TSet<FIntVector>& AffectedChunks)
	{
		const FDungeonGrid& Grid = Result.Grid;
		const float CellWorldSize = S.CellWorldSize;
		const float CarveMargin = S.CarveMargin;
		int32 VoxelsModified = 0;

		// ------------------------------------------------------------------
		// Pass 1: Carve all open cells to air
		// ------------------------------------------------------------------
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_Carve);
			for (int32 GZ = 0; GZ < Grid.GridSize.Z; ++GZ)
			{
				for (int32 GY = 0; GY < Grid.GridSize.Y; ++GY)
				{
					for (int32 GX = 0; GX < Grid.GridSize.X; ++GX)
					{
						const FDungeonCell& Cell = Grid.GetCell(GX, GY, GZ);
						if (!FDungeonBoundaryRules::IsOpenCell(Cell.CellType))
						{
							continue;
						}
						const FVector CellWorldMin = WorldOffset + FVector(GX, GY, GZ) * CellWorldSize;
						VoxelsModified += CarveCell(S, CellWorldMin - FVector(CarveMargin), CellWorldSize + 2.0f * CarveMargin, Write);
						// Track chunk for the cell center
						AffectedChunks.Add(ChunkOf(S, CellWorldMin + FVector(CellWorldSize * 0.5f)));
					}
				}
			}
		}

		// ------------------------------------------------------------------
		// Pass 2: Place boundary voxels on faces adjacent to solid/OOB: the inner
		// lining inside the cell (voxel-lined modes only — in CarveOnly tile meshes
		// provide the lining, and boundary voxels inside the cell would bury them)
		// and the rest of the jacket outside it
		// ------------------------------------------------------------------

		// The jacket outside the cell. CarveOnly is lined by tile meshes, so its whole jacket goes outside
		// (inside the cell it would bury the tiles); it still needs one: dungeons anchor at the cave layer,
		// and without solid voxels asserted around the volume, procedural cave voids breach it and terrain
		// reads through the tiles. The voxel-lined modes put every layer past the inner lining there too,
		// sealing and armouring the dungeon without taking the room's interior.
		const bool bOuterSeal = S.SealWorldThickness > 0.0f;

		// Shared across every seal slab so each shell voxel is written exactly once.
		TSet<FIntVector> SealedVoxels;

		// Every sample Pass 1 carved, by lattice index. The seal slabs reach into the void (lateral
		// widening at edges and corners, and faces shared with another open cell lie entirely inside
		// it), and a sample that belongs to any open cell must never be written solid again.
		TSet<FIntVector> OpenCellSamples;
		if (bOuterSeal)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_OpenSamples);
			SealedVoxels.Reserve(Grid.Cells.Num() * 4);
			FDungeonVoxelStampPlan::CollectOpenCellSamples(Grid, S.Lattice, WorldOffset, CellWorldSize, CarveMargin, OpenCellSamples);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_Boundary);
			for (int32 GZ = 0; GZ < Grid.GridSize.Z; ++GZ)
			{
				for (int32 GY = 0; GY < Grid.GridSize.Y; ++GY)
				{
					for (int32 GX = 0; GX < Grid.GridSize.X; ++GX)
					{
						const FDungeonCell& Cell = Grid.GetCell(GX, GY, GZ);
						if (!FDungeonBoundaryRules::IsOpenCell(Cell.CellType))
						{
							continue;
						}

						const FVector CellWorldMin = WorldOffset + FVector(GX, GY, GZ) * CellWorldSize;
						const EDungeonRoomType RoomType = GetRoomTypeForCell(Cell, Result);

						for (int32 Face = 0; Face < 6; ++Face)
						{
							const FIntVector& Dir = FDungeonGrid::FaceDirections[Face];
							const int32 NX = GX + Dir.X;
							const int32 NY = GY + Dir.Y;
							const int32 NZ = GZ + Dir.Z;

							const bool bNeedsBoundary = Face < 4
								? FDungeonBoundaryRules::NeedsWall(Grid, FIntVector(GX, GY, GZ), NX, NY, NZ)
								: FDungeonBoundaryRules::NeedsVerticalBoundary(Grid, FIntVector(GX, GY, GZ), NX, NY, NZ);
							if (!bNeedsBoundary)
							{
								continue;
							}

							const uint8 MatID = Config.GetMaterialForCell(Cell.CellType, RoomType, Face);
							if (S.LiningWorldThickness > 0.0f)
							{
								VoxelsModified += PlaceBoundary(S, CellWorldMin, Face, MatID, Write);
							}
							if (bOuterSeal)
							{
								VoxelsModified += PlaceOuterSeal(S, CellWorldMin - FVector(CarveMargin), CellWorldSize + 2.0f * CarveMargin,
									Face, MatID, OpenCellSamples, SealedVoxels, Write);
							}

							// Track chunk for boundary cell too
							AffectedChunks.Add(ChunkOf(S, CellWorldMin + FVector(CellWorldSize * 0.5f)));
						}
					}
				}
			}
		}

		// ------------------------------------------------------------------
		// Pass 3: Place staircase step geometry inside body cells
		// (skipped in CarveOnly — the tile ramp IS the staircase, and solid voxels written over the
		// exact stair box re-fill the carve margin of every open neighbour, so the meshed rock then
		// stands up to VoxelSize / 2 INSIDE the room or hall alongside, through its partition)
		// ------------------------------------------------------------------
		if (S.StampMode != EDungeonStampMode::CarveOnly)
		{
			for (const FDungeonStaircase& Staircase : Result.Staircases)
			{
				VoxelsModified += PlaceStaircaseSteps(S, Staircase, WorldOffset, Config.StaircaseMaterialID, AffectedChunks, Write);
			}
		}
		return VoxelsModified;
	}

	/** Why a stamp cannot run, or empty when it can. */
	static FString CheckInputs(const FDungeonResult& Result, const UDungeonVoxelConfig* Config, const FDungeonStampWorldLayout& Layout)
	{
		if (!Config)
		{
			return TEXT("Config is null");
		}
		if (Result.Grid.Cells.Num() == 0)
		{
			return TEXT("Dungeon grid is empty");
		}
		if (Layout.VoxelSize <= 0.0f || Layout.ChunkSize <= 0)
		{
			return TEXT("The voxel world's layout is invalid");
		}
		return FString();
	}

	static void LogSetup(const FDungeonResult& Result, const FSetup& S, const TCHAR* What)
	{
		const FDungeonGrid& Grid = Result.Grid;
		UE_LOG(LogDungeonVoxelIntegration, Log,
			TEXT("%s: Grid=%dx%dx%d CellWorldSize=%.1f VoxelSize=%.1f VoxelsPerCell=%d Mode=%d lining=%.0f seal=%.0f%s"),
			What, Grid.GridSize.X, Grid.GridSize.Y, Grid.GridSize.Z,
			S.CellWorldSize, S.VoxelSize, S.VoxelsPerCell, static_cast<int32>(S.StampMode), S.LiningWorldThickness, S.SealWorldThickness,
			S.Lattice.bRotated ? TEXT(" (in a rotated frame)") : TEXT(""));

		// A one-storey cell keeps an air sample between its floor and ceiling linings only if it spans three voxels.
		if (!S.bCarveOnly && S.CellWorldSize < 2.0f * S.LiningWorldThickness + S.VoxelSize)
		{
			UE_LOG(LogDungeonVoxelIntegration, Warning,
				TEXT("%s: CellWorldSize %.1f is under two linings and a voxel (%.1f): one-storey rooms and hallways may stamp solid."),
				What, S.CellWorldSize, 2.0f * S.LiningWorldThickness + S.VoxelSize);
		}
	}
}

// ============================================================================
// The world's layout
// ============================================================================

bool UDungeonVoxelStamper::GetWorldLayout(const UVoxelChunkManager* ChunkManager, FDungeonStampWorldLayout& OutLayout)
{
	const UVoxelWorldConfiguration* VoxelConfig = ChunkManager ? ChunkManager->GetConfiguration() : nullptr;
	if (!VoxelConfig)
	{
		return false;
	}
	OutLayout.WorldOrigin = VoxelConfig->WorldOrigin;
	OutLayout.VoxelSize = VoxelConfig->VoxelSize;
	OutLayout.ChunkSize = VoxelConfig->ChunkSize;
	return true;
}

// ============================================================================
// The one-frame stamp
// ============================================================================

FDungeonStampResult UDungeonVoxelStamper::StampDungeonInFrame(
	const FDungeonResult& Result,
	UVoxelChunkManager* ChunkManager,
	const FVector& WorldOffset,
	EDungeonStampMode StampMode,
	UDungeonVoxelConfig* Config,
	const FTransform& Frame)
{
	FDungeonStampResult StampResult;
	const double StartTime = FPlatformTime::Seconds();

	// Validate inputs
	if (!ChunkManager)
	{
		StampResult.ErrorMessage = TEXT("ChunkManager is null");
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StampDungeon: %s"), *StampResult.ErrorMessage);
		return StampResult;
	}
	UVoxelEditManager* EditManager = ChunkManager->GetEditManager();
	if (!EditManager)
	{
		StampResult.ErrorMessage = TEXT("EditManager is null");
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StampDungeon: %s"), *StampResult.ErrorMessage);
		return StampResult;
	}
	FDungeonStampWorldLayout Layout;
	if (!GetWorldLayout(ChunkManager, Layout))
	{
		StampResult.ErrorMessage = TEXT("VoxelWorldConfiguration is null");
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StampDungeon: %s"), *StampResult.ErrorMessage);
		return StampResult;
	}
	StampResult.ErrorMessage = DungeonStampPasses::CheckInputs(Result, Config, Layout);
	if (!StampResult.ErrorMessage.IsEmpty())
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StampDungeon: %s"), *StampResult.ErrorMessage);
		return StampResult;
	}

	const DungeonStampPasses::FSetup Setup = DungeonStampPasses::MakeSetup(Result, Layout, StampMode, *Config, Frame);
	DungeonStampPasses::LogSetup(Result, Setup, TEXT("StampDungeon"));

	// Track affected chunks for dirty marking
	TSet<FIntVector> AffectedChunks;

	// Begin grouped edit operation. The world's own generation, outside the undo history (world modes review G68): a
	// player's undo takes back the player's last edit, never the dungeon, and the history does not hold the stamp's voxel
	// changes (112 bytes each: hundreds of thousands per dungeon).
	EditManager->BeginEditOperation(TEXT("Dungeon Stamp"), /*bAddToUndoHistory=*/false);
	EditManager->SetEditSource(EEditSource::System);

	// Each write goes into the edit manager as the passes make it.
	const FDungeonVoxelLattice& Lattice = Setup.Lattice;
	auto Write = [EditManager, &Lattice](const FIntVector& Index, const FVoxelData& Voxel)
	{
		return EditManager->ApplyEdit(Lattice.EditPosition(Index), Voxel, EEditMode::Set);
	};
	StampResult.VoxelsModified = DungeonStampPasses::Run(Result, Setup, WorldOffset, *Config, Write, AffectedChunks);

	// End edit operation
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_EndOperation);
		EditManager->EndEditOperation();
	}

	// Mark all affected chunks dirty for remeshing
	for (const FIntVector& ChunkCoord : AffectedChunks)
	{
		ChunkManager->MarkChunkDirty(ChunkCoord);
	}

	StampResult.ChunksAffected = AffectedChunks.Num();
	StampResult.bSuccess = true;
	StampResult.StampTimeMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StampDungeon: Complete — %d voxels modified, %d chunks affected, %.1fms"),
		StampResult.VoxelsModified, StampResult.ChunksAffected, StampResult.StampTimeMs);

	return StampResult;
}

// ============================================================================
// The planned stamp (G280)
// ============================================================================

bool UDungeonVoxelStamper::PlanStampInFrame(
	const FDungeonResult& Result,
	const FDungeonStampWorldLayout& Layout,
	const FVector& WorldOffset,
	EDungeonStampMode StampMode,
	const UDungeonVoxelConfig* Config,
	const FTransform& Frame,
	FDungeonPlannedStamp& OutPlan)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_Plan);
	OutPlan = FDungeonPlannedStamp();
	const double StartTime = FPlatformTime::Seconds();
	OutPlan.ErrorMessage = DungeonStampPasses::CheckInputs(Result, Config, Layout);
	if (!OutPlan.ErrorMessage.IsEmpty())
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("PlanStamp: %s"), *OutPlan.ErrorMessage);
		return false;
	}

	const DungeonStampPasses::FSetup Setup = DungeonStampPasses::MakeSetup(Result, Layout, StampMode, *Config, Frame);
	DungeonStampPasses::LogSetup(Result, Setup, TEXT("PlanStamp"));
	OutPlan.Layout = Layout;
	OutPlan.Lattice = Setup.Lattice;

	// Each voxel once, with its last write's value: every write is a Set, which replaces whatever the voxel held, so the
	// last one is the voxel the one-frame stamp leaves. Slots keep the order the voxels were first reached in.
	TArray<FDungeonStampWrite> Slots;
	Slots.Reserve(Result.Grid.Cells.Num() * 64);
	TMap<FIntVector, int32> SlotOf;
	SlotOf.Reserve(Result.Grid.Cells.Num() * 64);
	auto Write = [&Slots, &SlotOf](const FIntVector& Index, const FVoxelData& Voxel)
	{
		FDungeonStampWrite Entry;
		Entry.Index = Index;
		Entry.PackedVoxel = Voxel.Pack();
		if (const int32* Slot = SlotOf.Find(Index))
		{
			Slots[*Slot] = Entry;
		}
		else
		{
			SlotOf.Add(Index, Slots.Add(Entry));
		}
		return true;
	};
	TSet<FIntVector> AffectedChunks;
	OutPlan.VoxelWrites = DungeonStampPasses::Run(Result, Setup, WorldOffset, *Config, Write, AffectedChunks);
	OutPlan.Voxels = Slots.Num();

	// By chunk: the chunk the edit manager resolves each write's edit position to. Chunks in coordinate order (Z, Y, X),
	// and in each the voxels in the order they were first reached.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_PlanByChunk);
		TMap<FIntVector, int32> ChunkSlot;
		for (const FDungeonStampWrite& Entry : Slots)
		{
			const FIntVector Chunk = FVoxelCoordinates::WorldToChunk(
				Setup.Lattice.EditPosition(Entry.Index) - Layout.WorldOrigin, Layout.ChunkSize, Layout.VoxelSize);
			int32* Slot = ChunkSlot.Find(Chunk);
			if (!Slot)
			{
				Slot = &ChunkSlot.Add(Chunk, OutPlan.Chunks.Num());
				OutPlan.Chunks.AddDefaulted_GetRef().ChunkCoord = Chunk;
			}
			OutPlan.Chunks[*Slot].Writes.Add(Entry);
		}
		auto ChunkOrder = [](const FIntVector& A, const FIntVector& B)
		{
			return A.Z != B.Z ? A.Z < B.Z : (A.Y != B.Y ? A.Y < B.Y : A.X < B.X);
		};
		OutPlan.Chunks.Sort([&ChunkOrder](const FDungeonStampChunkWrites& A, const FDungeonStampChunkWrites& B)
		{
			return ChunkOrder(A.ChunkCoord, B.ChunkCoord);
		});
		OutPlan.DirtyChunks = AffectedChunks.Array();
		OutPlan.DirtyChunks.Sort(ChunkOrder);
	}

	OutPlan.bValid = true;
	OutPlan.PlanTimeMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
	UE_LOG(LogDungeonVoxelIntegration, Log, TEXT("PlanStamp: %d writes, %d voxels in %d chunks, %.1fms"),
		OutPlan.VoxelWrites, OutPlan.Voxels, OutPlan.Chunks.Num(), OutPlan.PlanTimeMs);
	return true;
}

int32 UDungeonVoxelStamper::ApplyPlannedChunks(UVoxelEditManager* EditManager, const FDungeonPlannedStamp& Plan, int32 FirstChunk, int32 NumChunks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_ApplyChunks);
	if (!EditManager || !Plan.bValid)
	{
		return 0;
	}
	const int32 End = FMath::Min(FirstChunk + NumChunks, Plan.Chunks.Num());
	if (FirstChunk < 0 || FirstChunk >= End)
	{
		return 0;
	}

	// One operation for the slice, outside the undo history as the one-frame stamp's (G68). Its chunks are whole: every
	// voxel the stamp writes in a chunk is in the slice that applies it, so no chunk is ever meshed or cooked half stamped.
	EditManager->BeginEditOperation(TEXT("Dungeon Stamp"), /*bAddToUndoHistory=*/false);
	EditManager->SetEditSource(EEditSource::System);
	int32 Count = 0;
	for (int32 ChunkIndex = FirstChunk; ChunkIndex < End; ++ChunkIndex)
	{
		for (const FDungeonStampWrite& Entry : Plan.Chunks[ChunkIndex].Writes)
		{
			Count += EditManager->ApplyEdit(Plan.Lattice.EditPosition(Entry.Index), FVoxelData::Unpack(Entry.PackedVoxel), EEditMode::Set) ? 1 : 0;
		}
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DungeonStamp_EndOperation);
		EditManager->EndEditOperation();
	}
	return Count;
}

void UDungeonVoxelStamper::MarkPlannedChunksDirty(UVoxelChunkManager* ChunkManager, const FDungeonPlannedStamp& Plan)
{
	if (!ChunkManager)
	{
		return;
	}
	for (const FIntVector& ChunkCoord : Plan.DirtyChunks)
	{
		ChunkManager->MarkChunkDirty(ChunkCoord);
	}
}
