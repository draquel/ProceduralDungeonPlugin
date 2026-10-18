#include "DungeonVoxelStamper.h"
#include "DungeonVoxelStampPlan.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelIntegration.h"
#include "DungeonTypes.h"
#include "DungeonBoundaryRules.h"
#include "VoxelData.h"
#include "VoxelEditManager.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"

// ============================================================================
// Helpers
// ============================================================================

// Which faces of a carved cell get a solid shell is decided by FDungeonBoundaryRules
// (DungeonCore), the same predicates the tile mapper uses, so tiles and voxels always agree.

EDungeonRoomType UDungeonVoxelStamper::GetRoomTypeForCell(const FDungeonCell& Cell, const FDungeonResult& Result)
{
	if (Cell.RoomIndex > 0 && Cell.RoomIndex <= static_cast<uint8>(Result.Rooms.Num()))
	{
		// Rooms array is 0-indexed, RoomIndex is 1-based
		return Result.Rooms[Cell.RoomIndex - 1].RoomType;
	}
	return EDungeonRoomType::Generic;
}

// ============================================================================
// Voxel Editing Helpers
// ============================================================================

int32 UDungeonVoxelStamper::CarveCell(
	UVoxelEditManager* EditManager,
	const FDungeonVoxelLattice& Lattice,
	const FVector& CellWorldMin,
	float CellWorldSize,
	bool bOnlyIfSolid,
	UVoxelChunkManager* ChunkManager)
{
	int32 Count = 0;
	const FVoxelData AirVoxel = FVoxelData::Air();

	// The cell box is in the dungeon's frame (the world for an unrotated dungeon).
	Lattice.ForEachSampleInBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), [&](const FIntVector& Index)
	{
		const FVector WorldPos = Lattice.EditPosition(Index);

		if (bOnlyIfSolid)
		{
			const FVoxelData Existing = ChunkManager->GetVoxelAtWorldPosition(WorldPos);
			if (!Existing.IsSolid())
			{
				return;
			}
		}

		if (EditManager->ApplyEdit(WorldPos, AirVoxel, EEditMode::Set))
		{
			++Count;
		}
	});
	return Count;
}

// Face slab geometry (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z ceiling, 5=-Z floor) and the outer seal's
// sample selection live in FDungeonVoxelStampPlan so they can be unit tested without a voxel world.

int32 UDungeonVoxelStamper::PlaceBoundary(
	UVoxelEditManager* EditManager,
	const FDungeonVoxelLattice& Lattice,
	const FVector& CellWorldMin,
	float CellWorldSize,
	int32 Face,
	float Thickness,
	uint8 MaterialID,
	uint8 BiomeID)
{
	int32 Count = 0;
	const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, BiomeID);

	FVector BoxMin, BoxMax;
	FDungeonVoxelStampPlan::FaceSlabBox(CellWorldMin, CellWorldSize, Face, Thickness, /*bOutward=*/false, BoxMin, BoxMax);

	Lattice.ForEachSampleInBox(BoxMin, BoxMax, [&](const FIntVector& Index)
	{
		if (EditManager->ApplyEdit(Lattice.EditPosition(Index), SolidVoxel, EEditMode::Set))
		{
			++Count;
		}
	});
	return Count;
}

int32 UDungeonVoxelStamper::PlaceOuterSeal(
	UVoxelEditManager* EditManager,
	const FDungeonVoxelLattice& Lattice,
	const FVector& CellWorldMin,
	float CellWorldSize,
	int32 Face,
	float Thickness,
	uint8 MaterialID,
	uint8 BiomeID,
	const TSet<FIntVector>& OpenCellSamples,
	TSet<FIntVector>& SealedVoxels)
{
	int32 Count = 0;
	const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, BiomeID);

	TArray<FIntVector> ToWrite;
	FDungeonVoxelStampPlan::CollectOuterSealSamples(
		Lattice, CellWorldMin, CellWorldSize, Face, Thickness, OpenCellSamples, SealedVoxels, ToWrite);

	for (const FIntVector& Index : ToWrite)
	{
		if (EditManager->ApplyEdit(Lattice.EditPosition(Index), SolidVoxel, EEditMode::Set))
		{
			++Count;
		}
	}
	return Count;
}

// ============================================================================
// Staircase Step Geometry
// ============================================================================

static int32 PlaceStaircaseSteps(
	UVoxelEditManager* EditManager,
	UVoxelChunkManager* ChunkManager,
	const FDungeonStaircase& Staircase,
	const FDungeonVoxelLattice& Lattice,
	const FVector& WorldOffset,
	float CellWorldSize,
	int32 StepsPerCell,
	uint8 MaterialID,
	uint8 BiomeID,
	TSet<FIntVector>& AffectedChunks)
{
	int32 Count = 0;
	const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, BiomeID);

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

		Lattice.ForEachSampleInBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), [&](const FIntVector& Index)
		{
			// Measured in the dungeon's frame (the world for an unrotated dungeon).
			const FVector FramePos = Lattice.FrameEditPosition(Index);

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

			if (EditManager->ApplyEdit(Lattice.EditPosition(Index), SolidVoxel, EEditMode::Set))
			{
				++Count;
			}
		});

		AffectedChunks.Add(ChunkManager->WorldToChunkCoord(
			Lattice.ToWorld(CellWorldMin + FVector(CellWorldSize * 0.5f))));
	}

	return Count;
}

// ============================================================================
// Main Stamp Entry Point
// ============================================================================

FDungeonStampResult UDungeonVoxelStamper::StampDungeon(
	const FDungeonResult& Result,
	UVoxelChunkManager* ChunkManager,
	const FVector& WorldOffset,
	EDungeonStampMode StampMode,
	UDungeonVoxelConfig* Config)
{
	return StampDungeonInFrame(Result, ChunkManager, WorldOffset, StampMode, Config, FTransform::Identity);
}

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

	if (!Config)
	{
		StampResult.ErrorMessage = TEXT("Config is null");
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

	const FDungeonGrid& Grid = Result.Grid;
	if (Grid.Cells.Num() == 0)
	{
		StampResult.ErrorMessage = TEXT("Dungeon grid is empty");
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StampDungeon: %s"), *StampResult.ErrorMessage);
		return StampResult;
	}

	const UVoxelWorldConfiguration* VoxelConfig = ChunkManager->GetConfiguration();
	if (!VoxelConfig)
	{
		StampResult.ErrorMessage = TEXT("VoxelWorldConfiguration is null");
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StampDungeon: %s"), *StampResult.ErrorMessage);
		return StampResult;
	}

	const float VoxelSize = VoxelConfig->VoxelSize;
	const float CellWorldSize = Result.CellWorldSize;
	const int32 VoxelsPerCell = Config->GetEffectiveVoxelsPerCell(CellWorldSize, VoxelSize);
	const uint8 BiomeID = Config->DungeonBiomeID;
	const bool bMergeMode = (StampMode == EDungeonStampMode::MergeAsStructure);

	// Every voxel write below resolves through this lattice rather than stepping in cell space:
	// the dungeon origin has an arbitrary phase against the voxel grid and CellWorldSize is not
	// generally a multiple of VoxelSize, so cell-space stepping leaves an uncarved rind. Cells,
	// slabs and stairs are laid out in the dungeon's frame (identity: the world; a planet POI's
	// pad space, spherical Phase 5), which the lattice maps samples into.
	FDungeonVoxelLattice Lattice(VoxelConfig->WorldOrigin, VoxelSize);
	Lattice.SetFrame(Frame);

	// A plane at an angle to the lattice aliases by up to half a voxel's diagonal (sqrt(3)/2 of a
	// voxel) where the axis-aligned lattice resolves it to a plane within half a voxel: a rotated
	// tile-dressed dungeon gets one more voxel of outer seal and half a voxel more carve margin, so
	// the rock stays behind the tiles. The voxel lining keeps its thickness: its surface is the
	// floor content stands on, and a slab a voxel thick has a sample on every lattice line through it.
	const float RotatedExtra = Lattice.bRotated ? 1.0f : 0.0f;

	// Wall/lining/seal depth in world units. WallThickness is authored in voxel layers.
	const float WallWorldThickness = (Config->WallThickness
		+ (StampMode == EDungeonStampMode::CarveOnly ? RotatedExtra : 0.0f)) * VoxelSize;

	// CarveOnly only: the void is carved CarveMargin past every cell plane so the meshed rock
	// surface (midway between the last carved sample and the first solid one) can never stand
	// inside the cell behind the tiles. The voxel-lined modes keep the exact cell box — their
	// stone lining IS the wall. The open-sample set and the seal slabs use the same expanded box.
	const float CarveMargin = (StampMode == EDungeonStampMode::CarveOnly)
		? (FMath::Clamp(Config->CarveMarginVoxels, 0.0f, 3.0f) + 0.5f * RotatedExtra) * VoxelSize
		: 0.0f;

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StampDungeon: Grid=%dx%dx%d CellWorldSize=%.1f VoxelSize=%.1f VoxelsPerCell=%d Mode=%d%s"),
		Grid.GridSize.X, Grid.GridSize.Y, Grid.GridSize.Z,
		CellWorldSize, VoxelSize, VoxelsPerCell, static_cast<int32>(StampMode),
		Lattice.bRotated ? TEXT(" (in a rotated frame)") : TEXT(""));

	// Track affected chunks for dirty marking
	TSet<FIntVector> AffectedChunks;

	// Begin grouped edit operation
	EditManager->BeginEditOperation(TEXT("Dungeon Stamp"));
	EditManager->SetEditSource(EEditSource::System);

	// ------------------------------------------------------------------
	// ReplaceRegion: clear entire bounding box first
	// ------------------------------------------------------------------
	if (StampMode == EDungeonStampMode::ReplaceRegion)
	{
		const FVoxelData AirVoxel = FVoxelData::Air();
		for (int32 GZ = 0; GZ < Grid.GridSize.Z; ++GZ)
		{
			for (int32 GY = 0; GY < Grid.GridSize.Y; ++GY)
			{
				for (int32 GX = 0; GX < Grid.GridSize.X; ++GX)
				{
					const FVector CellWorldMin = WorldOffset + FVector(GX, GY, GZ) * CellWorldSize;

					Lattice.ForEachSampleInBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), [&](const FIntVector& Index)
					{
						const FVector WorldPos = Lattice.EditPosition(Index);

						if (EditManager->ApplyEdit(WorldPos, AirVoxel, EEditMode::Set))
						{
							++StampResult.VoxelsModified;
						}

						AffectedChunks.Add(ChunkManager->WorldToChunkCoord(WorldPos));
					});
				}
			}
		}
	}

	// ------------------------------------------------------------------
	// Pass 1: Carve all open cells to air
	// ------------------------------------------------------------------
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

				const int32 Carved = CarveCell(EditManager, Lattice,
					CellWorldMin - FVector(CarveMargin), CellWorldSize + 2.0f * CarveMargin,
					bMergeMode, bMergeMode ? ChunkManager : nullptr);
				StampResult.VoxelsModified += Carved;

				// Track chunk for the cell center
				AffectedChunks.Add(ChunkManager->WorldToChunkCoord(
					Lattice.ToWorld(CellWorldMin + FVector(CellWorldSize * 0.5f))));
			}
		}
	}

	// ------------------------------------------------------------------
	// Pass 2: Place boundary voxels on faces adjacent to solid/OOB
	// (skipped in CarveOnly — tile meshes provide the lining, and boundary
	// voxels thicken INTO the open cell where those tiles stand)
	// ------------------------------------------------------------------
	// Direction offsets: +X, -X, +Y, -Y, +Z, -Z
	static const FIntVector Directions[6] = {
		{1, 0, 0}, {-1, 0, 0},
		{0, 1, 0}, {0, -1, 0},
		{0, 0, 1}, {0, 0, -1},
	};

	// CarveOnly is lined by tile meshes, so the lining must NOT go inside the cell (it would bury
	// the tiles). It still needs a seal: dungeons anchor at the cave layer, and without solid
	// voxels asserted around the volume, procedural cave voids breach it and terrain reads through
	// the tiles. Same boundary predicates, shell written OUTSIDE the cell instead.
	const bool bOuterSeal = (StampMode == EDungeonStampMode::CarveOnly);

	// Shared across every seal slab so each shell voxel is written exactly once.
	TSet<FIntVector> SealedVoxels;

	// Every sample Pass 1 carved, by lattice index. The seal slabs reach into the void (lateral
	// widening at edges and corners, and faces shared with another open cell lie entirely inside
	// it), and a sample that belongs to any open cell must never be written solid again.
	TSet<FIntVector> OpenCellSamples;
	if (bOuterSeal)
	{
		SealedVoxels.Reserve(Grid.Cells.Num() * 4);
		FDungeonVoxelStampPlan::CollectOpenCellSamples(Grid, Lattice, WorldOffset, CellWorldSize, CarveMargin, OpenCellSamples);
	}

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
					const FIntVector& Dir = Directions[Face];
					const int32 NX = GX + Dir.X;
					const int32 NY = GY + Dir.Y;
					const int32 NZ = GZ + Dir.Z;

					bool bNeedsBoundary;
					if (Face < 4)
					{
						bNeedsBoundary = FDungeonBoundaryRules::NeedsWall(Grid, FIntVector(GX, GY, GZ), NX, NY, NZ);
					}
					else
					{
						bNeedsBoundary = FDungeonBoundaryRules::NeedsVerticalBoundary(Grid, FIntVector(GX, GY, GZ), NX, NY, NZ);
					}

					if (!bNeedsBoundary)
					{
						continue;
					}

					const uint8 MatID = Config->GetMaterialForCell(Cell.CellType, RoomType, Face);

					const int32 Placed = bOuterSeal
						? PlaceOuterSeal(EditManager, Lattice,
							CellWorldMin - FVector(CarveMargin), CellWorldSize + 2.0f * CarveMargin,
							Face, WallWorldThickness, MatID, BiomeID, OpenCellSamples, SealedVoxels)
						: PlaceBoundary(EditManager, Lattice, CellWorldMin, CellWorldSize,
							Face, WallWorldThickness, MatID, BiomeID);
					StampResult.VoxelsModified += Placed;

					// Track chunk for boundary cell too
					AffectedChunks.Add(ChunkManager->WorldToChunkCoord(
						Lattice.ToWorld(CellWorldMin + FVector(CellWorldSize * 0.5f))));
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
	for (const FDungeonStaircase& Staircase : Result.Staircases)
	{
		if (StampMode == EDungeonStampMode::CarveOnly)
		{
			break;
		}
		const int32 StepVoxels = PlaceStaircaseSteps(
			EditManager, ChunkManager, Staircase, Lattice, WorldOffset,
			CellWorldSize, VoxelsPerCell,
			Config->StaircaseMaterialID, BiomeID, AffectedChunks);
		StampResult.VoxelsModified += StepVoxels;
	}

	// ------------------------------------------------------------------
	// ReplaceRegion: fill RoomWall cells solid + build outer shell
	// ------------------------------------------------------------------
	if (StampMode == EDungeonStampMode::ReplaceRegion)
	{
		const FVoxelData WallVoxel = FVoxelData::Solid(Config->WallMaterialID, BiomeID);

		for (int32 GZ = 0; GZ < Grid.GridSize.Z; ++GZ)
		{
			for (int32 GY = 0; GY < Grid.GridSize.Y; ++GY)
			{
				for (int32 GX = 0; GX < Grid.GridSize.X; ++GX)
				{
					const FDungeonCell& Cell = Grid.GetCell(GX, GY, GZ);
					if (Cell.CellType != EDungeonCellType::RoomWall && Cell.CellType != EDungeonCellType::Empty)
					{
						continue;
					}

					// Check if this cell is on the grid perimeter or adjacent to an open cell
					bool bIsShell = (GX == 0 || GX == Grid.GridSize.X - 1
						|| GY == 0 || GY == Grid.GridSize.Y - 1
						|| GZ == 0 || GZ == Grid.GridSize.Z - 1);

					if (!bIsShell)
					{
						// Check if adjacent to any open cell
						for (int32 D = 0; D < 6 && !bIsShell; ++D)
						{
							const int32 NX = GX + Directions[D].X;
							const int32 NY = GY + Directions[D].Y;
							const int32 NZ = GZ + Directions[D].Z;
							if (Grid.IsInBounds(NX, NY, NZ) && FDungeonBoundaryRules::IsOpenCell(Grid.GetCell(NX, NY, NZ).CellType))
							{
								bIsShell = true;
							}
						}
					}

					if (!bIsShell)
					{
						continue;
					}

					const FVector CellWorldMin = WorldOffset + FVector(GX, GY, GZ) * CellWorldSize;

					Lattice.ForEachSampleInBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), [&](const FIntVector& Index)
					{
						if (EditManager->ApplyEdit(Lattice.EditPosition(Index), WallVoxel, EEditMode::Set))
						{
							++StampResult.VoxelsModified;
						}
					});
				}
			}
		}
	}

	// End edit operation
	EditManager->EndEditOperation();

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
