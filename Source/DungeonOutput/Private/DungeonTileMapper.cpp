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
	// Exception: StaircaseHead cells only open toward same-staircase body/headroom cells.
	if (IsHallwayFamily(Current.CellType) && IsHallwayFamily(Neighbor.CellType))
	{
		const bool bEitherIsHead = (Current.CellType == EDungeonCellType::StaircaseHead
			|| Neighbor.CellType == EDungeonCellType::StaircaseHead);
		if (bEitherIsHead)
		{
			const bool bBothStaircaseFamily =
				(Current.CellType == EDungeonCellType::Staircase || Current.CellType == EDungeonCellType::StaircaseHead)
				&& (Neighbor.CellType == EDungeonCellType::Staircase || Neighbor.CellType == EDungeonCellType::StaircaseHead);
			return !(bBothStaircaseFamily && Current.HallwayIndex == Neighbor.HallwayIndex);
		}
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

	// Hallway floor variants: use variant mesh info if set, otherwise fall back to HallwayFloor's info
	const FMeshInfo& HallwayFloorInfo = MeshInfos[static_cast<int32>(EDungeonTileType::HallwayFloor)];
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayFloorStraight)]  = TileSet.HallwayFloorStraight.IsNull()  ? HallwayFloorInfo : GetMeshInfo(TileSet.HallwayFloorStraight);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayFloorCorner)]    = TileSet.HallwayFloorCorner.IsNull()    ? HallwayFloorInfo : GetMeshInfo(TileSet.HallwayFloorCorner);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayFloorTJunction)] = TileSet.HallwayFloorTJunction.IsNull() ? HallwayFloorInfo : GetMeshInfo(TileSet.HallwayFloorTJunction);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayFloorCrossroad)] = TileSet.HallwayFloorCrossroad.IsNull() ? HallwayFloorInfo : GetMeshInfo(TileSet.HallwayFloorCrossroad);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayFloorEndCap)]    = TileSet.HallwayFloorEndCap.IsNull()    ? HallwayFloorInfo : GetMeshInfo(TileSet.HallwayFloorEndCap);

	// Hallway ceiling variants: use variant mesh info if set, otherwise fall back to HallwayCeiling's info
	const FMeshInfo& HallwayCeilingInfo = MeshInfos[static_cast<int32>(EDungeonTileType::HallwayCeiling)];
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayCeilingStraight)]  = TileSet.HallwayCeilingStraight.IsNull()  ? HallwayCeilingInfo : GetMeshInfo(TileSet.HallwayCeilingStraight);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayCeilingCorner)]    = TileSet.HallwayCeilingCorner.IsNull()    ? HallwayCeilingInfo : GetMeshInfo(TileSet.HallwayCeilingCorner);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayCeilingTJunction)] = TileSet.HallwayCeilingTJunction.IsNull() ? HallwayCeilingInfo : GetMeshInfo(TileSet.HallwayCeilingTJunction);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayCeilingCrossroad)] = TileSet.HallwayCeilingCrossroad.IsNull() ? HallwayCeilingInfo : GetMeshInfo(TileSet.HallwayCeilingCrossroad);
	MeshInfos[static_cast<int32>(EDungeonTileType::HallwayCeilingEndCap)]    = TileSet.HallwayCeilingEndCap.IsNull()    ? HallwayCeilingInfo : GetMeshInfo(TileSet.HallwayCeilingEndCap);

	// --- Per-variant scale multipliers (applied on top of auto-fit scale) ---
	// Defaults to (1,1,1) for all types. Only hallway variants have user-configurable multipliers.
	FVector ScaleMultipliers[FDungeonTileMapResult::TypeCount];
	for (int32 i = 0; i < FDungeonTileMapResult::TypeCount; ++i) ScaleMultipliers[i] = FVector::OneVector;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayFloorStraight)]  = TileSet.HallwayFloorStraightScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayFloorCorner)]    = TileSet.HallwayFloorCornerScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayFloorTJunction)] = TileSet.HallwayFloorTJunctionScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayFloorCrossroad)] = TileSet.HallwayFloorCrossroadScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayFloorEndCap)]    = TileSet.HallwayFloorEndCapScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayCeilingStraight)]  = TileSet.HallwayCeilingStraightScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayCeilingCorner)]    = TileSet.HallwayCeilingCornerScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayCeilingTJunction)] = TileSet.HallwayCeilingTJunctionScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayCeilingCrossroad)] = TileSet.HallwayCeilingCrossroadScaleMultiplier;
	ScaleMultipliers[static_cast<int32>(EDungeonTileType::HallwayCeilingEndCap)]    = TileSet.HallwayCeilingEndCapScaleMultiplier;

	// Floor/ceiling target: CS × CS × Thin — mesh local axes: X=CS, Y=CS, Z=Thin
	// ScaleMultiplier is applied on top so users can fine-tune without fighting auto-fit.
	auto FloorScale = [&](EDungeonTileType Type) -> FVector
	{
		const FVector& E = MeshInfos[static_cast<int32>(Type)].Extent;
		const FVector& M = ScaleMultipliers[static_cast<int32>(Type)];
		return FVector(CS / E.X * M.X, CS / E.Y * M.Y, Thin / E.Z * M.Z);
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

	// Helper: returns true if a cell type is "hallway-connected" for floor variant classification.
	// When bHallwayOnly is set, only Hallway cells count — transitions get end caps.
	const bool bHallwayOnly = TileSet.bHallwayVariantsHallwayOnly;
	auto IsHallwayConnected = [bHallwayOnly](EDungeonCellType Type) -> bool
	{
		if (bHallwayOnly)
		{
			return Type == EDungeonCellType::Hallway;
		}
		return Type == EDungeonCellType::Hallway
			|| Type == EDungeonCellType::Staircase
			|| Type == EDungeonCellType::StaircaseHead
			|| Type == EDungeonCellType::Door
			|| Type == EDungeonCellType::Entrance;
	};

	// Cardinal directions: +X, -X, +Y, -Y (indices 0-3)
	static constexpr int32 DX[] = { 1, -1, 0, 0 };
	static constexpr int32 DY[] = { 0, 0, 1, -1 };

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

				// Ceiling tile type (unchanged — hallway variants only apply to floors)
				const EDungeonTileType CeilingType = bIsHallway
					? EDungeonTileType::HallwayCeiling
					: EDungeonTileType::RoomCeiling;

				// Check mesh availability
				const bool bHasFloorMesh = bIsHallway
					? !TileSet.HallwayFloor.IsNull()
					: !TileSet.RoomFloor.IsNull();
				const bool bHasCeilingMesh = bIsHallway
					? !TileSet.HallwayCeiling.IsNull()
					: !TileSet.RoomCeiling.IsNull();

				// --- Hallway connectivity detection (shared by floor + ceiling variants) ---
				// Computed once per hallway cell, used by both floor and ceiling placement.
				bool bConn[4] = {}; // +X, -X, +Y, -Y
				int32 ConnCount = 0;
				// Base yaw per shape (before T-junction offset which differs per floor/ceiling)
				float BaseEndCapYaw = 0.0f, BaseStraightYaw = 0.0f, BaseCornerYaw = 0.0f;
				float BaseTJuncYaw = 0.0f, BaseCrossroadYaw = 0.0f;
				enum { ShapeIsolated, ShapeEndCap, ShapeStraight, ShapeCorner, ShapeTJunction, ShapeCrossroad } HallwayShape = ShapeIsolated;

				if (bIsHallway)
				{
					for (int32 Dir = 0; Dir < 4; ++Dir)
					{
						const int32 NX = X + DX[Dir];
						const int32 NY = Y + DY[Dir];
						if (Result.Grid.IsInBounds(NX, NY, Z)
							&& IsHallwayConnected(Result.Grid.GetCell(NX, NY, Z).CellType))
						{
							bConn[Dir] = true;
							++ConnCount;
						}
					}

					switch (ConnCount)
					{
					case 1:
						HallwayShape = ShapeEndCap;
						if      (bConn[0]) BaseEndCapYaw = -90.0f;
						else if (bConn[1]) BaseEndCapYaw =  90.0f;
						else if (bConn[2]) BaseEndCapYaw =   0.0f;
						else               BaseEndCapYaw = 180.0f;
						break;
					case 2:
						if (bConn[0] && bConn[1])      { HallwayShape = ShapeStraight; BaseStraightYaw = 90.0f; }
						else if (bConn[2] && bConn[3])  { HallwayShape = ShapeStraight; BaseStraightYaw = 0.0f; }
						else
						{
							HallwayShape = ShapeCorner;
							if      (bConn[0] && bConn[2]) BaseCornerYaw =   0.0f;
							else if (bConn[1] && bConn[2]) BaseCornerYaw =  90.0f;
							else if (bConn[1] && bConn[3]) BaseCornerYaw = 180.0f;
							else                            BaseCornerYaw = -90.0f;
						}
						break;
					case 3:
						HallwayShape = ShapeTJunction;
						if      (!bConn[0]) BaseTJuncYaw =  90.0f;
						else if (!bConn[1]) BaseTJuncYaw = -90.0f;
						else if (!bConn[2]) BaseTJuncYaw = 180.0f;
						else                BaseTJuncYaw =   0.0f;
						break;
					case 4:
						HallwayShape = ShapeCrossroad;
						break;
					default:
						break;
					}
				}

				// --- Helper: compose connectivity yaw with a per-variant rotation offset ---
				auto ComposeYaw = [](float BaseYaw, const FRotator& Offset) -> float
				{
					return (FQuat(FRotator(0.0f, BaseYaw, 0.0f)) * Offset.Quaternion()).Rotator().Yaw;
				};

				// --- Helper: select variant type + yaw for a given shape ---
				// Selects from the given variant mesh set, falling back to baseType if variant is null.
				// Each variant has its own rotation offset composed with the connectivity-derived yaw.
				auto SelectVariant = [&](
					EDungeonTileType BaseType,
					EDungeonTileType StraightType, const TSoftObjectPtr<UStaticMesh>& StraightMesh, const FRotator& StraightOffset,
					EDungeonTileType CornerType, const TSoftObjectPtr<UStaticMesh>& CornerMesh, const FRotator& CornerOffset,
					EDungeonTileType TJuncType, const TSoftObjectPtr<UStaticMesh>& TJuncMesh, const FRotator& TJuncOffset,
					EDungeonTileType CrossType, const TSoftObjectPtr<UStaticMesh>& CrossMesh, const FRotator& CrossOffset,
					EDungeonTileType EndCapType, const TSoftObjectPtr<UStaticMesh>& EndCapMesh, const FRotator& EndCapOffset)
					-> TPair<EDungeonTileType, float>
				{
					switch (HallwayShape)
					{
					case ShapeEndCap:
						if (!EndCapMesh.IsNull()) return {EndCapType, ComposeYaw(BaseEndCapYaw, EndCapOffset)};
						break;
					case ShapeStraight:
						if (!StraightMesh.IsNull()) return {StraightType, ComposeYaw(BaseStraightYaw, StraightOffset)};
						break;
					case ShapeCorner:
						if (!CornerMesh.IsNull()) return {CornerType, ComposeYaw(BaseCornerYaw, CornerOffset)};
						break;
					case ShapeTJunction:
						if (!TJuncMesh.IsNull()) return {TJuncType, ComposeYaw(BaseTJuncYaw, TJuncOffset)};
						break;
					case ShapeCrossroad:
						if (!CrossMesh.IsNull()) return {CrossType, ComposeYaw(BaseCrossroadYaw, CrossOffset)};
						break;
					default:
						break;
					}
					return {BaseType, 0.0f};
				};

				// Floor: place if cell below is a different space, solid, or OOB.
				// Bottom face of the floor mesh is aligned flush with the cell's lower boundary.
				if (bHasFloorMesh && NeedsVerticalBoundary(Result.Grid, Cell, X, Y, Z - 1))
				{
					if (bIsHallway)
					{
						const auto [VariantType, VariantYaw] = SelectVariant(
							EDungeonTileType::HallwayFloor,
							EDungeonTileType::HallwayFloorStraight,  TileSet.HallwayFloorStraight,  TileSet.HallwayFloorStraightRotationOffset,
							EDungeonTileType::HallwayFloorCorner,    TileSet.HallwayFloorCorner,    TileSet.HallwayFloorCornerRotationOffset,
							EDungeonTileType::HallwayFloorTJunction, TileSet.HallwayFloorTJunction, TileSet.HallwayFloorTJunctionRotationOffset,
							EDungeonTileType::HallwayFloorCrossroad, TileSet.HallwayFloorCrossroad, TileSet.HallwayFloorCrossroadRotationOffset,
							EDungeonTileType::HallwayFloorEndCap,    TileSet.HallwayFloorEndCap,    TileSet.HallwayFloorEndCapRotationOffset);

						const FRotator FloorRot(0.0f, VariantYaw, 0.0f);
						const FVector FS = FloorScale(VariantType);
						const float FloorHalfZ = MeshInfos[static_cast<int32>(VariantType)].Extent.Z * FS.Z * 0.5f;
						Out.Transforms[static_cast<int32>(VariantType)].Emplace(
							FTransform(FloorRot,
								CellCenter + PivotOffset(VariantType, FS, FloorRot) + FVector(0.0f, 0.0f, FloorHalfZ), FS));
					}
					else
					{
						const FVector FS = FloorScale(EDungeonTileType::RoomFloor);
						const float FloorHalfZ = MeshInfos[static_cast<int32>(EDungeonTileType::RoomFloor)].Extent.Z * FS.Z * 0.5f;
						Out.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Emplace(
							FTransform(FRotator::ZeroRotator,
								CellCenter + PivotOffset(EDungeonTileType::RoomFloor, FS, FRotator::ZeroRotator) + FVector(0.0f, 0.0f, FloorHalfZ), FS));
					}
				}

				// Ceiling: place if cell above is a different space, solid, or OOB.
				// Top face of the ceiling mesh is aligned flush with the cell's upper boundary.
				if (bHasCeilingMesh && NeedsVerticalBoundary(Result.Grid, Cell, X, Y, Z + 1))
				{
					const FVector CeilingPos = CellCenter + FVector(0.0f, 0.0f, CS);

					if (bIsHallway)
					{
						const auto [VariantType, VariantYaw] = SelectVariant(
							EDungeonTileType::HallwayCeiling,
							EDungeonTileType::HallwayCeilingStraight,  TileSet.HallwayCeilingStraight,  TileSet.HallwayCeilingStraightRotationOffset,
							EDungeonTileType::HallwayCeilingCorner,    TileSet.HallwayCeilingCorner,    TileSet.HallwayCeilingCornerRotationOffset,
							EDungeonTileType::HallwayCeilingTJunction, TileSet.HallwayCeilingTJunction, TileSet.HallwayCeilingTJunctionRotationOffset,
							EDungeonTileType::HallwayCeilingCrossroad, TileSet.HallwayCeilingCrossroad, TileSet.HallwayCeilingCrossroadRotationOffset,
							EDungeonTileType::HallwayCeilingEndCap,    TileSet.HallwayCeilingEndCap,    TileSet.HallwayCeilingEndCapRotationOffset);

						const FRotator CeilRot(0.0f, VariantYaw, 0.0f);
						const FVector CeilS = FloorScale(VariantType);
						const float CeilHalfZ = MeshInfos[static_cast<int32>(VariantType)].Extent.Z * CeilS.Z * 0.5f;
						Out.Transforms[static_cast<int32>(VariantType)].Emplace(
							FTransform(CeilRot,
								CeilingPos + PivotOffset(VariantType, CeilS, CeilRot) - FVector(0.0f, 0.0f, CeilHalfZ), CeilS));
					}
					else
					{
						const FVector CeilS = FloorScale(CeilingType);
						const float CeilHalfZ = MeshInfos[static_cast<int32>(CeilingType)].Extent.Z * CeilS.Z * 0.5f;
						Out.Transforms[static_cast<int32>(CeilingType)].Emplace(
							FTransform(FRotator::ZeroRotator,
								CeilingPos + PivotOffset(CeilingType, CeilS, FRotator::ZeroRotator) - FVector(0.0f, 0.0f, CeilHalfZ), CeilS));
					}
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
							|| Result.Grid.GetCell(NX, NY, Z).CellType == EDungeonCellType::RoomWall
							|| Result.Grid.GetCell(NX, NY, Z).CellType == EDungeonCellType::StaircaseHead;

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
					else if (bIsStaircase)
					{
						// Staircase cells: wall all faces except entry approach and same-staircase continuation.
						// Entry face (bottom approach): use normal NeedsWall (open to hallways, walled against solid).
						// Climb face (high/exit side): walled unless same-staircase or underpass enabled.
						// Side faces: always walled (prevents hallways clipping into staircase sides).
						const uint8 Dir = Cell.StaircaseDirection;
						const bool bIsClimbFace = (WC.DX == DX[Dir] && WC.DY == DY[Dir]);
						const bool bIsEntryFace = (WC.DX == -DX[Dir] && WC.DY == -DY[Dir]);

						bool bPlaceWall;

						if (bIsEntryFace)
						{
							// Entry: defer to standard logic, but open toward room-family cells
							// (staircase can attach directly to a room without an intermediate hallway)
							bPlaceWall = NeedsWall(Result.Grid, Cell, NX, NY, Z);
							if (bPlaceWall && Result.Grid.IsInBounds(NX, NY, Z))
							{
								const EDungeonCellType NType = Result.Grid.GetCell(NX, NY, Z).CellType;
								if (NType == EDungeonCellType::Room
									|| NType == EDungeonCellType::Door
									|| NType == EDungeonCellType::Entrance)
								{
									bPlaceWall = false;
								}
							}
						}
						else if (bIsClimbFace)
						{
							// Check for same-staircase continuation (multi-cell runs)
							bool bSameStaircase = false;
							if (Result.Grid.IsInBounds(NX, NY, Z))
							{
								const FDungeonCell& Neighbor = Result.Grid.GetCell(NX, NY, Z);
								bSameStaircase = (Neighbor.CellType == EDungeonCellType::Staircase
									|| Neighbor.CellType == EDungeonCellType::StaircaseHead)
									&& Neighbor.HallwayIndex == Cell.HallwayIndex;
							}

							if (bSameStaircase)
								bPlaceWall = false;
							else
								bPlaceWall = true;
						}
						else
						{
							// Side face: always wall
							bPlaceWall = true;
						}

						if (bPlaceWall && !TileSet.WallSegment.IsNull())
						{
							const FVector WS = WallScale(EDungeonTileType::WallSegment);
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(FaceRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, FaceRot), WS));
						}
					}
					else if (bIsStaircaseHead)
					{
						// StaircaseHead cells: climb/entry faces open to hallways, rooms, and same-staircase.
						// Side faces: restricted (only open to same-staircase cells).
						const uint8 Dir = Cell.StaircaseDirection;
						const bool bIsClimbFace = (WC.DX == DX[Dir] && WC.DY == DY[Dir]);
						const bool bIsEntryFace = (WC.DX == -DX[Dir] && WC.DY == -DY[Dir]);

						bool bPlaceWall;

						if (bIsClimbFace || bIsEntryFace)
						{
							// Open toward hallway-family, room-family, or same-staircase
							bPlaceWall = NeedsWall(Result.Grid, Cell, NX, NY, Z);
							if (bPlaceWall && Result.Grid.IsInBounds(NX, NY, Z))
							{
								const EDungeonCellType NType = Result.Grid.GetCell(NX, NY, Z).CellType;
								if (NType == EDungeonCellType::Hallway
									|| NType == EDungeonCellType::Room
									|| NType == EDungeonCellType::Door
									|| NType == EDungeonCellType::Entrance)
								{
									bPlaceWall = false;
								}
							}
						}
						else
						{
							// Side faces: defer to NeedsWall (StaircaseHead restriction applies)
							bPlaceWall = NeedsWall(Result.Grid, Cell, NX, NY, Z);
						}

						if (bPlaceWall && !TileSet.WallSegment.IsNull())
						{
							const FVector WS = WallScale(EDungeonTileType::WallSegment);
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(FaceRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, FaceRot), WS));
						}
					}
					else if (NeedsWall(Result.Grid, Cell, NX, NY, Z))
					{
						// Check if neighbor is a staircase with its entry facing us — door frame instead of wall
						bool bStaircaseEntry = false;
						// Check if neighbor is a StaircaseHead with climb/entry face toward us — skip wall
						bool bStaircaseHeadOpen = false;
						if (Result.Grid.IsInBounds(NX, NY, Z))
						{
							const FDungeonCell& Neighbor = Result.Grid.GetCell(NX, NY, Z);
							if (Neighbor.CellType == EDungeonCellType::Staircase)
							{
								// Room-to-staircase direction matches staircase's climb direction
								// means we're at the staircase's entry side (opposite of climb)
								bStaircaseEntry = (WC.DX == DX[Neighbor.StaircaseDirection]
									&& WC.DY == DY[Neighbor.StaircaseDirection]);
							}
							else if (Neighbor.CellType == EDungeonCellType::StaircaseHead)
							{
								// Face from StaircaseHead toward us is (-WC.DX, -WC.DY).
								// If that's the head's climb or entry face, don't wall.
								const uint8 HeadDir = Neighbor.StaircaseDirection;
								const bool bHeadClimb = (-WC.DX == DX[HeadDir] && -WC.DY == DY[HeadDir]);
								const bool bHeadEntry = (WC.DX == DX[HeadDir] && WC.DY == DY[HeadDir]);
								if (bHeadClimb || bHeadEntry)
								{
									bStaircaseHeadOpen = true;
								}
							}
						}

						if (bStaircaseHeadOpen)
						{
							// Don't place wall — StaircaseHead's climb/entry face is open
						}
						else if (bStaircaseEntry && !TileSet.DoorFrame.IsNull())
						{
							const FVector FS = WallScale(EDungeonTileType::DoorFrame);
							Out.Transforms[static_cast<int32>(EDungeonTileType::DoorFrame)].Emplace(
								FTransform(FaceRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::DoorFrame, FS, FaceRot), FS));
						}
						else if (!bStaircaseEntry && !TileSet.WallSegment.IsNull())
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

			// Compose directional yaw with user-configured mesh rotation offset.
			// Offset is applied first (mesh-local), then directional yaw (world-space).
			const FRotator DirectionalRot(0.0f, StairYaw, 0.0f);
			const FRotator StairRot = (DirectionalRot.Quaternion() * TileSet.StaircaseMeshRotationOffset.Quaternion()).Rotator();

			// Scale: target dimensions in standard convention are X=width(CS), Y=run(RunWorld), Z=rise(RiseWorld).
			// When StaircaseMeshRotationOffset is set, the mesh axes are rotated relative to convention,
			// so we rotate the target dimensions into mesh-local space before dividing by extent.
			const FVector& StairE = MeshInfos[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Extent;
			const FQuat InvOffsetQuat = TileSet.StaircaseMeshRotationOffset.Quaternion().Inverse();
			const FVector TargetLocal = InvOffsetQuat.RotateVector(FVector(CS, RunWorld, RiseWorld));
			const FVector StairScale(
				FMath::Abs(TargetLocal.X) / StairE.X,
				FMath::Abs(TargetLocal.Y) / StairE.Y,
				FMath::Abs(TargetLocal.Z) / StairE.Z);

			// Position: center of the ramp footprint, shifted up by floor depth so the
			// stair base sits on top of the floor tile. Extends into StaircaseHead cell above.
			const FVector RampCenter = (BottomCenter + TopCenter) * 0.5f + FVector(0.0f, 0.0f, Thin * 0.8f);
			const FVector StairPos = RampCenter + PivotOffset(EDungeonTileType::StaircaseMesh, StairScale, StairRot);

			Out.Transforms[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Emplace(
				FTransform(StairRot, StairPos, StairScale));
		}
	}

	UE_LOG(LogDungeonOutput, Log, TEXT("TileMapper: Generated %d instances across %d tile types"),
		Out.GetTotalInstanceCount(), FDungeonTileMapResult::TypeCount);

	return Out;
}
