#include "DungeonEntranceStitcher.h"
#include "DungeonEntrancePassagePlan.h"
#include "DungeonVoxelConfig.h"
#include "DungeonVoxelLattice.h"
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

	// 2-voxel wall shell: a single-voxel skin meshes as a pinched thin feature in smooth
	// (MC/DC) worlds wherever the shaft crosses open cave space — unstable normals streak the
	// triplanar texturing. Two voxels give the mesher a solid interior sample so both faces
	// mesh cleanly (and the shell survives a grazing edit).
	const float WallThickness = 2.0f * VoxelSize;
	const float OuterExtent = HalfExtentXY + (bPlaceWalls ? WallThickness : 0.0f);

	const UVoxelWorldConfiguration* VoxelConfig = ChunkManager->GetConfiguration();
	if (!VoxelConfig)
	{
		return 0;
	}

	// Resolve on the voxel lattice, not by float-stepping from the shaft centre: the shaft origin
	// has an arbitrary phase against the voxel grid, so stepping by VoxelSize skips voxels and
	// double-writes others, leaving an uncarved rind down the shaft wall.
	const FDungeonVoxelLattice Lattice(VoxelConfig->WorldOrigin, VoxelSize);

	FIntVector Min, Max;
	Lattice.RangeForBox(
		FVector(Center.X - OuterExtent, Center.Y - OuterExtent, BottomZ),
		FVector(Center.X + OuterExtent, Center.Y + OuterExtent, TopZ),
		Min, Max);
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
				const FIntVector Index(IX, IY, IZ);
				const FVector WorldPos = Lattice.SamplePosition(Index); // where this voxel is generated / meshed
				const FVector EditPos = Lattice.EditPosition(Index);    // what resolves to it in ApplyEdit
				const float DistX = FMath::Abs(WorldPos.X - Center.X);
				const float DistY = FMath::Abs(WorldPos.Y - Center.Y);

				// The carve may overshoot the terrain surface (breaking the mouth open), but the
				// wall shell must not follow it up: solid ring voxels placed in the air above the
				// surface would build a knee-high collar the character cannot step over.
				const bool bWallLayer = bPlaceWalls && WorldPos.Z < WallTopZ;

				if (DistX < HalfExtentXY && DistY < HalfExtentXY)
				{
					// Interior — carve to air
					if (EditManager->ApplyEdit(EditPos, AirVoxel, EEditMode::Set))
					{
						++VoxelsModified;
					}
				}
				else if (bWallLayer)
				{
					// Shell — place wall
					if (EditManager->ApplyEdit(EditPos, WallVoxel, EEditMode::Set))
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
// Cave opening carver
// ============================================================================

int32 UDungeonEntranceStitcher::CarveCaveOpening(
	UVoxelEditManager* EditManager,
	UVoxelChunkManager* ChunkManager,
	const FDungeonPassageSegment& Segment,
	float SurfaceZ,
	float VoxelSize,
	UDungeonVoxelConfig* Config)
{
	const FVector& EntranceCenter = Segment.Center;
	const float EntranceZ = Segment.BottomZ;
	const float CarveTopZ = Segment.TopZ;
	const float BaseRadius = Segment.HalfExtentXY;

	int32 VoxelsModified = 0;
	const FVoxelData AirVoxel = FVoxelData::Air();
	const FVoxelData WallVoxel = FVoxelData::Solid(Config->WallMaterialID, Config->DungeonBiomeID);

	// Carve a column with noise-displaced radius per Z-level. 2-voxel shell for the same
	// reason as CarveColumn: a 1-voxel skin streaks in smooth worlds where it crosses caves.
	const float WallThickness = 2.0f * VoxelSize;
	const float TotalHeight = CarveTopZ - EntranceZ;

	const UVoxelWorldConfiguration* VoxelConfig = ChunkManager->GetConfiguration();
	if (!VoxelConfig)
	{
		return 0;
	}
	const FDungeonVoxelLattice Lattice(VoxelConfig->WorldOrigin, VoxelSize);

	// Widest the displaced column can ever get, so one lattice range covers every Z slice.
	const float MaxOuterRadius = BaseRadius * 1.3f + WallThickness + VoxelSize * 1.5f;

	FIntVector Min, Max;
	Lattice.RangeForBox(
		FVector(EntranceCenter.X - MaxOuterRadius, EntranceCenter.Y - MaxOuterRadius, EntranceZ),
		FVector(EntranceCenter.X + MaxOuterRadius, EntranceCenter.Y + MaxOuterRadius, CarveTopZ),
		Min, Max);
	if (!FDungeonVoxelLattice::IsRangeValid(Min, Max))
	{
		return 0;
	}

	for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
	{
		// Per-Z-slice displacement, evaluated at the lattice plane's own Z.
		const double Z = Lattice.SamplePosition(FIntVector(Min.X, Min.Y, IZ)).Z;
		const float ZNormalized = static_cast<float>((Z - EntranceZ) / FMath::Max(TotalHeight, 1.0f));
		const float NoiseX = FMath::Sin(Z * 0.03f) * VoxelSize * 1.5f;
		const float NoiseY = FMath::Cos(Z * 0.037f) * VoxelSize * 1.5f;
		// Radius tapers: wider at top (cave mouth), narrower at bottom
		const float RadiusFactor = FMath::Lerp(0.7f, 1.3f, ZNormalized);
		const float Radius = BaseRadius * RadiusFactor;
		const float OuterRadius = Radius + WallThickness;

		const float CenterX = EntranceCenter.X + NoiseX;
		const float CenterY = EntranceCenter.Y + NoiseY;

		for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
		{
			for (int32 IX = Min.X; IX <= Max.X; ++IX)
			{
				const FIntVector Index(IX, IY, IZ);
				const FVector WorldPos = Lattice.SamplePosition(Index); // where this voxel is generated / meshed
				const FVector EditPos = Lattice.EditPosition(Index);    // what resolves to it in ApplyEdit
				const float DistXY = FMath::Sqrt(
					FMath::Square(WorldPos.X - CenterX) + FMath::Square(WorldPos.Y - CenterY));

				if (DistXY < Radius)
				{
					if (EditManager->ApplyEdit(EditPos, AirVoxel, EEditMode::Set))
					{
						++VoxelsModified;
					}
				}
				else if (DistXY < OuterRadius && WorldPos.Z < SurfaceZ) // no wall collar above ground
				{
					if (EditManager->ApplyEdit(EditPos, WallVoxel, EEditMode::Set))
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

	// The geometry is decided from the dungeon's recorded approach, so the carve stays inside the
	// volume the generator kept clear. A mismatched style is refused rather than carved blindly.
	const FDungeonEntrancePassagePlan Plan = FDungeonEntrancePassagePlan::Build(
		Result, WorldOffset, Style, VoxelSize, bStopAtEntranceCellTop,
		[this, ChunkManager](float X, float Y) { return DetectSurfaceHeight(ChunkManager, X, Y); });

	if (!Plan.IsValid())
	{
		UE_LOG(LogDungeonVoxelIntegration, Error, TEXT("StitchEntrance: refused — %s"), *Plan.Error);
		return -1;
	}
	if (!Plan.Note.IsEmpty())
	{
		UE_LOG(LogDungeonVoxelIntegration, Warning, TEXT("StitchEntrance: %s"), *Plan.Note);
	}

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StitchEntrance: Style=%d Approach=%d EntranceCell=(%d,%d,%d) StopAtTop=%d segments=%d surface=%.0f carveTop=%.0f entranceZ=%.0f"),
		static_cast<int32>(Style), static_cast<int32>(Plan.Approach),
		Result.EntranceCell.X, Result.EntranceCell.Y, Result.EntranceCell.Z,
		bStopAtEntranceCellTop ? 1 : 0, Plan.Segments.Num(), Plan.SurfaceZ, Plan.CarveTopZ, Plan.EntranceZ);

	static const TCHAR* OpNames[] = { TEXT("Entrance Shaft"), TEXT("Entrance Sloped Tunnel"), TEXT("Entrance Cave Opening"), TEXT("Entrance Trapdoor") };
	const int32 StyleIdx = FMath::Clamp(static_cast<int32>(Style), 0, 3);

	UVoxelEditManager* EditManager = ChunkManager->GetEditManager();
	EditManager->BeginEditOperation(OpNames[StyleIdx]);
	EditManager->SetEditSource(EEditSource::System);

	int32 VoxelsModified = 0;
	if (Style == EDungeonEntranceStyle::CaveOpening && Plan.Segments.Num() > 0)
	{
		VoxelsModified = CarveCaveOpening(EditManager, ChunkManager, Plan.Segments[0], Plan.SurfaceZ, VoxelSize, Config);
	}
	else
	{
		for (const FDungeonPassageSegment& Seg : Plan.Segments)
		{
			VoxelsModified += CarveColumn(EditManager, ChunkManager,
				FVector(Seg.Center.X, Seg.Center.Y, 0.0f),
				Seg.HalfExtentXY, Seg.TopZ, Seg.BottomZ, VoxelSize,
				Seg.bWalls, /*WallTopZ=*/Plan.SurfaceZ,
				Seg.bWalls ? Config->WallMaterialID : 0, Seg.bWalls ? Config->DungeonBiomeID : 0);
		}
	}

	EditManager->EndEditOperation();

	// Mark affected chunks dirty: every segment, every chunk-height slice of it.
	const float ChunkStride = VoxelSize * 32.0f;
	for (const FDungeonPassageSegment& Seg : Plan.Segments)
	{
		for (float Z = Seg.BottomZ; Z < Seg.TopZ + ChunkStride; Z += ChunkStride)
		{
			ChunkManager->MarkChunkDirty(
				ChunkManager->WorldToChunkCoord(FVector(Seg.Center.X, Seg.Center.Y, FMath::Min(Z, Seg.TopZ))));
		}
	}

	UE_LOG(LogDungeonVoxelIntegration, Log,
		TEXT("StitchEntrance: %s carved %d segment(s), %d voxels modified"),
		OpNames[StyleIdx], Plan.Segments.Num(), VoxelsModified);

	return VoxelsModified;
}
