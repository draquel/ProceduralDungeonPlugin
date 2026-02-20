#include "DungeonTileMapper.h"
#include "DungeonTileSet.h"
#include "DungeonTypes.h"
#include "DungeonOutput.h"

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

bool FDungeonTileMapper::IsSolid(const FDungeonResult& Result, int32 X, int32 Y, int32 Z)
{
	if (!Result.Grid.IsInBounds(X, Y, Z))
	{
		return true;
	}

	const EDungeonCellType CellType = Result.Grid.GetCell(X, Y, Z).CellType;
	return CellType == EDungeonCellType::Empty || CellType == EDungeonCellType::RoomWall;
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

	// Scale factors for 100cm unit meshes
	const float MeshUnit = 100.0f;
	const float ScaleCS = CS / MeshUnit;
	const float ScaleThin = Thin / MeshUnit;

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

				// --- Staircase body: place thin marker mesh on the floor (walls/floor/ceiling added below) ---
				if (bIsStaircase && !TileSet.StaircaseMesh.IsNull())
				{
					// Direction: 0=+X, 1=-X, 2=+Y, 3=-Y
					float StairYaw = 0.0f;
					switch (Cell.StaircaseDirection)
					{
					case 0: StairYaw = 0.0f; break;
					case 1: StairYaw = 180.0f; break;
					case 2: StairYaw = 90.0f; break;
					case 3: StairYaw = -90.0f; break;
					}

					// Thin marker sitting on the floor — doesn't block the shaft.
					// Replace with a proper ramp mesh in the TileSet for real stair geometry.
					const FVector StairPos = CellCenter + FVector(0.0f, 0.0f, Thin * 0.5f);
					const FRotator StairRot(0.0f, StairYaw, 0.0f);
					const FVector StairScale(ScaleCS, ScaleCS, ScaleThin);

					Out.Transforms[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Emplace(
						FTransform(StairRot, StairPos, StairScale));
				}

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

				const FVector FloorScale(ScaleCS, ScaleCS, ScaleThin);

				// Floor: place if cell below is solid or OOB
				if (bHasFloorMesh && IsSolid(Result, X, Y, Z - 1))
				{
					Out.Transforms[static_cast<int32>(FloorType)].Emplace(
						FTransform(FRotator::ZeroRotator, CellCenter, FloorScale));
				}

				// Ceiling: place if cell above is solid or OOB
				if (bHasCeilingMesh && IsSolid(Result, X, Y, Z + 1))
				{
					const FVector CeilingPos = CellCenter + FVector(0.0f, 0.0f, CS);
					Out.Transforms[static_cast<int32>(CeilingType)].Emplace(
						FTransform(FRotator::ZeroRotator, CeilingPos, FloorScale));
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

				// Scale is in local space (applied before rotation), so always
				// thin on local X. Rotation orients the thin face toward the neighbor.
				const FVector WallScale(ScaleThin, ScaleCS, ScaleCS);

				for (const FWallCheck& WC : WallChecks)
				{
					const int32 NX = X + WC.DX;
					const int32 NY = Y + WC.DY;

					if (IsSolid(Result, NX, NY, Z))
					{
						// Solid neighbor → wall on this face
						if (!TileSet.WallSegment.IsNull())
						{
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(FRotator(0.0f, WC.Yaw, 0.0f),
									CellCenter + WC.Offset, WallScale));
						}
					}
					else if (bIsDoor || bIsEntrance)
					{
						// Door/Entrance face-based logic:
						//   Room neighbor → open passage (no geometry)
						//   Hallway/Staircase/other non-room → place frame on this face
						const EDungeonCellType NeighborType =
							Result.Grid.GetCell(NX, NY, Z).CellType;

						const bool bNeighborIsRoom = (NeighborType == EDungeonCellType::Room);

						if (!bNeighborIsRoom)
						{
							const EDungeonTileType FrameType = bIsDoor
								? EDungeonTileType::DoorFrame
								: EDungeonTileType::EntranceFrame;

							const bool bHasFrameMesh = bIsDoor
								? !TileSet.DoorFrame.IsNull()
								: !TileSet.EntranceFrame.IsNull();

							if (bHasFrameMesh)
							{
								Out.Transforms[static_cast<int32>(FrameType)].Emplace(
									FTransform(FRotator(0.0f, WC.Yaw, 0.0f),
										CellCenter + WC.Offset, WallScale));
							}
						}
						// Room neighbor → open, no geometry on this face
					}
				}
			}
		}
	}

	UE_LOG(LogDungeonOutput, Log, TEXT("TileMapper: Generated %d instances across %d tile types"),
		Out.GetTotalInstanceCount(), FDungeonTileMapResult::TypeCount);

	return Out;
}
