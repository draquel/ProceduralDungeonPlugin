#include "VoxelDungeonWorldMode.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelIntegration.h"

FVoxelDungeonWorldMode::FVoxelDungeonWorldMode()
{
}

void FVoxelDungeonWorldMode::Initialize(
	const FDungeonResult& InResult,
	const FVector& InWorldOffset,
	const UDungeonVoxelConfig* InConfig,
	float InVoxelSize)
{
	// Deep copy grid data (no UObject pointers)
	Grid = InResult.Grid;
	GridSize = InResult.Grid.GridSize;
	CellWorldSize = InResult.CellWorldSize;
	WorldOffset = InWorldOffset;
	VoxelSize = InVoxelSize;

	// Copy room data for material lookup
	Rooms = InResult.Rooms;

	// Copy config values
	if (InConfig)
	{
		VoxelsPerCell = InConfig->GetEffectiveVoxelsPerCell(CellWorldSize, InVoxelSize);
		WallThickness = InConfig->WallThickness;
		WallMaterialID = InConfig->WallMaterialID;
		FloorMaterialID = InConfig->FloorMaterialID;
		CeilingMaterialID = InConfig->CeilingMaterialID;
		StaircaseMaterialID = InConfig->StaircaseMaterialID;
		DoorFrameMaterialID = InConfig->DoorFrameMaterialID;
		DungeonBiomeID = InConfig->DungeonBiomeID;
		RoomTypeMaterialOverrides = InConfig->RoomTypeMaterialOverrides;
	}

	// Compute chunk Z bounds from dungeon volume
	const float DungeonMinZ = WorldOffset.Z;
	const float DungeonMaxZ = WorldOffset.Z + GridSize.Z * CellWorldSize;
	// Add 1 chunk shell around the volume
	const float DefaultChunkWorldSize = 32.0f * InVoxelSize;
	MinZChunks = FMath::FloorToInt32(DungeonMinZ / DefaultChunkWorldSize) - 1;
	MaxZChunks = FMath::CeilToInt32(DungeonMaxZ / DefaultChunkWorldSize) + 1;

	bInitialized = true;

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("FVoxelDungeonWorldMode initialized: Grid=%dx%dx%d VoxelsPerCell=%d ZChunks=[%d,%d]"),
		GridSize.X, GridSize.Y, GridSize.Z, VoxelsPerCell, MinZChunks, MaxZChunks);
}

// ============================================================================
// Helpers
// ============================================================================

bool FVoxelDungeonWorldMode::IsOpenCell(EDungeonCellType CellType)
{
	return CellType == EDungeonCellType::Room
		|| CellType == EDungeonCellType::Hallway
		|| CellType == EDungeonCellType::Staircase
		|| CellType == EDungeonCellType::StaircaseHead
		|| CellType == EDungeonCellType::Door
		|| CellType == EDungeonCellType::Entrance;
}

bool FVoxelDungeonWorldMode::WorldToGridCoord(const FVector& WorldPos, FIntVector& OutGridCoord) const
{
	const FVector Local = WorldPos - WorldOffset;
	OutGridCoord = FIntVector(
		FMath::FloorToInt32(Local.X / CellWorldSize),
		FMath::FloorToInt32(Local.Y / CellWorldSize),
		FMath::FloorToInt32(Local.Z / CellWorldSize));

	return Grid.IsInBounds(OutGridCoord);
}

uint8 FVoxelDungeonWorldMode::GetMaterialForPosition(const FVector& WorldPos, const FIntVector& GridCoord) const
{
	if (!Grid.IsInBounds(GridCoord))
	{
		return WallMaterialID;
	}

	const FDungeonCell& Cell = Grid.GetCell(GridCoord);

	// Staircase surfaces
	if (Cell.CellType == EDungeonCellType::Staircase || Cell.CellType == EDungeonCellType::StaircaseHead)
	{
		return StaircaseMaterialID;
	}

	// Door frame
	if (Cell.CellType == EDungeonCellType::Door)
	{
		return DoorFrameMaterialID;
	}

	// Check room-type override
	if (Cell.RoomIndex > 0 && Cell.RoomIndex <= static_cast<uint8>(Rooms.Num()))
	{
		const EDungeonRoomType RoomType = Rooms[Cell.RoomIndex - 1].RoomType;
		if (const uint8* Override = RoomTypeMaterialOverrides.Find(RoomType))
		{
			return *Override;
		}
	}

	return WallMaterialID;
}

// ============================================================================
// IVoxelWorldMode Interface
// ============================================================================

float FVoxelDungeonWorldMode::GetDensityAt(
	const FVector& WorldPos,
	int32 LODLevel,
	float NoiseValue) const
{
	if (!bInitialized)
	{
		return 1.0f; // solid
	}

	FIntVector GridCoord;
	if (!WorldToGridCoord(WorldPos, GridCoord))
	{
		// Outside dungeon bounding box — compute distance to bbox for tapering
		const FVector DungeonMin = WorldOffset;
		const FVector DungeonMax = WorldOffset + FVector(GridSize) * CellWorldSize;

		// Distance to bounding box (positive = outside)
		const FVector Closest = FVector(
			FMath::Clamp(WorldPos.X, DungeonMin.X, DungeonMax.X),
			FMath::Clamp(WorldPos.Y, DungeonMin.Y, DungeonMax.Y),
			FMath::Clamp(WorldPos.Z, DungeonMin.Z, DungeonMax.Z));

		const float DistToBBox = FVector::Dist(WorldPos, Closest);

		// Within a shell thickness of VoxelSize, taper from solid to air
		if (DistToBBox < VoxelSize * 2.0f)
		{
			return DistToBBox; // positive = solid shell around dungeon
		}

		// Far outside — return air so we don't generate infinite solid
		return -1.0f;
	}

	const FDungeonCell& Cell = Grid.GetCell(GridCoord);

	// Empty/RoomWall cells are solid
	if (Cell.CellType == EDungeonCellType::Empty || Cell.CellType == EDungeonCellType::RoomWall)
	{
		return 1.0f;
	}

	// Open cell — compute distance to nearest boundary face
	const FVector CellWorldMin = WorldOffset + FVector(GridCoord) * CellWorldSize;
	const FVector LocalInCell = WorldPos - CellWorldMin;

	// Distance to each face of the cell (inward-positive)
	const float DistToMinX = LocalInCell.X;
	const float DistToMaxX = CellWorldSize - LocalInCell.X;
	const float DistToMinY = LocalInCell.Y;
	const float DistToMaxY = CellWorldSize - LocalInCell.Y;
	const float DistToMinZ = LocalInCell.Z;
	const float DistToMaxZ = CellWorldSize - LocalInCell.Z;

	float MinDistToBoundary = CellWorldSize; // large default

	// Check each face: if boundary needed, that face's distance matters
	const FIntVector Directions[6] = {
		{1, 0, 0}, {-1, 0, 0},
		{0, 1, 0}, {0, -1, 0},
		{0, 0, 1}, {0, 0, -1},
	};
	const float FaceDistances[6] = {
		DistToMaxX, DistToMinX,
		DistToMaxY, DistToMinY,
		DistToMaxZ, DistToMinZ,
	};

	auto NeedsWallCheck = [this, &Cell](const FIntVector& GC, int32 NX, int32 NY, int32 NZ) -> bool
	{
		if (!Grid.IsInBounds(NX, NY, NZ))
		{
			return true;
		}
		const FDungeonCell& Neighbor = Grid.GetCell(NX, NY, NZ);
		if (!IsOpenCell(Neighbor.CellType))
		{
			return true;
		}
		// Simplified: different room/hallway indices need boundary
		if (Cell.RoomIndex != 0 && Neighbor.RoomIndex != 0 && Cell.RoomIndex == Neighbor.RoomIndex)
		{
			return false;
		}
		if (Cell.HallwayIndex != 0 && Neighbor.HallwayIndex != 0 && Cell.HallwayIndex == Neighbor.HallwayIndex)
		{
			return false;
		}
		return Cell.RoomIndex != Neighbor.RoomIndex || Cell.HallwayIndex != Neighbor.HallwayIndex;
	};

	for (int32 Face = 0; Face < 6; ++Face)
	{
		const FIntVector& Dir = Directions[Face];
		const int32 NX = GridCoord.X + Dir.X;
		const int32 NY = GridCoord.Y + Dir.Y;
		const int32 NZ = GridCoord.Z + Dir.Z;

		if (NeedsWallCheck(GridCoord, NX, NY, NZ))
		{
			MinDistToBoundary = FMath::Min(MinDistToBoundary, FaceDistances[Face]);
		}
	}

	// Convert distance to SDF: negative = air (inside open space)
	// At the boundary face, transition from solid (wall) to air
	const float WallWorldThickness = WallThickness * VoxelSize;

	if (MinDistToBoundary < WallWorldThickness)
	{
		// Within wall thickness — solid (positive)
		return WallWorldThickness - MinDistToBoundary;
	}

	// Beyond wall thickness — air (negative)
	return -(MinDistToBoundary - WallWorldThickness);
}

float FVoxelDungeonWorldMode::GetTerrainHeightAt(
	float X,
	float Y,
	const FVoxelNoiseParams& NoiseParams) const
{
	// Not heightmap-based — return 0
	return 0.0f;
}

FIntVector FVoxelDungeonWorldMode::WorldToChunkCoord(
	const FVector& WorldPos,
	int32 ChunkSize,
	float InVoxelSize) const
{
	const float ChunkWorldSize = ChunkSize * InVoxelSize;
	return FIntVector(
		FMath::FloorToInt(WorldPos.X / ChunkWorldSize),
		FMath::FloorToInt(WorldPos.Y / ChunkWorldSize),
		FMath::FloorToInt(WorldPos.Z / ChunkWorldSize));
}

FVector FVoxelDungeonWorldMode::ChunkCoordToWorld(
	const FIntVector& ChunkCoord,
	int32 ChunkSize,
	float InVoxelSize,
	int32 LODLevel) const
{
	const float ChunkWorldSize = ChunkSize * InVoxelSize * FMath::Pow(2.0f, static_cast<float>(LODLevel));
	return FVector(ChunkCoord) * ChunkWorldSize;
}

int32 FVoxelDungeonWorldMode::GetMinZ() const
{
	return MinZChunks;
}

int32 FVoxelDungeonWorldMode::GetMaxZ() const
{
	return MaxZChunks;
}

uint8 FVoxelDungeonWorldMode::GetMaterialAtDepth(
	const FVector& WorldPos,
	float SurfaceHeight,
	float DepthBelowSurface) const
{
	if (!bInitialized)
	{
		return WallMaterialID;
	}

	FIntVector GridCoord;
	if (WorldToGridCoord(WorldPos, GridCoord))
	{
		return GetMaterialForPosition(WorldPos, GridCoord);
	}

	// Outside grid — outer shell material
	return WallMaterialID;
}
