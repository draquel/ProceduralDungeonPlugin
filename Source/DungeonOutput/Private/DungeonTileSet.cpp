#include "DungeonTileSet.h"

UDungeonTileSet::UDungeonTileSet()
{
	// A fresh tileset gets the base tile types pointing at the engine cube, so it renders something
	// immediately. Legacy assets overwrite this in PostLoad via migration; assets saved with the
	// Slots layout overwrite it on Serialize.
	PopulateDefaultSlots();
}

void UDungeonTileSet::PopulateDefaultSlots()
{
	const FSoftObjectPath DefaultCube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static const EDungeonTileType BaseTypes[] = {
		EDungeonTileType::RoomFloor, EDungeonTileType::HallwayFloor,
		EDungeonTileType::RoomCeiling, EDungeonTileType::HallwayCeiling,
		EDungeonTileType::WallSegment, EDungeonTileType::DoorFrame,
		EDungeonTileType::EntranceFrame, EDungeonTileType::StaircaseMesh,
	};
	for (EDungeonTileType Type : BaseTypes)
	{
		FDungeonTileSlot Slot;
		Slot.Mesh = TSoftObjectPtr<UStaticMesh>(DefaultCube);
		Slots.Add(Type, Slot);
	}
}

void UDungeonTileSet::PostInitProperties()
{
	Super::PostInitProperties();
	// (Default slots come from the constructor. Nothing extra here — kept as an override point.)
}

void UDungeonTileSet::PostLoad()
{
	Super::PostLoad();

	// A legacy tileset (authored before Slots existed) carries its data in the deprecated parallel
	// fields. The current constructor never populates those, so any non-null legacy mesh / module
	// means "this asset predates Slots" — migrate once. New assets have null legacy fields and are
	// left alone.
	const bool bLegacyHasData =
		!RoomFloor.IsNull() || !HallwayFloor.IsNull() || !RoomCeiling.IsNull() || !HallwayCeiling.IsNull()
		|| !WallSegment.IsNull() || !DoorFrame.IsNull() || !EntranceFrame.IsNull() || !StaircaseMesh.IsNull()
		|| TileModules.Num() > 0;

	if (bLegacyHasData)
	{
		MigrateLegacyFieldsToSlots();
	}
}

void UDungeonTileSet::MigrateLegacyFieldsToSlots()
{
	Slots.Empty();

	auto Set = [this](EDungeonTileType Type, const TSoftObjectPtr<UStaticMesh>& Mesh,
		const FRotator& Rotation, const FVector& Scale)
	{
		FDungeonTileSlot Slot;
		Slot.Mesh = Mesh;
		Slot.RotationOffset = Rotation;
		// Old assets saved before the scale-multiplier feature load Scale as (0,0,0); treat that as 1.
		Slot.ScaleMultiplier = Scale.IsNearlyZero() ? FVector::OneVector : Scale;
		Slots.Add(Type, Slot);
	};
	auto SetVariant = [&Set](EDungeonTileType Type, const TSoftObjectPtr<UStaticMesh>& Mesh,
		const FRotator& Rotation, const FVector& Scale)
	{
		if (!Mesh.IsNull())
		{
			Set(Type, Mesh, Rotation, Scale);
		}
	};

	// Base types (always present).
	Set(EDungeonTileType::RoomFloor,      RoomFloor,      RoomFloorRotationOffset,      RoomFloorScaleMultiplier);
	Set(EDungeonTileType::HallwayFloor,   HallwayFloor,   HallwayFloorRotationOffset,   HallwayFloorScaleMultiplier);
	Set(EDungeonTileType::RoomCeiling,    RoomCeiling,    RoomCeilingRotationOffset,    RoomCeilingScaleMultiplier);
	Set(EDungeonTileType::HallwayCeiling, HallwayCeiling, HallwayCeilingRotationOffset, HallwayCeilingScaleMultiplier);
	Set(EDungeonTileType::WallSegment,    WallSegment,    WallSegmentRotationOffset,    WallSegmentScaleMultiplier);
	Set(EDungeonTileType::DoorFrame,      DoorFrame,      DoorFrameRotationOffset,      DoorFrameScaleMultiplier);
	Set(EDungeonTileType::EntranceFrame,  EntranceFrame,  EntranceFrameRotationOffset,  EntranceFrameScaleMultiplier);
	Set(EDungeonTileType::StaircaseMesh,  StaircaseMesh,  StaircaseMeshRotationOffset,  FVector::OneVector);

	// Hallway variants (only migrate the ones that had a mesh).
	SetVariant(EDungeonTileType::HallwayFloorStraight,  HallwayFloorStraight,  HallwayFloorStraightRotationOffset,  HallwayFloorStraightScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayFloorCorner,    HallwayFloorCorner,    HallwayFloorCornerRotationOffset,    HallwayFloorCornerScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayFloorTJunction, HallwayFloorTJunction, HallwayFloorTJunctionRotationOffset, HallwayFloorTJunctionScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayFloorCrossroad, HallwayFloorCrossroad, HallwayFloorCrossroadRotationOffset, HallwayFloorCrossroadScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayFloorEndCap,    HallwayFloorEndCap,    HallwayFloorEndCapRotationOffset,    HallwayFloorEndCapScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayCeilingStraight,  HallwayCeilingStraight,  HallwayCeilingStraightRotationOffset,  HallwayCeilingStraightScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayCeilingCorner,    HallwayCeilingCorner,    HallwayCeilingCornerRotationOffset,    HallwayCeilingCornerScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayCeilingTJunction, HallwayCeilingTJunction, HallwayCeilingTJunctionRotationOffset, HallwayCeilingTJunctionScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayCeilingCrossroad, HallwayCeilingCrossroad, HallwayCeilingCrossroadRotationOffset, HallwayCeilingCrossroadScaleMultiplier);
	SetVariant(EDungeonTileType::HallwayCeilingEndCap,    HallwayCeilingEndCap,    HallwayCeilingEndCapRotationOffset,    HallwayCeilingEndCapScaleMultiplier);

	// Modules override their type's slot geometry.
	for (const TPair<EDungeonTileType, TSoftObjectPtr<UDungeonTileModule>>& MPair : TileModules)
	{
		Slots.FindOrAdd(MPair.Key).Module = MPair.Value;
	}
}

const FDungeonTileSlot& UDungeonTileSet::GetSlot(EDungeonTileType Type) const
{
	static const FDungeonTileSlot Empty;
	const FDungeonTileSlot* Found = Slots.Find(Type);
	return Found ? *Found : Empty;
}

bool UDungeonTileSet::IsValid() const
{
	for (const TPair<EDungeonTileType, FDungeonTileSlot>& Pair : Slots)
	{
		if (Pair.Value.IsActive())
		{
			return true;
		}
	}
	return false;
}

void UDungeonTileSet::GetAllUniqueMeshes(TArray<TPair<FName, TSoftObjectPtr<UStaticMesh>>>& OutMeshes) const
{
	OutMeshes.Reset();
	const UEnum* TypeEnum = StaticEnum<EDungeonTileType>();
	for (const TPair<EDungeonTileType, FDungeonTileSlot>& Pair : Slots)
	{
		if (!Pair.Value.Mesh.IsNull())
		{
			const FName Name = TypeEnum
				? FName(*TypeEnum->GetNameStringByValue(static_cast<int64>(Pair.Key)))
				: NAME_None;
			OutMeshes.Emplace(Name, Pair.Value.Mesh);
		}
	}
}
