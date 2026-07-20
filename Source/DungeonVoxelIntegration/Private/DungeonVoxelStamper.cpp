#include "DungeonVoxelStamper.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelIntegration.h"
#include "DungeonTypes.h"
#include "VoxelData.h"
#include "VoxelEditManager.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"

// ============================================================================
// Boundary Detection (replicates DungeonTileMapper logic)
// ============================================================================

bool UDungeonVoxelStamper::IsOpenCell(EDungeonCellType CellType)
{
	return CellType == EDungeonCellType::Room
		|| CellType == EDungeonCellType::Hallway
		|| CellType == EDungeonCellType::Staircase
		|| CellType == EDungeonCellType::StaircaseHead
		|| CellType == EDungeonCellType::Door
		|| CellType == EDungeonCellType::Entrance;
}

bool UDungeonVoxelStamper::NeedsWall(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ)
{
	if (!Grid.IsInBounds(NX, NY, NZ))
	{
		return true;
	}

	const FDungeonCell& Neighbor = Grid.GetCell(NX, NY, NZ);

	if (Neighbor.CellType == EDungeonCellType::Empty || Neighbor.CellType == EDungeonCellType::RoomWall)
	{
		return true;
	}

	// Door/Entrance neighbors handle their own frames
	if (Neighbor.CellType == EDungeonCellType::Door || Neighbor.CellType == EDungeonCellType::Entrance)
	{
		return false;
	}

	auto IsRoomFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Room
			|| Type == EDungeonCellType::Door
			|| Type == EDungeonCellType::Entrance;
	};

	auto IsHallwayFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Hallway
			|| Type == EDungeonCellType::Staircase
			|| Type == EDungeonCellType::StaircaseHead;
	};

	// Door/Entrance opening toward hallway = no wall (connection point between room and hallway)
	if ((Current.CellType == EDungeonCellType::Door || Current.CellType == EDungeonCellType::Entrance)
		&& IsHallwayFamily(Neighbor.CellType))
	{
		return false;
	}

	// Same room = no wall
	if (IsRoomFamily(Current.CellType) && IsRoomFamily(Neighbor.CellType)
		&& Current.RoomIndex == Neighbor.RoomIndex)
	{
		return false;
	}

	// Hallway-family merge: same hallway = open, different hallway = wall
	// StaircaseHead cells must connect to their exit Hallway (same HallwayIndex)
	if (IsHallwayFamily(Current.CellType) && IsHallwayFamily(Neighbor.CellType))
	{
		const bool bEitherIsHead = (Current.CellType == EDungeonCellType::StaircaseHead
			|| Neighbor.CellType == EDungeonCellType::StaircaseHead);
		if (bEitherIsHead)
		{
			return Current.HallwayIndex != Neighbor.HallwayIndex;
		}
		return false;
	}

	return true;
}

bool UDungeonVoxelStamper::NeedsVerticalBoundary(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ)
{
	if (!Grid.IsInBounds(NX, NY, NZ))
	{
		return true;
	}

	const FDungeonCell& Neighbor = Grid.GetCell(NX, NY, NZ);

	if (Neighbor.CellType == EDungeonCellType::Empty || Neighbor.CellType == EDungeonCellType::RoomWall)
	{
		return true;
	}

	// Door/Entrance neighbors — open
	if (Neighbor.CellType == EDungeonCellType::Door || Neighbor.CellType == EDungeonCellType::Entrance)
	{
		return false;
	}

	auto IsRoomFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Room
			|| Type == EDungeonCellType::Door
			|| Type == EDungeonCellType::Entrance;
	};

	auto IsHallwayFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Hallway
			|| Type == EDungeonCellType::Staircase
			|| Type == EDungeonCellType::StaircaseHead;
	};

	// Door/Entrance opening toward hallway = no vertical boundary
	if ((Current.CellType == EDungeonCellType::Door || Current.CellType == EDungeonCellType::Entrance)
		&& IsHallwayFamily(Neighbor.CellType))
	{
		return false;
	}

	if (IsRoomFamily(Current.CellType) && IsRoomFamily(Neighbor.CellType)
		&& Current.RoomIndex == Neighbor.RoomIndex)
	{
		return false;
	}

	if (IsHallwayFamily(Current.CellType) && IsHallwayFamily(Neighbor.CellType)
		&& Current.HallwayIndex == Neighbor.HallwayIndex)
	{
		return false;
	}

	return true;
}

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

	FIntVector Min, Max;
	Lattice.RangeForBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), Min, Max);
	if (!FDungeonVoxelLattice::IsRangeValid(Min, Max))
	{
		return 0;
	}

	for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
	{
		for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
		{
			for (int32 IX = Min.X; IX <= Max.X; ++IX)
			{
				const FVector WorldPos = Lattice.Center(FIntVector(IX, IY, IZ));

				if (bOnlyIfSolid)
				{
					const FVoxelData Existing = ChunkManager->GetVoxelAtWorldPosition(WorldPos);
					if (!Existing.IsSolid())
					{
						continue;
					}
				}

				if (EditManager->ApplyEdit(WorldPos, AirVoxel, EEditMode::Set))
				{
					++Count;
				}
			}
		}
	}
	return Count;
}

bool UDungeonVoxelStamper::IsInsideOpenCell(
	const FDungeonResult& Result,
	const FVector& WorldOffset,
	const FVector& WorldPos)
{
	const FVector Local = (WorldPos - WorldOffset) / Result.CellWorldSize;
	const FIntVector GridCoord(
		FMath::FloorToInt32(Local.X),
		FMath::FloorToInt32(Local.Y),
		FMath::FloorToInt32(Local.Z));

	return Result.Grid.IsInBounds(GridCoord) && IsOpenCell(Result.Grid.GetCell(GridCoord).CellType);
}

/**
 * World-space box covering one face slab of a cell.
 *
 * Faces: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z(ceiling), 5=-Z(floor).
 *
 * bOutward=false puts the slab INSIDE the cell (the stone lining). bOutward=true puts it just
 * OUTSIDE and also widens the slab laterally by Thickness, so the six face slabs overlap at the
 * cell's edges and corners and together form a closed shell.
 */
static void FaceSlabBox(
	const FVector& CellWorldMin,
	float CellWorldSize,
	int32 Face,
	float Thickness,
	bool bOutward,
	FVector& OutMin,
	FVector& OutMax)
{
	const FVector CellMax = CellWorldMin + FVector(CellWorldSize);
	const float Lateral = bOutward ? Thickness : 0.0f;

	OutMin = CellWorldMin - FVector(Lateral);
	OutMax = CellMax + FVector(Lateral);

	switch (Face)
	{
	case 0: // +X
		OutMin.X = bOutward ? CellMax.X : CellMax.X - Thickness;
		OutMax.X = bOutward ? CellMax.X + Thickness : CellMax.X;
		break;
	case 1: // -X
		OutMin.X = bOutward ? CellWorldMin.X - Thickness : CellWorldMin.X;
		OutMax.X = bOutward ? CellWorldMin.X : CellWorldMin.X + Thickness;
		break;
	case 2: // +Y
		OutMin.Y = bOutward ? CellMax.Y : CellMax.Y - Thickness;
		OutMax.Y = bOutward ? CellMax.Y + Thickness : CellMax.Y;
		break;
	case 3: // -Y
		OutMin.Y = bOutward ? CellWorldMin.Y - Thickness : CellWorldMin.Y;
		OutMax.Y = bOutward ? CellWorldMin.Y : CellWorldMin.Y + Thickness;
		break;
	case 4: // +Z (ceiling)
		OutMin.Z = bOutward ? CellMax.Z : CellMax.Z - Thickness;
		OutMax.Z = bOutward ? CellMax.Z + Thickness : CellMax.Z;
		break;
	case 5: // -Z (floor)
		OutMin.Z = bOutward ? CellWorldMin.Z - Thickness : CellWorldMin.Z;
		OutMax.Z = bOutward ? CellWorldMin.Z : CellWorldMin.Z + Thickness;
		break;
	default:
		OutMin = OutMax = FVector::ZeroVector;
		break;
	}
}

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
	FaceSlabBox(CellWorldMin, CellWorldSize, Face, Thickness, /*bOutward=*/false, BoxMin, BoxMax);

	FIntVector Min, Max;
	Lattice.RangeForBox(BoxMin, BoxMax, Min, Max);
	if (!FDungeonVoxelLattice::IsRangeValid(Min, Max))
	{
		return 0;
	}

	for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
	{
		for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
		{
			for (int32 IX = Min.X; IX <= Max.X; ++IX)
			{
				if (EditManager->ApplyEdit(Lattice.Center(FIntVector(IX, IY, IZ)), SolidVoxel, EEditMode::Set))
				{
					++Count;
				}
			}
		}
	}
	return Count;
}

int32 UDungeonVoxelStamper::PlaceOuterSeal(
	UVoxelEditManager* EditManager,
	const FDungeonVoxelLattice& Lattice,
	const FDungeonResult& Result,
	const FVector& WorldOffset,
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
	FaceSlabBox(CellWorldMin, CellWorldSize, Face, Thickness, /*bOutward=*/true, BoxMin, BoxMax);

	FIntVector Min, Max;
	Lattice.RangeForBox(BoxMin, BoxMax, Min, Max);
	if (!FDungeonVoxelLattice::IsRangeValid(Min, Max))
	{
		return 0;
	}

	for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
	{
		for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
		{
			for (int32 IX = Min.X; IX <= Max.X; ++IX)
			{
				const FVector WorldPos = Lattice.Center(FIntVector(IX, IY, IZ));

				// Never plug a room, hallway or the cell this seal belongs to. The lateral
				// widening that closes the corners is exactly what makes this reachable.
				if (IsInsideOpenCell(Result, WorldOffset, WorldPos))
				{
					continue;
				}

				if (EditManager->ApplyEdit(WorldPos, SolidVoxel, EEditMode::Set))
				{
					++Count;
				}
			}
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

		FIntVector Min, Max;
		Lattice.RangeForBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), Min, Max);
		if (!FDungeonVoxelLattice::IsRangeValid(Min, Max))
		{
			continue;
		}

		for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
		{
			for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
			{
				for (int32 IX = Min.X; IX <= Max.X; ++IX)
				{
					const FVector WorldPos = Lattice.Center(FIntVector(IX, IY, IZ));

					// Distance along the climb axis measured from the entry side of the cell.
					const double AlongAxis = bClimbAlongX
						? WorldPos.X - CellWorldMin.X
						: WorldPos.Y - CellWorldMin.Y;
					const double AlongFromEntry = bPositiveClimb
						? AlongAxis
						: CellWorldSize - AlongAxis;

					const int32 RunWithinCell = FMath::Clamp(
						FMath::FloorToInt32(AlongFromEntry / StepWorldSize), 0, StepsPerCell - 1);
					const int32 GlobalRunIdx = ci * StepsPerCell + RunWithinCell;

					// Step height: rises one tread every RiseToRun runs.
					const int32 StepTop = GlobalRunIdx / RiseToRun;
					const double StepTopZ = CellWorldMin.Z + (StepTop + 1) * StepWorldSize;

					if (WorldPos.Z >= StepTopZ)
					{
						continue;
					}

					if (EditManager->ApplyEdit(WorldPos, SolidVoxel, EEditMode::Set))
					{
						++Count;
					}
				}
			}
		}

		AffectedChunks.Add(ChunkManager->WorldToChunkCoord(
			CellWorldMin + FVector(CellWorldSize * 0.5f)));
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

	// Wall/lining/seal depth in world units. WallThickness is authored in voxel layers.
	const float WallWorldThickness = Config->WallThickness * VoxelSize;

	// Every voxel write below resolves through this lattice rather than stepping in cell space:
	// the dungeon origin has an arbitrary phase against the voxel grid and CellWorldSize is not
	// generally a multiple of VoxelSize, so cell-space stepping leaves an uncarved rind.
	const FDungeonVoxelLattice Lattice(VoxelConfig->WorldOrigin, VoxelSize);

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StampDungeon: Grid=%dx%dx%d CellWorldSize=%.1f VoxelSize=%.1f VoxelsPerCell=%d Mode=%d"),
		Grid.GridSize.X, Grid.GridSize.Y, Grid.GridSize.Z,
		CellWorldSize, VoxelSize, VoxelsPerCell, static_cast<int32>(StampMode));

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

					FIntVector VMin, VMax;
					Lattice.RangeForBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), VMin, VMax);
					if (!FDungeonVoxelLattice::IsRangeValid(VMin, VMax))
					{
						continue;
					}

					for (int32 IZ = VMin.Z; IZ <= VMax.Z; ++IZ)
					{
						for (int32 IY = VMin.Y; IY <= VMax.Y; ++IY)
						{
							for (int32 IX = VMin.X; IX <= VMax.X; ++IX)
							{
								const FVector WorldPos = Lattice.Center(FIntVector(IX, IY, IZ));

								if (EditManager->ApplyEdit(WorldPos, AirVoxel, EEditMode::Set))
								{
									++StampResult.VoxelsModified;
								}

								AffectedChunks.Add(ChunkManager->WorldToChunkCoord(WorldPos));
							}
						}
					}
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
				if (!IsOpenCell(Cell.CellType))
				{
					continue;
				}

				const FVector CellWorldMin = WorldOffset + FVector(GX, GY, GZ) * CellWorldSize;

				const int32 Carved = CarveCell(EditManager, Lattice, CellWorldMin, CellWorldSize,
					bMergeMode, bMergeMode ? ChunkManager : nullptr);
				StampResult.VoxelsModified += Carved;

				// Track chunk for the cell center
				AffectedChunks.Add(ChunkManager->WorldToChunkCoord(
					CellWorldMin + FVector(CellWorldSize * 0.5f)));
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
	for (int32 GZ = 0; GZ < Grid.GridSize.Z; ++GZ)
	{
		for (int32 GY = 0; GY < Grid.GridSize.Y; ++GY)
		{
			for (int32 GX = 0; GX < Grid.GridSize.X; ++GX)
			{
				const FDungeonCell& Cell = Grid.GetCell(GX, GY, GZ);
				if (!IsOpenCell(Cell.CellType))
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
						bNeedsBoundary = NeedsWall(Grid, Cell, NX, NY, NZ);
					}
					else
					{
						bNeedsBoundary = NeedsVerticalBoundary(Grid, Cell, NX, NY, NZ);
					}

					if (!bNeedsBoundary)
					{
						continue;
					}

					const uint8 MatID = Config->GetMaterialForCell(Cell.CellType, RoomType, Face);

					const int32 Placed = bOuterSeal
						? PlaceOuterSeal(EditManager, Lattice, Result, WorldOffset, CellWorldMin,
							CellWorldSize, Face, WallWorldThickness, MatID, BiomeID)
						: PlaceBoundary(EditManager, Lattice, CellWorldMin, CellWorldSize,
							Face, WallWorldThickness, MatID, BiomeID);
					StampResult.VoxelsModified += Placed;

					// Track chunk for boundary cell too
					AffectedChunks.Add(ChunkManager->WorldToChunkCoord(
						CellWorldMin + FVector(CellWorldSize * 0.5f)));
				}
			}
		}
	}

	// ------------------------------------------------------------------
	// Pass 3: Place staircase step geometry inside body cells
	// ------------------------------------------------------------------
	for (const FDungeonStaircase& Staircase : Result.Staircases)
	{
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
							if (Grid.IsInBounds(NX, NY, NZ) && IsOpenCell(Grid.GetCell(NX, NY, NZ).CellType))
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

					FIntVector VMin, VMax;
					Lattice.RangeForBox(CellWorldMin, CellWorldMin + FVector(CellWorldSize), VMin, VMax);
					if (!FDungeonVoxelLattice::IsRangeValid(VMin, VMax))
					{
						continue;
					}

					for (int32 IZ = VMin.Z; IZ <= VMax.Z; ++IZ)
					{
						for (int32 IY = VMin.Y; IY <= VMax.Y; ++IY)
						{
							for (int32 IX = VMin.X; IX <= VMax.X; ++IX)
							{
								const FVector WorldPos = Lattice.Center(FIntVector(IX, IY, IZ));

								if (EditManager->ApplyEdit(WorldPos, WallVoxel, EEditMode::Set))
								{
									++StampResult.VoxelsModified;
								}
							}
						}
					}
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
