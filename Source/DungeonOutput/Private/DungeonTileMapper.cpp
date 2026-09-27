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
	Openings.Reset();
	Fixtures.Reset();
}

uint32 FDungeonTileMapper::MakeInteractableId(const FIntVector& Cell, int32 FaceDX, int32 FaceDY, uint8 Kind)
{
	// FNV-1a over the six fields in a fixed byte order: platform- and version-stable, unlike
	// GetTypeHash / HashCombine which are free to change between engine releases.
	uint32 Hash = 2166136261u;
	auto Fold = [&Hash](int32 V)
	{
		for (int32 B = 0; B < 4; ++B)
		{
			Hash ^= static_cast<uint8>((static_cast<uint32>(V) >> (B * 8)) & 0xFFu);
			Hash *= 16777619u;
		}
	};
	Fold(Cell.X); Fold(Cell.Y); Fold(Cell.Z); Fold(FaceDX); Fold(FaceDY); Fold(Kind);
	return Hash;
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

	// The entrance opening (bOpenEntranceCeiling): which cell, and which of its faces, the
	// stitched passage enters through. Decided from the dungeon's recorded approach so the tiles
	// and the voxel carve agree:
	//   FromAbove (and legacy None): the LID of the top cell of the entrance room's column over
	//     EntranceCell. For a two-floor entrance room the ground-floor cell has no ceiling of its
	//     own (same room above), so skipping "its" ceiling would leave the lid closed.
	//   FromBelow: the FLOOR of the opening cell.
	//   FromSide:  the WALL on the resolved face of the opening cell.
	const FDungeonEntranceApproachInfo& Approach = Result.EntranceApproach;
	const bool bApproachKnown = Approach.Approach != EDungeonEntranceApproach::None && Approach.bSatisfied;
	const EDungeonEntranceApproach OpeningKind = bApproachKnown ? Approach.Approach : EDungeonEntranceApproach::FromAbove;
	const FIntVector OpeningCell = bApproachKnown ? Approach.OpeningCell : Result.GetEntranceOpeningCell();
	int32 OpeningFaceDX = 0, OpeningFaceDY = 0;
	if (OpeningKind == EDungeonEntranceApproach::FromSide)
	{
		switch (Approach.Face)
		{
		case EDungeonGridFace::MinX: OpeningFaceDX = -1; break;
		case EDungeonGridFace::MaxX: OpeningFaceDX = +1; break;
		case EDungeonGridFace::MinY: OpeningFaceDY = -1; break;
		case EDungeonGridFace::MaxY: OpeningFaceDY = +1; break;
		default: break;
		}
	}

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
	// The wall profile, scaled to this cell size. Single wall meshes are fitted to it: a
	// rock-backed wall or frame keeps the TileThickness slab but is shifted so its finished face
	// lands FaceInset inside the plane; a partition is PartitionThickness thick, centred on the
	// plane. Modules are authored to the profile and placed as-is (the conformance check proves it).
	const FDungeonWallProfile& Profile = TileSet.WallProfile;
	const float ProfileScale = CS / FMath::Max(Profile.ReferenceCellSize, 1.0f);
	const float FaceInsetW = Profile.FaceInset * ProfileScale;
	const float PartitionThickW = Profile.PartitionThickness * ProfileScale;

	auto WallScale = [&](EDungeonTileType Type) -> FVector
	{
		const int32 Idx = static_cast<int32>(Type);
		if (bTypeIsModule[Idx]) { return FVector(ModuleUniformScale[Idx]); }
		const FVector& M = ScaleMultipliers[Idx];
		const FVector& E = MeshInfos[Idx].Extent;
		const float Thickness = (Type == EDungeonTileType::WallPartition) ? PartitionThickW : Thin;
		return FVector(Thickness / E.X * M.X, CS / E.Y * M.Y, CS / E.Z * M.Z);
	};

	// Where a single wall mesh's slab sits relative to the face plane: the auto-fit centres it on
	// the plane (face at -Thin/2), so a rock-backed piece slides outward by (Thin/2 - FaceInset)
	// to put its finished face at the profile inset. Partitions stay centred. Zero for modules.
	auto WallFaceShift = [&](EDungeonTileType Type, int32 DX, int32 DY) -> FVector
	{
		const int32 Idx = static_cast<int32>(Type);
		if (bTypeIsModule[Idx] || Type == EDungeonTileType::WallPartition) { return FVector::ZeroVector; }
		const float Shift = Thin * 0.5f - FaceInsetW;
		return FVector(DX * Shift, DY * Shift, 0.0f);
	};

	// Framed openings (E3): one record per frame site, whether or not the frame slot has geometry,
	// so gameplay can hang door leaves in Doorway openings. Frame = face centre on the cell floor,
	// +X into the neighbour. LeafHinge = the hinge line on the finished-face plane at the -Y jamb
	// of the opening (local frame), +Y running along the wall toward the opening centre.
	auto EmitOpening = [&](EDungeonOpeningKind Kind, const FIntVector& C, int32 FDX, int32 FDY)
	{
		FDungeonOpening O;
		O.Cell = C;
		O.FaceDX = FDX;
		O.FaceDY = FDY;
		O.Kind = Kind;
		const FVector Base = Result.GridToWorld(C) + WorldOffset;
		const FVector FaceCentre = Base + FVector(HalfCS + FDX * HalfCS, HalfCS + FDY * HalfCS, 0.0f);
		const FRotator Rot(0.0f, FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(FDY), static_cast<float>(FDX))), 0.0f);
		O.Frame = FTransform(Rot, FaceCentre, FVector(ProfileScale));
		O.LeafWidth = Profile.DoorLeafWidth * ProfileScale;
		O.LeafHeight = Profile.DoorLeafHeight * ProfileScale;
		const FVector HingeLocal(-FaceInsetW + Profile.HingeInset * ProfileScale, -O.LeafWidth * 0.5f, 0.0f);
		O.LeafHinge = FTransform(Rot, FaceCentre + Rot.RotateVector(HingeLocal), FVector(ProfileScale));
		Out.Openings.Add(O);
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

				const bool bIsOpeningCell = bOpenEntranceCeiling
					&& X == OpeningCell.X && Y == OpeningCell.Y && Z == OpeningCell.Z;

				// Floor: place if cell below is a different space, solid, or OOB.
				// Bottom face of the floor mesh is aligned flush with the cell's lower boundary.
				// The opening cell's floor stays OPEN for a passage rising from below.
				const bool bIsOpenEntranceFloor = bIsOpeningCell && OpeningKind == EDungeonEntranceApproach::FromBelow;
				if (bHasFloorMesh && !bIsOpenEntranceFloor && FDungeonBoundaryRules::NeedsVerticalBoundary(Result.Grid, FIntVector(X, Y, Z), X, Y, Z - 1))
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
				// The entrance room's lid (ceiling of the opening cell) stays OPEN when a vertical
				// passage enters from above (bOpenEntranceCeiling).
				const bool bIsOpenEntranceCeiling = bIsOpeningCell && OpeningKind == EDungeonEntranceApproach::FromAbove;
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
					// The opening cell's wall on the approach face stays OPEN for a side tunnel
					// (the stitcher carves the corridor right up to this face).
					if (bIsOpeningCell && OpeningKind == EDungeonEntranceApproach::FromSide
						&& WC.DX == OpeningFaceDX && WC.DY == OpeningFaceDY)
					{
						continue;
					}

					const int32 NX = X + WC.DX;
					const int32 NY = Y + WC.DY;
					const FRotator FaceRot(0.0f, WC.Yaw, 0.0f);
					// Per-face placement rotation composed with each wall-family slot's offset, so a
					// mesh whose finished face points the wrong way can be flipped in data (Yaw=180).
					const FRotator WallRot = ApplyRot(EDungeonTileType::WallSegment, FaceRot);
					const FRotator PartitionRot = ApplyRot(EDungeonTileType::WallPartition, FaceRot);
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
										CellCenter + WC.Offset + WallFaceShift(EDungeonTileType::WallSegment, WC.DX, WC.DY)
											+ PivotOffset(EDungeonTileType::WallSegment, WS, WallRot), WS));
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

								EmitOpening(bIsDoor ? EDungeonOpeningKind::Doorway : EDungeonOpeningKind::EntranceOpening,
									FIntVector(X, Y, Z), WC.DX, WC.DY);

								const bool bHasFrameMesh = bIsDoor
									? SlotActive(EDungeonTileType::DoorFrame)
									: SlotActive(EDungeonTileType::EntranceFrame);

								if (bHasFrameMesh)
								{
									const FVector FS = WallScale(FrameType);
									Out.Transforms[static_cast<int32>(FrameType)].Emplace(
										FTransform(FrameRot,
											CellCenter + WC.Offset + WallFaceShift(FrameType, WC.DX, WC.DY)
												+ PivotOffset(FrameType, FS, FrameRot), FS));
								}
							}
						}
					}
					else if (bIsStaircase || bIsStaircaseHead)
					{
						// Every staircase face (flank, entry, climb, headroom level) is decided by
						// FDungeonBoundaryRules. The stair cell places its own wall only against a
						// solid / OOB neighbour: an open neighbour walls its side of the face, so a wall
						// module inset into this cell never cuts through the ramp mesh.
						const bool bNeighborOpen = Result.Grid.IsInBounds(NX, NY, Z)
							&& FDungeonBoundaryRules::IsOpenCell(Result.Grid.GetCell(NX, NY, Z).CellType);
						if (!bNeighborOpen
							&& FDungeonBoundaryRules::NeedsWall(Result.Grid, FIntVector(X, Y, Z), NX, NY, Z)
							&& SlotActive(EDungeonTileType::WallSegment))
						{
							const FVector WS = WallScale(EDungeonTileType::WallSegment);
							Out.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Emplace(
								FTransform(WallRot,
									CellCenter + WC.Offset + WallFaceShift(EDungeonTileType::WallSegment, WC.DX, WC.DY)
										+ PivotOffset(EDungeonTileType::WallSegment, WS, WallRot), WS));
						}
					}
					else
					{
						const bool bWall = FDungeonBoundaryRules::NeedsWall(Result.Grid, FIntVector(X, Y, Z), NX, NY, Z);

						// A ramp entering this room cell directly (no Door cell between): the shared
						// rules leave the ramp's entry face open; frame the opening like a doorway.
						bool bStaircaseEntry = false;
						if (!bWall && !bIsHallway && Result.Grid.IsInBounds(NX, NY, Z))
						{
							const FDungeonCell& Neighbor = Result.Grid.GetCell(NX, NY, Z);
							bStaircaseEntry = Neighbor.CellType == EDungeonCellType::Staircase
								&& WC.DX == DX[Neighbor.StaircaseDirection]
								&& WC.DY == DY[Neighbor.StaircaseDirection];
						}

						if (bStaircaseEntry)
						{
							EmitOpening(EDungeonOpeningKind::StairEntry, FIntVector(X, Y, Z), WC.DX, WC.DY);
							if (SlotActive(EDungeonTileType::DoorFrame))
							{
								const FVector FS = WallScale(EDungeonTileType::DoorFrame);
								Out.Transforms[static_cast<int32>(EDungeonTileType::DoorFrame)].Emplace(
									FTransform(DoorRot,
										CellCenter + WC.Offset + WallFaceShift(EDungeonTileType::DoorFrame, WC.DX, WC.DY)
											+ PivotOffset(EDungeonTileType::DoorFrame, FS, DoorRot), FS));
							}
						}
						else if (bWall)
						{
							// A face shared with another OPEN cell (room beside corridor, landing beside a
							// ramp flank) needs a wall from both sides, but is dressed ONCE, by the owner,
							// with the two-faced WallPartition piece (WallSegment when that slot is unset).
							// The non-owner places nothing. Rock-backed faces keep WallSegment.
							const bool bNeighborOpen = Result.Grid.IsInBounds(NX, NY, Z)
								&& FDungeonBoundaryRules::IsOpenCell(Result.Grid.GetCell(NX, NY, Z).CellType);
							if (bNeighborOpen && !FDungeonBoundaryRules::OwnsSharedFace(Result.Grid, FIntVector(X, Y, Z), WC.DX, WC.DY))
							{
								continue;
							}
							const EDungeonTileType WallType = (bNeighborOpen && SlotActive(EDungeonTileType::WallPartition))
								? EDungeonTileType::WallPartition
								: EDungeonTileType::WallSegment;
							if (SlotActive(WallType))
							{
								const FRotator Rot = (WallType == EDungeonTileType::WallPartition) ? PartitionRot : WallRot;
								const FVector WS = WallScale(WallType);
								Out.Transforms[static_cast<int32>(WallType)].Emplace(
									FTransform(Rot,
										CellCenter + WC.Offset + WallFaceShift(WallType, WC.DX, WC.DY)
											+ PivotOffset(WallType, WS, Rot), WS));
							}
						}
					}
				}
			}
		}
	}

	// --- Corner posts (optional slots) ---
	// Placed once per corner point on the cell floor. Inner: two walled faces of one open cell
	// meet. Outer: a wall run ends at an open face that carries no frame (the mouth of a side
	// corridor, a room wall stopping at an opening); never beside a door / entrance frame, whose
	// jambs cover that corner. "Walled" = NeedsWall, whoever owns the piece.
	const bool bInnerCorners = SlotActive(EDungeonTileType::WallCornerInner);
	const bool bOuterCorners = SlotActive(EDungeonTileType::WallCornerOuter);
	if (bInnerCorners || bOuterCorners)
	{
		auto CornerScale = [&](EDungeonTileType Type) -> FVector
		{
			const int32 Idx = static_cast<int32>(Type);
			return bTypeIsModule[Idx] ? FVector(ModuleUniformScale[Idx]) : FVector(ProfileScale);
		};
		auto IsFrameCell = [](EDungeonCellType T)
		{
			return T == EDungeonCellType::Door || T == EDungeonCellType::Entrance;
		};
		auto CellTypeAt = [&](int32 CX, int32 CY, int32 CZ) -> EDungeonCellType
		{
			return Result.Grid.IsInBounds(CX, CY, CZ) ? Result.Grid.GetCell(CX, CY, CZ).CellType : EDungeonCellType::Empty;
		};
		auto Walled = [&](const FIntVector& C, int32 WDX, int32 WDY) -> bool
		{
			return FDungeonBoundaryRules::NeedsWall(Result.Grid, C, C.X + WDX, C.Y + WDY, C.Z);
		};

		// How far inside the cell plane a walled face's visible surface sits: a rock-backed wall is
		// finished at FaceInset; a partition (open neighbour) is centred on the plane, so its face
		// is half its thickness in. Posts are pulled in by this along each walled face's normal so
		// they stand ON the face instead of hidden behind the inset.
		auto FaceDepth = [&](const FIntVector& C, int32 WDX, int32 WDY) -> float
		{
			const int32 NX = C.X + WDX, NY = C.Y + WDY;
			const bool bNeighborOpen = Result.Grid.IsInBounds(NX, NY, C.Z)
				&& FDungeonBoundaryRules::IsOpenCell(Result.Grid.GetCell(NX, NY, C.Z).CellType);
			return (bNeighborOpen && SlotActive(EDungeonTileType::WallPartition)) ? PartitionThickW * 0.5f : FaceInsetW;
		};

		// Corner lattice key: doubled cell coords + face offsets, so a point shared by up to four
		// cells maps to one key. Inset = pull-in along each walled face's normal (see FaceDepth).
		TSet<FIntVector> Placed;
		auto Place = [&](EDungeonTileType Type, const FIntVector& C, int32 CDXv, int32 CDYv, const FVector& Inset, float Yaw)
		{
			const FIntVector Key(2 * C.X + CDXv, 2 * C.Y + CDYv, C.Z);
			if (Placed.Contains(Key)) { return; }
			Placed.Add(Key);
			const FVector Base = Result.GridToWorld(C) + WorldOffset;
			const FVector Corner = Base + FVector(HalfCS + CDXv * HalfCS, HalfCS + CDYv * HalfCS, 0.0f) + Inset;
			const FRotator Rot = ApplyRot(Type, FRotator(0.0f, Yaw, 0.0f));
			Out.Transforms[static_cast<int32>(Type)].Emplace(FTransform(Rot, Corner, CornerScale(Type)));
		};

		for (int32 Z = 0; Z < GridSize.Z; ++Z)
		for (int32 Y = 0; Y < GridSize.Y; ++Y)
		for (int32 X = 0; X < GridSize.X; ++X)
		{
			const FIntVector C(X, Y, Z);
			const EDungeonCellType T = Result.Grid.GetCell(C).CellType;
			if (!FDungeonBoundaryRules::IsOpenCell(T)
				|| T == EDungeonCellType::Staircase || T == EDungeonCellType::StaircaseHead)
			{
				continue;
			}
			for (int32 A = 0; A < 2; ++A)          // X faces: +X, -X
			for (int32 B = 2; B < 4; ++B)          // Y faces: +Y, -Y
			{
				const int32 DXA = DX[A], DYB = DY[B];
				const bool bWalledA = Walled(C, DXA, 0);
				const bool bWalledB = Walled(C, 0, DYB);

				if (bWalledA && bWalledB)
				{
					if (bInnerCorners)
					{
						// +X points along the diagonal into the cell; pulled onto both wall faces.
						const FVector Inset(-DXA * FaceDepth(C, DXA, 0), -DYB * FaceDepth(C, 0, DYB), 0.0f);
						Place(EDungeonTileType::WallCornerInner, C, DXA, DYB, Inset,
							FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(-DYB), static_cast<float>(-DXA))));
					}
					continue;
				}
				if (!bOuterCorners || IsFrameCell(T) || bWalledA == bWalledB)
				{
					continue;
				}
				// Exactly one of the two faces is walled: does that wall continue into the
				// neighbour across the open face? If not, it ends here.
				const int32 WDX = bWalledA ? DXA : 0, WDY = bWalledA ? 0 : DYB;   // the walled face
				const int32 ODX = bWalledA ? 0 : DXA, ODY = bWalledA ? DYB : 0;   // the open face
				const FIntVector N(C.X + ODX, C.Y + ODY, C.Z);
				const EDungeonCellType NT = CellTypeAt(N.X, N.Y, N.Z);
				if (!FDungeonBoundaryRules::IsOpenCell(NT) || IsFrameCell(NT)
					|| NT == EDungeonCellType::Staircase || NT == EDungeonCellType::StaircaseHead)
				{
					continue;
				}
				if (Walled(N, WDX, WDY))
				{
					continue; // the wall runs on
				}
				// The wall stops because the neighbour's face is an opening, unless that opening
				// is a door / entrance frame (its jambs cover the corner).
				if (IsFrameCell(CellTypeAt(N.X + WDX, N.Y + WDY, N.Z)))
				{
					continue;
				}
				// +X points along the wall's direction of travel, into the open face; pulled onto
				// the ending wall's face (nothing along the open face, the post marks the wall end).
				const float Depth = FaceDepth(C, WDX, WDY);
				const FVector Inset(-WDX * Depth, -WDY * Depth, 0.0f);
				Place(EDungeonTileType::WallCornerOuter, C, DXA, DYB, Inset,
					FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(ODY), static_cast<float>(ODX))));
			}
		}
	}

	// --- Wall fixtures (E3): wall lights per the tileset's FDungeonFixtureRules ---
	// Doorway lights first (both side walls of every Door cell, at the room end of the jamb
	// passage), then every Nth rock-backed wall face of each room, until the cap. Only
	// rock-backed faces (the neighbour is solid) carry fixtures: a partition is seen from both
	// sides and its owner may be the other cell.
	{
		const FDungeonFixtureRules& Rules = TileSet.FixtureRules;
		const float MountZ = Rules.WallLightHeight * ProfileScale;
		TSet<FIntVector> UsedFaces;
		auto RockBacked = [&](const FIntVector& C, int32 FDX, int32 FDY) -> bool
		{
			const int32 NX = C.X + FDX, NY = C.Y + FDY;
			const bool bNeighborOpen = Result.Grid.IsInBounds(NX, NY, C.Z)
				&& FDungeonBoundaryRules::IsOpenCell(Result.Grid.GetCell(NX, NY, C.Z).CellType);
			return !bNeighborOpen && FDungeonBoundaryRules::NeedsWall(Result.Grid, C, NX, NY, C.Z);
		};
		auto EmitFixture = [&](const FIntVector& C, int32 FDX, int32 FDY, const FVector& AlongWall) -> bool
		{
			if (Out.Fixtures.Num() >= Rules.MaxWallLights)
			{
				return false;
			}
			const FIntVector Key(2 * C.X + FDX, 2 * C.Y + FDY, C.Z);
			if (UsedFaces.Contains(Key))
			{
				return true;
			}
			UsedFaces.Add(Key);
			FDungeonFixture F;
			F.Cell = C;
			F.FaceDX = FDX;
			F.FaceDY = FDY;
			F.Kind = EDungeonFixtureKind::WallLight;
			const FVector Base = Result.GridToWorld(C) + WorldOffset;
			const FVector OnFace = Base + FVector(HalfCS + FDX * (HalfCS - FaceInsetW), HalfCS + FDY * (HalfCS - FaceInsetW), MountZ);
			// +X off the wall into the cell.
			const FRotator Rot(0.0f, FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(-FDY), static_cast<float>(-FDX))), 0.0f);
			F.Anchor = FTransform(Rot, OnFace + AlongWall, FVector(ProfileScale));
			Out.Fixtures.Add(F);
			return true;
		};

		if (Rules.MaxWallLights > 0 && Rules.bLightDoorways)
		{
			for (const FDungeonOpening& O : Out.Openings)
			{
				if (O.Kind != EDungeonOpeningKind::Doorway)
				{
					continue;
				}
				// The two faces perpendicular to the opening; the light sits toward the room
				// (away from the frame) so an open leaf swung into the cell clears it.
				const FVector TowardRoom(-O.FaceDX * Rules.DoorwayLightOffsetFraction * CS, -O.FaceDY * Rules.DoorwayLightOffsetFraction * CS, 0.0f);
				const int32 Sides[2][2] = { { -O.FaceDY, O.FaceDX }, { O.FaceDY, -O.FaceDX } };
				for (const auto& Side : Sides)
				{
					if (RockBacked(O.Cell, Side[0], Side[1]) && !EmitFixture(O.Cell, Side[0], Side[1], TowardRoom))
					{
						break;
					}
				}
			}
		}

		if (Rules.MaxWallLights > 0 && Rules.RoomWallLightEvery > 0)
		{
			TMap<uint8, int32> FacesSeenPerRoom;
			for (int32 Z = 0; Z < GridSize.Z && Out.Fixtures.Num() < Rules.MaxWallLights; ++Z)
			for (int32 Y = 0; Y < GridSize.Y && Out.Fixtures.Num() < Rules.MaxWallLights; ++Y)
			for (int32 X = 0; X < GridSize.X && Out.Fixtures.Num() < Rules.MaxWallLights; ++X)
			{
				const FDungeonCell& Cell = Result.Grid.GetCell(X, Y, Z);
				if (Cell.CellType != EDungeonCellType::Room)
				{
					continue;
				}
				const FIntVector C(X, Y, Z);
				for (int32 Dir = 0; Dir < 4; ++Dir)
				{
					if (!RockBacked(C, DX[Dir], DY[Dir]))
					{
						continue;
					}
					int32& Seen = FacesSeenPerRoom.FindOrAdd(Cell.RoomIndex);
					++Seen;
					if (Seen % Rules.RoomWallLightEvery == 0)
					{
						EmitFixture(C, DX[Dir], DY[Dir], FVector::ZeroVector);
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
