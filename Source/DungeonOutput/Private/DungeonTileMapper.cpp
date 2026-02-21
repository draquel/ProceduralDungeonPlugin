#include "DungeonTileMapper.h"
#include "DungeonTileSet.h"
#include "DungeonTypes.h"
#include "DungeonOutput.h"
#include "Engine/StaticMesh.h"

// ============================================================================
// FDungeonTileMapResult
// ============================================================================

int32 FDungeonTileMapResult::GetTotalInstanceCount() const
{
	int32 Total = 0;
	for (int32 i = 0; i < TypeCount; ++i)
	{
		Total += Transforms[i].Num();
	}
	return Total;
}

void FDungeonTileMapResult::Reset()
{
	for (int32 i = 0; i < TypeCount; ++i)
	{
		Transforms[i].Reset();
	}
}

// ============================================================================
// FDungeonTileMapper
// ============================================================================

bool FDungeonTileMapper::NeedsWall(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ)
{
	if (!Grid.IsInBounds(NX, NY, NZ))
	{
		return true;
	}

	const FDungeonCell& Neighbor = Grid.GetCell(NX, NY, NZ);

	// Solid neighbor always needs wall
	if (Neighbor.CellType == EDungeonCellType::Empty || Neighbor.CellType == EDungeonCellType::RoomWall)
	{
		return true;
	}

	// Door/Entrance neighbors handle their own frames — don't wall them off
	if (Neighbor.CellType == EDungeonCellType::Door || Neighbor.CellType == EDungeonCellType::Entrance)
	{
		return false;
	}

	// Room-family cells (Room, Door, Entrance) — grouped by RoomIndex
	auto IsRoomFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Room
			|| Type == EDungeonCellType::Door
			|| Type == EDungeonCellType::Entrance;
	};

	// Hallway-family cells (Hallway, Staircase, StaircaseHead) — grouped by HallwayIndex
	auto IsHallwayFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Hallway
			|| Type == EDungeonCellType::Staircase
			|| Type == EDungeonCellType::StaircaseHead;
	};

	// Same room = no wall
	if (IsRoomFamily(Current.CellType) && IsRoomFamily(Neighbor.CellType)
		&& Current.RoomIndex == Neighbor.RoomIndex)
	{
		return false;
	}

	// Hallway-family ↔ hallway-family = no wall (hallways merge naturally at intersections).
	// HallwayIndex only matters for vertical boundaries (staircase shaft ceilings).
	if (IsHallwayFamily(Current.CellType) && IsHallwayFamily(Neighbor.CellType))
	{
		return false;
	}

	// Different spaces (room↔hallway, different rooms) = wall
	return true;
}

bool FDungeonTileMapper::NeedsVerticalBoundary(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ)
{
	if (!Grid.IsInBounds(NX, NY, NZ))
	{
		return true;
	}

	const FDungeonCell& Neighbor = Grid.GetCell(NX, NY, NZ);

	// Solid neighbor always needs boundary
	if (Neighbor.CellType == EDungeonCellType::Empty || Neighbor.CellType == EDungeonCellType::RoomWall)
	{
		return true;
	}

	// Room-family cells (Room, Door, Entrance) — grouped by RoomIndex
	auto IsRoomFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Room
			|| Type == EDungeonCellType::Door
			|| Type == EDungeonCellType::Entrance;
	};

	// Hallway-family cells (Hallway, Staircase, StaircaseHead) — grouped by HallwayIndex
	auto IsHallwayFamily = [](EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Hallway
			|| Type == EDungeonCellType::Staircase
			|| Type == EDungeonCellType::StaircaseHead;
	};

	// Same room = no boundary (multi-floor room interior)
	if (IsRoomFamily(Current.CellType) && IsRoomFamily(Neighbor.CellType)
		&& Current.RoomIndex == Neighbor.RoomIndex)
	{
		return false;
	}

	// Same hallway = no boundary (staircase shaft stays open)
	if (IsHallwayFamily(Current.CellType) && IsHallwayFamily(Neighbor.CellType)
		&& Current.HallwayIndex == Neighbor.HallwayIndex)
	{
		return false;
	}

	// Different spaces = needs boundary
	return true;
}

FDungeonTileMapResult FDungeonTileMapper::MapToTiles(
	const FDungeonResult& Result,
	const UDungeonTileSet& TileSet,
	const FVector& WorldOffset)
{
	FDungeonTileMapResult Out;

	const FIntVector& GridSize = Result.Grid.GridSize;
	const float CS = Result.CellWorldSize;
	const float HalfCS = CS * 0.5f;
	const float Thin = CS * 0.2f;

	// --- Compute per-mesh bounding box info for scale-to-fit and pivot correction ---
	// Each mesh may have different native dimensions and pivot locations.
	// We query the bounding box to compute:
	//   Extent: full size along each axis (for scaling mesh to fit the target slot)
	//   Center: bounding box center relative to origin (for pivot correction)

	struct FMeshInfo
	{
		FVector Extent;  // Bounding box full size (not half-extent)
		FVector Center;  // Bounding box center in local (unscaled) space
	};

	auto GetMeshInfo = [](const TSoftObjectPtr<UStaticMesh>& MeshPtr) -> FMeshInfo
	{
		if (MeshPtr.IsNull()) return { FVector(100.0f), FVector::ZeroVector };
		UStaticMesh* Mesh = MeshPtr.LoadSynchronous();
		if (!Mesh) return { FVector(100.0f), FVector::ZeroVector };
		const FBox Box = Mesh->GetBoundingBox();
		const FVector Size = Box.GetSize();
		return {
			FVector(FMath::Max(Size.X, 0.01f), FMath::Max(Size.Y, 0.01f), FMath::Max(Size.Z, 0.01f)),
			Box.GetCenter()
		};
	};

	FMeshInfo MeshInfos[FDungeonTileMapResult::TypeCount];
	MeshInfos[static_cast<int32>(EDungeonTileType::RoomFloor)]     = GetMeshInfo(TileSet.RoomFloor);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayFloor)]  = GetMeshInfo(TileSet.HallwayFloor);
	MeshInfos[static_cast<int32>(EDungeonTileType::RoomCeiling)]   = GetMeshInfo(TileSet.RoomCeiling);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayCeiling)]= GetMeshInfo(TileSet.HallwayCeiling);
	MeshInfos[static_cast<int32>(EDungeonTileType::WallSegment)]   = GetMeshInfo(TileSet.WallSegment);
	MeshInfos[static_cast<int32>(EDungeonTileType::DoorFrame)]     = GetMeshInfo(TileSet.DoorFrame);
	MeshInfos[static_cast<int32>(EDungeonTileType::EntranceFrame)] = GetMeshInfo(TileSet.EntranceFrame);
	MeshInfos[static_cast<int32>(EDungeonTileType::StaircaseMesh)] = GetMeshInfo(TileSet.StaircaseMesh);

	// Floor/ceiling target: CS × CS × Thin — mesh local axes: X=CS, Y=CS, Z=Thin
	auto FloorScale = [&](EDungeonTileType Type) -> FVector
	{
		const FVector& E = MeshInfos[static_cast<int32>(Type)].Extent;
		return FVector(CS / E.X, CS / E.Y, Thin / E.Z);
	};

	// Wall target: Thin × CS × CS — mesh local X=thin, Y=width, Z=height (pre-rotation)
	auto WallScale = [&](EDungeonTileType Type) -> FVector
	{
		const FVector& E = MeshInfos[static_cast<int32>(Type)].Extent;
		return FVector(Thin / E.X, CS / E.Y, CS / E.Z);
	};

	// Pivot correction: offset placement so the mesh's bounding box center
	// lands at the intended position, regardless of where the pivot is.
	// PivotOffset = -Rotation.RotateVector(BoundsCenter * Scale)
	auto PivotOffset = [&](EDungeonTileType Type, const FVector& Scale, const FRotator& Rotation) -> FVector
	{
		const FVector& C = MeshInfos[static_cast<int32>(Type)].Center;
		return -Rotation.RotateVector(C * Scale);
	};

	for (int32 Z = 0; Z < GridSize.Z; ++Z)
	{
		for (int32 Y = 0; Y < GridSize.Y; ++Y)
		{
			for (int32 X = 0; X < GridSize.X; ++X)
			{
				const FDungeonCell& Cell = Result.Grid.GetCell(X, Y, Z);
				const EDungeonCellType CellType = Cell.CellType;

				// Skip non-geometry cells
				if (CellType == EDungeonCellType::Empty
					|| CellType == EDungeonCellType::RoomWall)
				{
					continue;
				}

				// Cell center in world space
				const FVector CellBase = Result.GridToWorld(FIntVector(X, Y, Z)) + WorldOffset;
				const FVector CellCenter = CellBase + FVector(HalfCS, HalfCS, 0.0f);

				const bool bIsStaircase = (CellType == EDungeonCellType::Staircase);
				const bool bIsStaircaseHead = (CellType == EDungeonCellType::StaircaseHead);

				// Staircase cell rendering is handled below per-staircase, not per-cell.

				// --- Walkable cells: Room, Hallway, Door, Entrance, Staircase, StaircaseHead ---
				const bool bIsHallway = (CellType == EDungeonCellType::Hallway);
				const bool bIsDoor = (CellType == EDungeonCellType::Door);
				const bool bIsEntrance = (CellType == EDungeonCellType::Entrance);

				// Determine floor/ceiling tile types (staircase cells use room tiles)
				const EDungeonTileType FloorType = bIsHallway
					? EDungeonTileType::HallwayFloor
					: EDungeonTileType::RoomFloor;
				const EDungeonTileType CeilingType = bIsHallway
					? EDungeonTileType::HallwayCeiling
					: EDungeonTileType::RoomCeiling;

				// Check which mesh to use for floor/ceiling validity
				const bool bHasFloorMesh = bIsHallway
					? !TileSet.HallwayFloor.IsNull()
					: !TileSet.RoomFloor.IsNull();
				const bool bHasCeilingMesh = bIsHallway
					? !TileSet.HallwayCeiling.IsNull()
					: !TileSet.RoomCeiling.IsNull();

				// Floor: place if cell below is a different space, solid, or OOB
				if (bHasFloorMesh && NeedsVerticalBoundary(Result.Grid, Cell, X, Y, Z - 1))
				{
					const FVector FS = FloorScale(FloorType);
					Out.Transforms[static_cast<int32>(FloorType)].Emplace(
						FTransform(FRotator::ZeroRotator,
							CellCenter + PivotOffset(FloorType, FS, FRotator::ZeroRotator), FS));
				}

				// Ceiling: place if cell above is a different space, solid, or OOB
				if (bHasCeilingMesh && NeedsVerticalBoundary(Result.Grid, Cell, X, Y, Z + 1))
				{
					const FVector CeilingPos = CellCenter + FVector(0.0f, 0.0f, CS);
					const FVector CeilS = FloorScale(CeilingType);
					Out.Transforms[static_cast<int32>(CeilingType)].Emplace(
						FTransform(FRotator::ZeroRotator,
							CeilingPos + PivotOffset(CeilingType, CeilS, FRotator::ZeroRotator), CeilS));
				}

				// --- Per-face geometry: walls, door frames, entrance frames ---
				struct FWallCheck
				{
					int32 DX, DY;
					float Yaw;
					FVector Offset;
				};

				const FWallCheck WallChecks[] =
				{
					{ +1, 0, 0.0f,   FVector(+HalfCS, 0.0f, +HalfCS) },  // +X
					{ -1, 0, 180.0f,  FVector(-HalfCS, 0.0f, +HalfCS) },  // -X
					{ 0, +1, 90.0f,   FVector(0.0f, +HalfCS, +HalfCS) },  // +Y
					{ 0, -1, -90.0f,  FVector(0.0f, -HalfCS, +HalfCS) },  // -Y
				};

				for (const FWallCheck& WC : WallChecks)
				{
					const int32 NX = X + WC.DX;
					const int32 NY = Y + WC.DY;
					const FRotator FaceRot(0.0f, WC.Yaw, 0.0f);

					if (bIsDoor || bIsEntrance)
					{
						// Door/Entrance face-based logic:
						//   Solid/OOB → wall (exterior face)
						//   Same-room neighbor → open passage (no geometry)
						//   Hallway/other → door/entrance frame
						const bool bIsSolid = !Result.Grid.IsInBounds(NX, NY, Z)
							|| Result.Grid.GetCell(NX, NY, Z).CellType == EDungeonCellType::Empty
							|| Result.Grid.GetCell(NX, NY, Z).CellType == EDungeonCellType::RoomWall;

						if (bIsSolid)
						{
							if (!TileSet.WallSegment.IsNull())
							{
								const FVector WS = WallScale(EDungeonTileType::WallSegment);
								Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
									FTransform(FaceRot,
										CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, FaceRot), WS));
							}
						}
						else
						{
							const bool bNeighborIsSameRoom = Result.Grid.GetCell(NX, NY, Z).RoomIndex == Cell.RoomIndex
								&& (Result.Grid.GetCell(NX, NY, Z).CellType == EDungeonCellType::Room
									|| Result.Grid.GetCell(NX, NY, Z).CellType == EDungeonCellType::Door
									|| Result.Grid.GetCell(NX, NY, Z).CellType == EDungeonCellType::Entrance);

							if (!bNeighborIsSameRoom)
							{
								const EDungeonTileType FrameType = bIsDoor
									? EDungeonTileType::DoorFrame
									: EDungeonTileType::EntranceFrame;

								const bool bHasFrameMesh = bIsDoor
									? !TileSet.DoorFrame.IsNull()
									: !TileSet.EntranceFrame.IsNull();

								if (bHasFrameMesh)
								{
									const FVector FS = WallScale(FrameType);
									Out.Transforms[static_cast<int32>(FrameType)].Emplace(
										FTransform(FaceRot,
											CellCenter + WC.Offset + PivotOffset(FrameType, FS, FaceRot), FS));
								}
							}
						}
					}
					else if (NeedsWall(Result.Grid, Cell, NX, NY, Z))
					{
						// Wall on this face (solid, OOB, or different logical space)
						if (!TileSet.WallSegment.IsNull())
						{
							const FVector WS = WallScale(EDungeonTileType::WallSegment);
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(FaceRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, FaceRot), WS));
						}
					}
				}
			}
		}
	}

	// --- Staircase ramps: one mesh per staircase spanning bottom to top ---
	// Mesh convention (UE Level Prototyping ramp):
	//   - Slopes DOWN along local +Y (climb direction is -Y)
	//   - Width along local X
	//   - Rise along local Z
	//   - Pivot at high-end corner (minX, minY, minZ)
	if (!TileSet.StaircaseMesh.IsNull())
	{
		for (const FDungeonStaircase& Staircase : Result.Staircases)
		{
			// Bottom and top cell centers in world space
			const FVector BottomBase = Result.GridToWorld(Staircase.BottomCell) + WorldOffset;
			const FVector BottomCenter = BottomBase + FVector(HalfCS, HalfCS, 0.0f);
			const FVector TopBase = Result.GridToWorld(Staircase.TopCell) + WorldOffset;
			const FVector TopCenter = TopBase + FVector(HalfCS, HalfCS, 0.0f);

			// Run = horizontal distance, Rise = one floor height
			const float RunWorld = static_cast<float>(Staircase.RiseRunRatio) * CS;
			const float RiseWorld = CS;

			// Yaw: rotate mesh's -Y (climb direction) to face the staircase Direction.
			// Direction 0=+X, 1=-X, 2=+Y, 3=-Y
			float StairYaw = 0.0f;
			switch (Staircase.Direction)
			{
			case 0: StairYaw = 90.0f; break;    // Climb +X
			case 1: StairYaw = -90.0f; break;   // Climb -X
			case 2: StairYaw = 180.0f; break;   // Climb +Y
			case 3: StairYaw = 0.0f; break;     // Climb -Y
			}

			const FRotator StairRot(0.0f, StairYaw, 0.0f);

			// Scale: local X = width(CS), local Y = run(RunWorld), local Z = rise(RiseWorld)
			const FVector& StairE = MeshInfos[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Extent;
			const FVector StairScale(CS / StairE.X, RunWorld / StairE.Y, RiseWorld / StairE.Z);

			// Position: center of the ramp footprint, corrected for mesh pivot offset
			const FVector RampCenter = (BottomCenter + TopCenter) * 0.5f;
			const FVector StairPos = RampCenter + PivotOffset(EDungeonTileType::StaircaseMesh, StairScale, StairRot);

			Out.Transforms[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Emplace(
				FTransform(StairRot, StairPos, StairScale));
		}
	}

	UE_LOG(LogDungeonOutput, Log, TEXT("TileMapper: Generated %d instances across %d tile types"),
		Out.GetTotalInstanceCount(), FDungeonTileMapResult::TypeCount);

	return Out;
}
