#include "DungeonTileMapper.h"
#include "DungeonTileSet.h"
#include "DungeonTileModule.h"
#include "DungeonTypes.h"
#include "DungeonBoundaryRules.h"
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

// Wall / floor / ceiling decisions come from FDungeonBoundaryRules (DungeonCore), shared with
// the voxel stamper so the two backends can never disagree about which faces are open.

FDungeonTileMapResult FDungeonTileMapper::MapToTiles(
	const FDungeonResult& Result,
	const UDungeonTileSet& TileSet,
	const FVector& WorldOffset,
	bool bOpenEntranceCeiling)
{
	FDungeonTileMapResult Out;

	const FIntVector& GridSize = Result.Grid.GridSize;
	const float CS = Result.CellWorldSize;
	const float HalfCS = CS * 0.5f;
	const float Thin = TileThickness(CS);

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

	// Everything below reads from the consolidated per-type slots (TileSet.Slots) via getters.
	// Resolve each type's single-mesh once into a flat array; the rest of the setup is array-driven.
	TSoftObjectPtr<UStaticMesh> MeshForType[FDungeonTileMapResult::TypeCount];
	FVector ScaleMultipliers[FDungeonTileMapResult::TypeCount];
	FRotator BaseRotationOffsets[FDungeonTileMapResult::TypeCount];
	FMeshInfo MeshInfos[FDungeonTileMapResult::TypeCount];
	for (int32 i = 0; i < FDungeonTileMapResult::TypeCount; ++i)
	{
		const EDungeonTileType Type = static_cast<EDungeonTileType>(i);
		MeshForType[i] = TileSet.GetMesh(Type);
		ScaleMultipliers[i] = TileSet.GetScaleMultiplier(Type);
		BaseRotationOffsets[i] = TileSet.GetRotationOffset(Type);
		MeshInfos[i] = GetMeshInfo(MeshForType[i]);
	}

	// Hallway variants with no own mesh fall back to the base hallway floor/ceiling mesh info.
	auto FallbackVariantInfo = [&MeshForType, &MeshInfos](EDungeonTileType Variant, EDungeonTileType Base)
	{
		if (MeshForType[static_cast<int32>(Variant)].IsNull())
		{
			MeshInfos[static_cast<int32>(Variant)] = MeshInfos[static_cast<int32>(Base)];
		}
	};
	FallbackVariantInfo(EDungeonTileType::HallwayFloorStraight,  EDungeonTileType::HallwayFloor);
	FallbackVariantInfo(EDungeonTileType::HallwayFloorCorner,    EDungeonTileType::HallwayFloor);
	FallbackVariantInfo(EDungeonTileType::HallwayFloorTJunction, EDungeonTileType::HallwayFloor);
	FallbackVariantInfo(EDungeonTileType::HallwayFloorCrossroad, EDungeonTileType::HallwayFloor);
	FallbackVariantInfo(EDungeonTileType::HallwayFloorEndCap,    EDungeonTileType::HallwayFloor);
	FallbackVariantInfo(EDungeonTileType::HallwayCeilingStraight,  EDungeonTileType::HallwayCeiling);
	FallbackVariantInfo(EDungeonTileType::HallwayCeilingCorner,    EDungeonTileType::HallwayCeiling);
	FallbackVariantInfo(EDungeonTileType::HallwayCeilingTJunction, EDungeonTileType::HallwayCeiling);
	FallbackVariantInfo(EDungeonTileType::HallwayCeilingCrossroad, EDungeonTileType::HallwayCeiling);
	FallbackVariantInfo(EDungeonTileType::HallwayCeilingEndCap,    EDungeonTileType::HallwayCeiling);

	// Compose a placement rotation with a slot's configured offset (offset applied mesh-local first).
	auto ApplyRot = [&](EDungeonTileType Type, const FRotator& PlacementRot) -> FRotator
	{
		return (PlacementRot.Quaternion() * BaseRotationOffsets[static_cast<int32>(Type)].Quaternion()).Rotator();
	};

	// --- Module overrides (per tile type) ---
	// A type with a module is placed with a single UNIFORM cell scale (ActualCellSize / module
	// ReferenceCellSize) and NO pivot correction / slab lift — the module author owns the pieces'
	// positions. The scale/pivot/half-Z helpers below short-circuit for such types; ADungeonActor
	// expands the module's elements at render time. The stored per-type transform is thus a clean
	// anchor (position + rotation + uniform scale).
	bool bTypeIsModule[FDungeonTileMapResult::TypeCount] = {};
	float ModuleUniformScale[FDungeonTileMapResult::TypeCount];
	for (int32 i = 0; i < FDungeonTileMapResult::TypeCount; ++i)
	{
		ModuleUniformScale[i] = 1.0f;
		// StaircaseMesh is a bespoke ramp, not a per-cell tile — modules unsupported (actor mirrors this).
		if (static_cast<EDungeonTileType>(i) == EDungeonTileType::StaircaseMesh) { continue; }
		const TSoftObjectPtr<UDungeonTileModule> ModulePtr = TileSet.GetModule(static_cast<EDungeonTileType>(i));
		if (ModulePtr.IsNull()) { continue; }
		const UDungeonTileModule* Module = ModulePtr.LoadSynchronous();
		if (Module && Module->HasGeometry())
		{
			bTypeIsModule[i] = true;
			ModuleUniformScale[i] = Module->GetUniformScale(CS);
		}
	}

	// A slot renders if it has a module OR a non-null mesh.
	auto SlotActive = [&](EDungeonTileType Type) -> bool
	{
		return bTypeIsModule[static_cast<int32>(Type)] || !MeshForType[static_cast<int32>(Type)].IsNull();
	};

	// Floor/ceiling target: CS × CS × Thin — mesh local axes: X=CS, Y=CS, Z=Thin
	// ScaleMultiplier is applied on top so users can fine-tune without fighting auto-fit.
	auto FloorScale = [&](EDungeonTileType Type) -> FVector
	{
		const int32 Idx = static_cast<int32>(Type);
		if (bTypeIsModule[Idx]) { return FVector(ModuleUniformScale[Idx]); }
		const FVector& E = MeshInfos[Idx].Extent;
		const FVector& M = ScaleMultipliers[Idx];
		return FVector(CS / E.X * M.X, CS / E.Y * M.Y, Thin / E.Z * M.Z);
	};

	// Wall target: Thin × CS × CS — mesh local X=thin, Y=width, Z=height (pre-rotation)
	auto WallScale = [&](EDungeonTileType Type) -> FVector
	{
		const int32 Idx = static_cast<int32>(Type);
		if (bTypeIsModule[Idx]) { return FVector(ModuleUniformScale[Idx]); }
		const FVector& M = ScaleMultipliers[Idx];
		const FVector& E = MeshInfos[Idx].Extent;
		return FVector(Thin / E.X * M.X, CS / E.Y * M.Y, CS / E.Z * M.Z);
	};

	// Pivot correction: offset placement so the mesh's bounding box center
	// lands at the intended position, regardless of where the pivot is.
	// PivotOffset = -Rotation.RotateVector(BoundsCenter * Scale). Zero for modules (author owns pivots).
	auto PivotOffset = [&](EDungeonTileType Type, const FVector& Scale, const FRotator& Rotation) -> FVector
	{
		if (bTypeIsModule[static_cast<int32>(Type)]) { return FVector::ZeroVector; }
		const FVector& C = MeshInfos[static_cast<int32>(Type)].Center;
		return -Rotation.RotateVector(C * Scale);
	};

	// Half the slab thickness along Z, used to seat floor/ceiling meshes flush with the cell
	// boundary. Zero for modules (the author positions pieces relative to the anchor).
	auto SlabHalfZ = [&](EDungeonTileType Type, const FVector& Scale) -> float
	{
		if (bTypeIsModule[static_cast<int32>(Type)]) { return 0.0f; }
		return MeshInfos[static_cast<int32>(Type)].Extent.Z * Scale.Z * 0.5f;
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

				// Check slot availability (mesh OR module)
				const bool bHasFloorMesh = bIsHallway
					? SlotActive(EDungeonTileType::HallwayFloor)
					: SlotActive(EDungeonTileType::RoomFloor);
				const bool bHasCeilingMesh = bIsHallway
					? SlotActive(EDungeonTileType::HallwayCeiling)
					: SlotActive(EDungeonTileType::RoomCeiling);

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
				// A variant is used if its slot has a module OR a mesh; else fall back to baseType.
				// Each variant's own rotation offset (from its slot) composes with the connectivity yaw.
				auto SelectVariant = [&](
					EDungeonTileType BaseType,
					EDungeonTileType StraightType, EDungeonTileType CornerType, EDungeonTileType TJuncType,
					EDungeonTileType CrossType, EDungeonTileType EndCapType)
					-> TPair<EDungeonTileType, float>
				{
					auto Off = [&](EDungeonTileType T) -> const FRotator& { return BaseRotationOffsets[static_cast<int32>(T)]; };
					switch (HallwayShape)
					{
					case ShapeEndCap:
						if (SlotActive(EndCapType)) return {EndCapType, ComposeYaw(BaseEndCapYaw, Off(EndCapType))};
						break;
					case ShapeStraight:
						if (SlotActive(StraightType)) return {StraightType, ComposeYaw(BaseStraightYaw, Off(StraightType))};
						break;
					case ShapeCorner:
						if (SlotActive(CornerType)) return {CornerType, ComposeYaw(BaseCornerYaw, Off(CornerType))};
						break;
					case ShapeTJunction:
						if (SlotActive(TJuncType)) return {TJuncType, ComposeYaw(BaseTJuncYaw, Off(TJuncType))};
						break;
					case ShapeCrossroad:
						if (SlotActive(CrossType)) return {CrossType, ComposeYaw(BaseCrossroadYaw, Off(CrossType))};
						break;
					default:
						break;
					}
					return {BaseType, 0.0f};
				};

				// Floor: place if cell below is a different space, solid, or OOB.
				// Bottom face of the floor mesh is aligned flush with the cell's lower boundary.
				if (bHasFloorMesh && FDungeonBoundaryRules::NeedsVerticalBoundary(Result.Grid, FIntVector(X, Y, Z), X, Y, Z - 1))
				{
					if (bIsHallway)
					{
						const auto [VariantType, VariantYaw] = SelectVariant(
							EDungeonTileType::HallwayFloor,
							EDungeonTileType::HallwayFloorStraight,  EDungeonTileType::HallwayFloorCorner,
							EDungeonTileType::HallwayFloorTJunction, EDungeonTileType::HallwayFloorCrossroad,
							EDungeonTileType::HallwayFloorEndCap);

						const FRotator FloorRot(0.0f, VariantYaw, 0.0f);
						const FVector FS = FloorScale(VariantType);
						const float FloorHalfZ = SlabHalfZ(VariantType, FS);
						Out.Transforms[static_cast<int32>(VariantType)].Emplace(
							FTransform(FloorRot,
								CellCenter + PivotOffset(VariantType, FS, FloorRot) + FVector(0.0f, 0.0f, FloorHalfZ), FS));
					}
					else
					{
						const FVector FS = FloorScale(EDungeonTileType::RoomFloor);
						const FRotator FloorRot = ApplyRot(EDungeonTileType::RoomFloor, FRotator::ZeroRotator);
						const float FloorHalfZ = SlabHalfZ(EDungeonTileType::RoomFloor, FS);
						Out.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Emplace(
							FTransform(FloorRot,
								CellCenter + PivotOffset(EDungeonTileType::RoomFloor, FS, FloorRot) + FVector(0.0f, 0.0f, FloorHalfZ), FS));
					}
				}

				// Ceiling: place if cell above is a different space, solid, or OOB.
				// Top face of the ceiling mesh is aligned flush with the cell's upper boundary.
				// The designated entrance cell's ceiling stays OPEN when a vertical passage
				// enters from above (bOpenEntranceCeiling).
				const bool bIsOpenEntranceCeiling = bOpenEntranceCeiling
					&& X == Result.EntranceCell.X && Y == Result.EntranceCell.Y && Z == Result.EntranceCell.Z;
				if (bHasCeilingMesh && !bIsOpenEntranceCeiling && FDungeonBoundaryRules::NeedsVerticalBoundary(Result.Grid, FIntVector(X, Y, Z), X, Y, Z + 1))
				{
					const FVector CeilingPos = CellCenter + FVector(0.0f, 0.0f, CS);

					if (bIsHallway)
					{
						const auto [VariantType, VariantYaw] = SelectVariant(
							EDungeonTileType::HallwayCeiling,
							EDungeonTileType::HallwayCeilingStraight,  EDungeonTileType::HallwayCeilingCorner,
							EDungeonTileType::HallwayCeilingTJunction, EDungeonTileType::HallwayCeilingCrossroad,
							EDungeonTileType::HallwayCeilingEndCap);

						const FRotator CeilRot(0.0f, VariantYaw, 0.0f);
						const FVector CeilS = FloorScale(VariantType);
						const float CeilHalfZ = SlabHalfZ(VariantType, CeilS);
						Out.Transforms[static_cast<int32>(VariantType)].Emplace(
							FTransform(CeilRot,
								CeilingPos + PivotOffset(VariantType, CeilS, CeilRot) - FVector(0.0f, 0.0f, CeilHalfZ), CeilS));
					}
					else
					{
						const FVector CeilS = FloorScale(CeilingType);
						const FRotator CeilRot = ApplyRot(CeilingType, FRotator::ZeroRotator);
						const float CeilHalfZ = SlabHalfZ(CeilingType, CeilS);
						Out.Transforms[static_cast<int32>(CeilingType)].Emplace(
							FTransform(CeilRot,
								CeilingPos + PivotOffset(CeilingType, CeilS, CeilRot) - FVector(0.0f, 0.0f, CeilHalfZ), CeilS));
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
					// Per-face placement rotation composed with each wall-family slot's offset, so a
					// mesh whose finished face points the wrong way can be flipped in data (Yaw=180).
					const FRotator WallRot = ApplyRot(EDungeonTileType::WallSegment, FaceRot);
					const FRotator DoorRot = ApplyRot(EDungeonTileType::DoorFrame, FaceRot);
					const FRotator EntranceRot = ApplyRot(EDungeonTileType::EntranceFrame, FaceRot);

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
							if (SlotActive(EDungeonTileType::WallSegment))
							{
								const FVector WS = WallScale(EDungeonTileType::WallSegment);
								Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
									FTransform(WallRot,
										CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, WallRot), WS));
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
								const FRotator FrameRot = bIsDoor ? DoorRot : EntranceRot;

								const bool bHasFrameMesh = bIsDoor
									? SlotActive(EDungeonTileType::DoorFrame)
									: SlotActive(EDungeonTileType::EntranceFrame);

								if (bHasFrameMesh)
								{
									const FVector FS = WallScale(FrameType);
									Out.Transforms[static_cast<int32>(FrameType)].Emplace(
										FTransform(FrameRot,
											CellCenter + WC.Offset + PivotOffset(FrameType, FS, FrameRot), FS));
								}
							}
						}
					}
					else if (bIsStaircase)
					{
						// Staircase cells: wall all faces except entry approach and same-staircase continuation.
						// Entry face (bottom approach): use normal NeedsWall (open to hallways, walled against solid).
						// Climb face (high/exit side): walled unless same-staircase or underpass enabled.
						// Flank faces: FDungeonBoundaryRules (rule 5) walls them from both sides; the stair
						// cell itself only places that wall against a solid neighbour. An open neighbour
						// places the wall on ITS face, so a wall module inset into this cell never cuts
						// through the ramp mesh.
						const uint8 Dir = Cell.StaircaseDirection;
						const bool bIsClimbFace = (WC.DX == DX[Dir] && WC.DY == DY[Dir]);
						const bool bIsEntryFace = (WC.DX == -DX[Dir] && WC.DY == -DY[Dir]);

						bool bPlaceWall;

						if (bIsEntryFace)
						{
							// Entry: defer to standard logic, but open toward room-family cells
							// (staircase can attach directly to a room without an intermediate hallway)
							bPlaceWall = FDungeonBoundaryRules::NeedsWall(Result.Grid, FIntVector(X, Y, Z), NX, NY, Z);
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
							// Flank: shared rule, own wall only against solid / OOB (see above).
							bPlaceWall = FDungeonBoundaryRules::NeedsWall(Result.Grid, FIntVector(X, Y, Z), NX, NY, Z)
								&& !(Result.Grid.IsInBounds(NX, NY, Z)
									&& FDungeonBoundaryRules::IsOpenCell(Result.Grid.GetCell(NX, NY, Z).CellType));
						}

						if (bPlaceWall && SlotActive(EDungeonTileType::WallSegment))
						{
							const FVector WS = WallScale(EDungeonTileType::WallSegment);
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(WallRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, WallRot), WS));
						}
					}
					else if (bIsStaircaseHead)
					{
						// StaircaseHead cells: climb/entry faces open to hallways, rooms, and same-staircase.
						// Flank faces: always walls (shared rule 5); as for Staircase cells, the head only
						// places its own flank wall against a solid neighbour.
						const uint8 Dir = Cell.StaircaseDirection;
						const bool bIsClimbFace = (WC.DX == DX[Dir] && WC.DY == DY[Dir]);
						const bool bIsEntryFace = (WC.DX == -DX[Dir] && WC.DY == -DY[Dir]);

						bool bPlaceWall;

						if (bIsClimbFace || bIsEntryFace)
						{
							// Open toward hallway-family, room-family, or same-staircase
							bPlaceWall = FDungeonBoundaryRules::NeedsWall(Result.Grid, FIntVector(X, Y, Z), NX, NY, Z);
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
							// Flank: shared rule, own wall only against solid / OOB.
							bPlaceWall = FDungeonBoundaryRules::NeedsWall(Result.Grid, FIntVector(X, Y, Z), NX, NY, Z)
								&& !(Result.Grid.IsInBounds(NX, NY, Z)
									&& FDungeonBoundaryRules::IsOpenCell(Result.Grid.GetCell(NX, NY, Z).CellType));
						}

						if (bPlaceWall && SlotActive(EDungeonTileType::WallSegment))
						{
							const FVector WS = WallScale(EDungeonTileType::WallSegment);
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(WallRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, WallRot), WS));
						}
					}
					else if (FDungeonBoundaryRules::NeedsWall(Result.Grid, FIntVector(X, Y, Z), NX, NY, Z))
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
						else if (bStaircaseEntry && SlotActive(EDungeonTileType::DoorFrame))
						{
							const FVector FS = WallScale(EDungeonTileType::DoorFrame);
							Out.Transforms[static_cast<int32>(EDungeonTileType::DoorFrame)].Emplace(
								FTransform(DoorRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::DoorFrame, FS, DoorRot), FS));
						}
						else if (!bStaircaseEntry && SlotActive(EDungeonTileType::WallSegment))
						{
							const FVector WS = WallScale(EDungeonTileType::WallSegment);
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(WallRot,
									CellCenter + WC.Offset + PivotOffset(EDungeonTileType::WallSegment, WS, WallRot), WS));
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
	const TSoftObjectPtr<UStaticMesh> StaircaseMeshPtr = TileSet.GetMesh(EDungeonTileType::StaircaseMesh);
	const FRotator StaircaseRotationOffset = TileSet.GetRotationOffset(EDungeonTileType::StaircaseMesh);
	if (!StaircaseMeshPtr.IsNull())
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
			const FRotator StairRot = (DirectionalRot.Quaternion() * StaircaseRotationOffset.Quaternion()).Rotator();

			// Scale: target dimensions in standard convention are X=width(CS), Y=run(RunWorld), Z=rise(RiseWorld).
			// When StaircaseMeshRotationOffset is set, the mesh axes are rotated relative to convention,
			// so we rotate the target dimensions into mesh-local space before dividing by extent.
			const FVector& StairE = MeshInfos[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Extent;
			const FQuat InvOffsetQuat = StaircaseRotationOffset.Quaternion().Inverse();
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
