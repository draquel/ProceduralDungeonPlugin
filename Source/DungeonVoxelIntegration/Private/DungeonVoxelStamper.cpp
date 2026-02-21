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
	const FVector& CellWorldMin,
	int32 VoxelsPerCell,
	float VoxelSize,
	bool bOnlyIfSolid,
	UVoxelChunkManager* ChunkManager)
{
	int32 Count = 0;
	const FVoxelData AirVoxel = FVoxelData::Air();

	for (int32 VZ = 0; VZ < VoxelsPerCell; ++VZ)
	{
		for (int32 VY = 0; VY < VoxelsPerCell; ++VY)
		{
			for (int32 VX = 0; VX < VoxelsPerCell; ++VX)
			{
				const FVector WorldPos = CellWorldMin + FVector(
					(VX + 0.5f) * VoxelSize,
					(VY + 0.5f) * VoxelSize,
					(VZ + 0.5f) * VoxelSize);

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

int32 UDungeonVoxelStamper::PlaceBoundary(
	UVoxelEditManager* EditManager,
	const FVector& CellWorldMin,
	int32 VoxelsPerCell,
	float VoxelSize,
	int32 Face,
	int32 Thickness,
	uint8 MaterialID,
	uint8 BiomeID)
{
	int32 Count = 0;
	const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, BiomeID);

	// Face directions: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z(ceiling), 5=-Z(floor)
	for (int32 Layer = 0; Layer < Thickness; ++Layer)
	{
		for (int32 A = 0; A < VoxelsPerCell; ++A)
		{
			for (int32 B = 0; B < VoxelsPerCell; ++B)
			{
				int32 VX, VY, VZ;

				switch (Face)
				{
				case 0: // +X face
					VX = VoxelsPerCell - 1 - Layer;
					VY = A;
					VZ = B;
					break;
				case 1: // -X face
					VX = Layer;
					VY = A;
					VZ = B;
					break;
				case 2: // +Y face
					VX = A;
					VY = VoxelsPerCell - 1 - Layer;
					VZ = B;
					break;
				case 3: // -Y face
					VX = A;
					VY = Layer;
					VZ = B;
					break;
				case 4: // +Z face (ceiling)
					VX = A;
					VY = B;
					VZ = VoxelsPerCell - 1 - Layer;
					break;
				case 5: // -Z face (floor)
					VX = A;
					VY = B;
					VZ = Layer;
					break;
				default:
					continue;
				}

				const FVector WorldPos = CellWorldMin + FVector(
					(VX + 0.5f) * VoxelSize,
					(VY + 0.5f) * VoxelSize,
					(VZ + 0.5f) * VoxelSize);

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
	const FVector& WorldOffset,
	float CellWorldSize,
	int32 VoxelsPerCell,
	float VoxelSize,
	uint8 MaterialID,
	uint8 BiomeID,
	TSet<FIntVector>& AffectedChunks)
{
	int32 Count = 0;
	const FVoxelData SolidVoxel = FVoxelData::Solid(MaterialID, BiomeID);

	const int32 RiseToRun = Staircase.RiseRunRatio;

	// Climb direction: 0=+X, 1=-X, 2=+Y, 3=-Y
	static const int32 CDX[] = {1, -1, 0, 0};
	static const int32 CDY[] = {0, 0, 1, -1};
	const int32 ClimbDX = CDX[Staircase.Direction];
	const int32 ClimbDY = CDY[Staircase.Direction];

	// Determine climb axis and sign
	const bool bClimbAlongX = (ClimbDX != 0);
	const bool bPositiveClimb = bClimbAlongX ? (ClimbDX > 0) : (ClimbDY > 0);

	// Iterate body cells in climb order (ci=0 is nearest entry)
	for (int32 ci = 0; ci < RiseToRun; ++ci)
	{
		const FIntVector CellCoord(
			Staircase.BottomCell.X + ClimbDX * (ci + 1),
			Staircase.BottomCell.Y + ClimbDY * (ci + 1),
			Staircase.BottomCell.Z);

		const FVector CellWorldMin = WorldOffset + FVector(CellCoord) * CellWorldSize;

		for (int32 LocalClimb = 0; LocalClimb < VoxelsPerCell; ++LocalClimb)
		{
			// Map local climb-axis position to global run index
			const int32 RunWithinCell = bPositiveClimb ? LocalClimb : (VoxelsPerCell - 1 - LocalClimb);
			const int32 GlobalRunIdx = ci * VoxelsPerCell + RunWithinCell;

			// Step height: rises by 1 voxel every RiseToRun horizontal voxels
			const int32 StepTop = GlobalRunIdx / RiseToRun; // 0 to VoxelsPerCell-1

			// Fill solid from VZ=0 up to and including StepTop
			for (int32 Perp = 0; Perp < VoxelsPerCell; ++Perp)
			{
				for (int32 VZ = 0; VZ <= StepTop; ++VZ)
				{
					int32 VX, VY;
					if (bClimbAlongX)
					{
						VX = LocalClimb;
						VY = Perp;
					}
					else
					{
						VX = Perp;
						VY = LocalClimb;
					}

					const FVector WorldPos = CellWorldMin + FVector(
						(VX + 0.5f) * VoxelSize,
						(VY + 0.5f) * VoxelSize,
						(VZ + 0.5f) * VoxelSize);

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
	const int32 WallThickness = Config->WallThickness;
	const uint8 BiomeID = Config->DungeonBiomeID;
	const bool bMergeMode = (StampMode == EDungeonStampMode::MergeAsStructure);

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

					for (int32 VZ = 0; VZ < VoxelsPerCell; ++VZ)
					{
						for (int32 VY = 0; VY < VoxelsPerCell; ++VY)
						{
							for (int32 VX = 0; VX < VoxelsPerCell; ++VX)
							{
								const FVector WorldPos = CellWorldMin + FVector(
									(VX + 0.5f) * VoxelSize,
									(VY + 0.5f) * VoxelSize,
									(VZ + 0.5f) * VoxelSize);

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

				const int32 Carved = CarveCell(EditManager, CellWorldMin, VoxelsPerCell, VoxelSize,
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
	// ------------------------------------------------------------------
	// Direction offsets: +X, -X, +Y, -Y, +Z, -Z
	static const FIntVector Directions[6] = {
		{1, 0, 0}, {-1, 0, 0},
		{0, 1, 0}, {0, -1, 0},
		{0, 0, 1}, {0, 0, -1},
	};

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

					const int32 Placed = PlaceBoundary(EditManager, CellWorldMin, VoxelsPerCell,
						VoxelSize, Face, WallThickness, MatID, BiomeID);
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
			EditManager, ChunkManager, Staircase, WorldOffset,
			CellWorldSize, VoxelsPerCell, VoxelSize,
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

					for (int32 VZ = 0; VZ < VoxelsPerCell; ++VZ)
					{
						for (int32 VY = 0; VY < VoxelsPerCell; ++VY)
						{
							for (int32 VX = 0; VX < VoxelsPerCell; ++VX)
							{
								const FVector WorldPos = CellWorldMin + FVector(
									(VX + 0.5f) * VoxelSize,
									(VY + 0.5f) * VoxelSize,
									(VZ + 0.5f) * VoxelSize);

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
