#include "DungeonEntranceStitcher.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelIntegration.h"
#include "DungeonTypes.h"
#include "VoxelData.h"
#include "VoxelEditManager.h"
#include "VoxelChunkManager.h"
#include "IVoxelWorldMode.h"
#include "VoxelWorldConfiguration.h"

// ============================================================================
// Surface Detection
// ============================================================================

float UDungeonEntranceStitcher::DetectSurfaceHeight(UVoxelChunkManager* ChunkManager, float WorldX, float WorldY) const
{
	// Try heightmap-based detection first. GetGeneratedSurfaceHeight is the canonical analytic
	// surface: continentalness AND terrain-conditioning zones included, so the shaft top lands on
	// the REAL generated surface. (The raw IVoxelWorldMode::GetTerrainHeightAt misses conditioning
	// — on a flattened POI pad it either capped the shaft below the pad crust or collared it above.)
	const IVoxelWorldMode* WorldMode = ChunkManager->GetWorldMode();
	if (WorldMode && WorldMode->IsHeightmapBased())
	{
		return ChunkManager->GetGeneratedSurfaceHeight(WorldX, WorldY);
	}

	// Fallback: vertical sweep from high to low, find first solid voxel
	const float SweepStart = 10000.0f;
	const float SweepEnd = -10000.0f;
	const float StepSize = 50.0f; // coarse sweep

	for (float Z = SweepStart; Z > SweepEnd; Z -= StepSize)
	{
		const FVoxelData Voxel = ChunkManager->GetVoxelAtWorldPosition(FVector(WorldX, WorldY, Z));
		if (Voxel.IsSolid())
		{
			return Z + StepSize; // return the Z just above the first solid
		}
	}

	UE_LOG(LogDungeonVoxelIntegration, Warning,
		TEXT("DetectSurfaceHeight: No solid voxel found at (%.0f, %.0f), defaulting to 0"),
		WorldX, WorldY);
	return 0.0f;
}

// ============================================================================
// Column Carver
// ============================================================================

int32 UDungeonEntranceStitcher::CarveColumn(
	UVoxelEditManager* EditManager,
	UVoxelChunkManager* ChunkManager,
	const FVector& Center,
	float HalfExtentXY,
	float TopZ,
	float BottomZ,
	float VoxelSize,
	bool bPlaceWalls,
	float WallTopZ,
	uint8 WallMaterialID,
	uint8 BiomeID)
{
	int32 VoxelsModified = 0;
	const FVoxelData AirVoxel = FVoxelData::Air();
	const FVoxelData WallVoxel = FVoxelData::Solid(WallMaterialID, BiomeID);

	const float WallThickness = VoxelSize; // 1 voxel wall shell
	const float OuterExtent = HalfExtentXY + (bPlaceWalls ? WallThickness : 0.0f);

	for (float Z = BottomZ; Z < TopZ; Z += VoxelSize)
	{
		// The carve may overshoot the terrain surface (breaking the mouth open), but the wall
		// shell must not follow it up: solid ring voxels placed in the air above the surface
		// would build a knee-high collar the character cannot step over.
		const bool bWallLayer = bPlaceWalls && Z < WallTopZ;

		for (float Y = Center.Y - OuterExtent; Y < Center.Y + OuterExtent; Y += VoxelSize)
		{
			for (float X = Center.X - OuterExtent; X < Center.X + OuterExtent; X += VoxelSize)
			{
				const FVector WorldPos(X + VoxelSize * 0.5f, Y + VoxelSize * 0.5f, Z + VoxelSize * 0.5f);
				const float DistX = FMath::Abs(WorldPos.X - Center.X);
				const float DistY = FMath::Abs(WorldPos.Y - Center.Y);

				if (DistX < HalfExtentXY && DistY < HalfExtentXY)
				{
					// Interior — carve to air
					if (EditManager->ApplyEdit(WorldPos, AirVoxel, EEditMode::Set))
					{
						++VoxelsModified;
					}
				}
				else if (bWallLayer && DistX < OuterExtent && DistY < OuterExtent)
				{
					// Shell — place wall
					if (EditManager->ApplyEdit(WorldPos, WallVoxel, EEditMode::Set))
					{
						++VoxelsModified;
					}
				}
			}
		}
	}

	return VoxelsModified;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int32 UDungeonEntranceStitcher::StitchEntrance(
	const FDungeonResult& Result,
	UVoxelChunkManager* ChunkManager,
	const FVector& WorldOffset,
	EDungeonEntranceStyle Style,
	UDungeonVoxelConfig* Config,
	bool bStopAtEntranceCellTop)
{
	if (!ChunkManager)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StitchEntrance: ChunkManager is null"));
		return -1;
	}

	if (!Config)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StitchEntrance: Config is null"));
		return -1;
	}

	if (Result.EntranceRoomIndex < 0)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StitchEntrance: No entrance defined in dungeon result"));
		return -1;
	}

	const UVoxelWorldConfiguration* VoxelConfig = ChunkManager->GetConfiguration();
	if (!VoxelConfig)
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StitchEntrance: VoxelWorldConfiguration is null"));
		return -1;
	}

	const float VoxelSize = VoxelConfig->VoxelSize;
	const float EntranceZ = ComputeEntranceZ(Result, WorldOffset, bStopAtEntranceCellTop);

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StitchEntrance: Style=%d EntranceCell=(%d,%d,%d) StopAtTop=%d"),
		static_cast<int32>(Style),
		Result.EntranceCell.X, Result.EntranceCell.Y, Result.EntranceCell.Z,
		bStopAtEntranceCellTop ? 1 : 0);

	switch (Style)
	{
	case EDungeonEntranceStyle::VerticalShaft:
		return StitchVerticalShaft(Result, ChunkManager, WorldOffset, Config, VoxelSize, EntranceZ);
	case EDungeonEntranceStyle::SlopedTunnel:
		return StitchSlopedTunnel(Result, ChunkManager, WorldOffset, Config, VoxelSize, EntranceZ);
	case EDungeonEntranceStyle::CaveOpening:
		return StitchCaveOpening(Result, ChunkManager, WorldOffset, Config, VoxelSize, EntranceZ);
	case EDungeonEntranceStyle::Trapdoor:
		return StitchTrapdoor(Result, ChunkManager, WorldOffset, Config, VoxelSize, EntranceZ);
	default:
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StitchEntrance: Unknown style %d"), static_cast<int32>(Style));
		return -1;
	}
}

float UDungeonEntranceStitcher::ComputeEntranceZ(const FDungeonResult& Result, const FVector& WorldOffset, bool bStopAtEntranceCellTop)
{
	const float EntranceCellBottomZ = WorldOffset.Z + Result.EntranceCell.Z * Result.CellWorldSize;
	return bStopAtEntranceCellTop ? EntranceCellBottomZ + Result.CellWorldSize : EntranceCellBottomZ;
}

// ============================================================================
// Style Implementations
// ============================================================================

int32 UDungeonEntranceStitcher::StitchVerticalShaft(
	const FDungeonResult& Result,
	UVoxelChunkManager* ChunkManager,
	const FVector& WorldOffset,
	UDungeonVoxelConfig* Config,
	float VoxelSize,
	float EntranceZ)
{
	const float CellWorldSize = Result.CellWorldSize;
	const FVector EntranceWorldMin = WorldOffset + FVector(Result.EntranceCell) * CellWorldSize;
	const FVector EntranceCenter = EntranceWorldMin + FVector(CellWorldSize * 0.5f);

	const float SurfaceZ = DetectSurfaceHeight(ChunkManager, EntranceCenter.X, EntranceCenter.Y);
	// Overshoot ABOVE the surface: carving to exactly SurfaceZ leaves the voxel band CONTAINING
	// the isosurface crossing uncarved, and the mesher skins a collidable lid over the mouth.
	// (Player digs never hit this — the spherical brush overshoots the clicked surface point.)
	const float CarveTopZ = SurfaceZ + 2.0f * VoxelSize;
	const float HalfExtent = CellWorldSize * 0.5f;

	UVoxelEditManager* EditManager = ChunkManager->GetEditManager();
	EditManager->BeginEditOperation(TEXT("Entrance Shaft"));
	EditManager->SetEditSource(EEditSource::System);

	const int32 VoxelsModified = CarveColumn(EditManager, ChunkManager,
		FVector(EntranceCenter.X, EntranceCenter.Y, 0.0f),
		HalfExtent, CarveTopZ, EntranceZ, VoxelSize,
		true, /*WallTopZ=*/SurfaceZ, Config->WallMaterialID, Config->DungeonBiomeID);

	EditManager->EndEditOperation();

	// Mark affected chunks dirty
	for (float Z = EntranceZ; Z < CarveTopZ; Z += VoxelSize * 32.0f)
	{
		ChunkManager->MarkChunkDirty(
			ChunkManager->WorldToChunkCoord(FVector(EntranceCenter.X, EntranceCenter.Y, Z)));
	}

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StitchVerticalShaft: Carved from Z=%.0f (surface %.0f) to Z=%.0f, %d voxels modified"),
		CarveTopZ, SurfaceZ, EntranceZ, VoxelsModified);

	return VoxelsModified;
}

int32 UDungeonEntranceStitcher::StitchSlopedTunnel(
	const FDungeonResult& Result,
	UVoxelChunkManager* ChunkManager,
	const FVector& WorldOffset,
	UDungeonVoxelConfig* Config,
	float VoxelSize,
	float EntranceZ)
{
	const float CellWorldSize = Result.CellWorldSize;
	const FVector EntranceWorldMin = WorldOffset + FVector(Result.EntranceCell) * CellWorldSize;
	const FVector EntranceCenter = EntranceWorldMin + FVector(CellWorldSize * 0.5f);

	const float SurfaceZ = DetectSurfaceHeight(ChunkManager, EntranceCenter.X, EntranceCenter.Y);
	// Break THROUGH the surface band, not up to it (see StitchVerticalShaft).
	const float CarveTopZ = SurfaceZ + 2.0f * VoxelSize;
	const float HalfExtent = CellWorldSize * 0.5f;

	// Determine horizontal direction: from entrance toward nearest grid boundary
	const FIntVector& EC = Result.EntranceCell;
	const FIntVector& GS = Result.Grid.GridSize;
	int32 BestDir = 0;
	int32 BestDist = EC.X;
	if (GS.X - 1 - EC.X < BestDist) { BestDir = 1; BestDist = GS.X - 1 - EC.X; }
	if (EC.Y < BestDist) { BestDir = 2; BestDist = EC.Y; }
	if (GS.Y - 1 - EC.Y < BestDist) { BestDir = 3; }

	FVector HorizDir;
	switch (BestDir)
	{
	case 0: HorizDir = FVector(-1, 0, 0); break;
	case 1: HorizDir = FVector(1, 0, 0); break;
	case 2: HorizDir = FVector(0, -1, 0); break;
	default: HorizDir = FVector(0, 1, 0); break;
	}

	UVoxelEditManager* EditManager = ChunkManager->GetEditManager();
	EditManager->BeginEditOperation(TEXT("Entrance Sloped Tunnel"));
	EditManager->SetEditSource(EEditSource::System);

	int32 TotalVoxels = 0;
	const float HeightPerStep = CellWorldSize;
	const int32 NumSteps = FMath::CeilToInt32((CarveTopZ - EntranceZ) / HeightPerStep);

	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		const float StepZ = EntranceZ + Step * HeightPerStep;
		const float StepTopZ = FMath::Min(StepZ + HeightPerStep, CarveTopZ);
		const FVector StepCenter = FVector(
			EntranceCenter.X + HorizDir.X * Step * CellWorldSize,
			EntranceCenter.Y + HorizDir.Y * Step * CellWorldSize,
			0.0f);

		TotalVoxels += CarveColumn(EditManager, ChunkManager,
			StepCenter, HalfExtent, StepTopZ, StepZ, VoxelSize,
			true, /*WallTopZ=*/SurfaceZ, Config->WallMaterialID, Config->DungeonBiomeID);
	}

	EditManager->EndEditOperation();

	// Mark chunks dirty along the tunnel path
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		const float StepZ = EntranceZ + Step * HeightPerStep;
		const FVector StepPos = FVector(
			EntranceCenter.X + HorizDir.X * Step * CellWorldSize,
			EntranceCenter.Y + HorizDir.Y * Step * CellWorldSize,
			StepZ);
		ChunkManager->MarkChunkDirty(ChunkManager->WorldToChunkCoord(StepPos));
	}

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StitchSlopedTunnel: %d steps, %d voxels modified"), NumSteps, TotalVoxels);

	return TotalVoxels;
}

int32 UDungeonEntranceStitcher::StitchCaveOpening(
	const FDungeonResult& Result,
	UVoxelChunkManager* ChunkManager,
	const FVector& WorldOffset,
	UDungeonVoxelConfig* Config,
	float VoxelSize,
	float EntranceZ)
{
	const float CellWorldSize = Result.CellWorldSize;
	const FVector EntranceWorldMin = WorldOffset + FVector(Result.EntranceCell) * CellWorldSize;
	const FVector EntranceCenter = EntranceWorldMin + FVector(CellWorldSize * 0.5f);

	const float SurfaceZ = DetectSurfaceHeight(ChunkManager, EntranceCenter.X, EntranceCenter.Y);
	// Break THROUGH the surface band, not up to it (see StitchVerticalShaft).
	const float CarveTopZ = SurfaceZ + 2.0f * VoxelSize;
	const float BaseRadius = CellWorldSize * 0.5f;

	UVoxelEditManager* EditManager = ChunkManager->GetEditManager();
	EditManager->BeginEditOperation(TEXT("Entrance Cave Opening"));
	EditManager->SetEditSource(EEditSource::System);

	int32 VoxelsModified = 0;
	const FVoxelData AirVoxel = FVoxelData::Air();
	const FVoxelData WallVoxel = FVoxelData::Solid(Config->WallMaterialID, Config->DungeonBiomeID);

	// Carve a column with noise-displaced radius per Z-level
	const float WallThickness = VoxelSize;
	const float TotalHeight = CarveTopZ - EntranceZ;

	for (float Z = EntranceZ; Z < CarveTopZ; Z += VoxelSize)
	{
		// Noise displacement: use sin-based pseudo-noise for organic feel
		const float ZNormalized = (Z - EntranceZ) / FMath::Max(TotalHeight, 1.0f);
		const float NoiseX = FMath::Sin(Z * 0.03f) * VoxelSize * 1.5f;
		const float NoiseY = FMath::Cos(Z * 0.037f) * VoxelSize * 1.5f;
		// Radius tapers: wider at top (cave mouth), narrower at bottom
		const float RadiusFactor = FMath::Lerp(0.7f, 1.3f, ZNormalized);
		const float Radius = BaseRadius * RadiusFactor;
		const float OuterRadius = Radius + WallThickness;

		const float CenterX = EntranceCenter.X + NoiseX;
		const float CenterY = EntranceCenter.Y + NoiseY;

		for (float Y = CenterY - OuterRadius; Y < CenterY + OuterRadius; Y += VoxelSize)
		{
			for (float X = CenterX - OuterRadius; X < CenterX + OuterRadius; X += VoxelSize)
			{
				const FVector WorldPos(X + VoxelSize * 0.5f, Y + VoxelSize * 0.5f, Z + VoxelSize * 0.5f);
				const float DistXY = FMath::Sqrt(
					FMath::Square(WorldPos.X - CenterX) + FMath::Square(WorldPos.Y - CenterY));

				if (DistXY < Radius)
				{
					if (EditManager->ApplyEdit(WorldPos, AirVoxel, EEditMode::Set))
					{
						++VoxelsModified;
					}
				}
				else if (DistXY < OuterRadius && Z < SurfaceZ) // no wall collar above ground
				{
					if (EditManager->ApplyEdit(WorldPos, WallVoxel, EEditMode::Set))
					{
						++VoxelsModified;
					}
				}
			}
		}
	}

	EditManager->EndEditOperation();

	// Mark affected chunks dirty
	for (float Z = EntranceZ; Z < CarveTopZ; Z += VoxelSize * 32.0f)
	{
		ChunkManager->MarkChunkDirty(
			ChunkManager->WorldToChunkCoord(FVector(EntranceCenter.X, EntranceCenter.Y, Z)));
	}

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StitchCaveOpening: %d voxels modified"), VoxelsModified);

	return VoxelsModified;
}

int32 UDungeonEntranceStitcher::StitchTrapdoor(
	const FDungeonResult& Result,
	UVoxelChunkManager* ChunkManager,
	const FVector& WorldOffset,
	UDungeonVoxelConfig* Config,
	float VoxelSize,
	float EntranceZ)
{
	const float CellWorldSize = Result.CellWorldSize;
	const FVector EntranceWorldMin = WorldOffset + FVector(Result.EntranceCell) * CellWorldSize;
	const FVector EntranceCenter = EntranceWorldMin + FVector(CellWorldSize * 0.5f);

	const float SurfaceZ = DetectSurfaceHeight(ChunkManager, EntranceCenter.X, EntranceCenter.Y);
	// Break THROUGH the surface band, not up to it (see StitchVerticalShaft) — a 1x1 column
	// skins over even more readily than the wide shaft.
	const float CarveTopZ = SurfaceZ + 2.0f * VoxelSize;

	// Minimal 1x1 voxel column — no walls
	const float HalfExtent = VoxelSize * 0.5f;

	UVoxelEditManager* EditManager = ChunkManager->GetEditManager();
	EditManager->BeginEditOperation(TEXT("Entrance Trapdoor"));
	EditManager->SetEditSource(EEditSource::System);

	const int32 VoxelsModified = CarveColumn(EditManager, ChunkManager,
		FVector(EntranceCenter.X, EntranceCenter.Y, 0.0f),
		HalfExtent, CarveTopZ, EntranceZ, VoxelSize,
		false, /*WallTopZ=*/SurfaceZ, 0, 0);

	EditManager->EndEditOperation();

	ChunkManager->MarkChunkDirty(
		ChunkManager->WorldToChunkCoord(FVector(EntranceCenter.X, EntranceCenter.Y, EntranceZ)));
	ChunkManager->MarkChunkDirty(
		ChunkManager->WorldToChunkCoord(FVector(EntranceCenter.X, EntranceCenter.Y, SurfaceZ)));

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StitchTrapdoor: %d voxels modified"), VoxelsModified);

	return VoxelsModified;
}
