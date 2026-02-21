#include "DungeonTileSet.h"

UDungeonTileSet::UDungeonTileSet()
{
	const FSoftObjectPath DefaultCube(TEXT("/Engine/BasicShapes/Cube.Cube"));

	RoomFloor = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	HallwayFloor = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	RoomCeiling = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	HallwayCeiling = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	WallSegment = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	DoorFrame = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	EntranceFrame = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	StaircaseMesh = TSoftObjectPtr<UStaticMesh>(DefaultCube);
	StaircaseMeshRotationOffset = FRotator::ZeroRotator;
	// Floor variant rotation offsets + scale multipliers
	HallwayFloorStraightRotationOffset = FRotator::ZeroRotator;
	HallwayFloorStraightScaleMultiplier = FVector::OneVector;
	HallwayFloorCornerRotationOffset = FRotator::ZeroRotator;
	HallwayFloorCornerScaleMultiplier = FVector::OneVector;
	HallwayFloorTJunctionRotationOffset = FRotator::ZeroRotator;
	HallwayFloorTJunctionScaleMultiplier = FVector::OneVector;
	HallwayFloorCrossroadRotationOffset = FRotator::ZeroRotator;
	HallwayFloorCrossroadScaleMultiplier = FVector::OneVector;
	HallwayFloorEndCapRotationOffset = FRotator::ZeroRotator;
	HallwayFloorEndCapScaleMultiplier = FVector::OneVector;
	// Ceiling variant rotation offsets + scale multipliers
	HallwayCeilingStraightRotationOffset = FRotator::ZeroRotator;
	HallwayCeilingStraightScaleMultiplier = FVector::OneVector;
	HallwayCeilingCornerRotationOffset = FRotator::ZeroRotator;
	HallwayCeilingCornerScaleMultiplier = FVector::OneVector;
	HallwayCeilingTJunctionRotationOffset = FRotator::ZeroRotator;
	HallwayCeilingTJunctionScaleMultiplier = FVector::OneVector;
	HallwayCeilingCrossroadRotationOffset = FRotator::ZeroRotator;
	HallwayCeilingCrossroadScaleMultiplier = FVector::OneVector;
	HallwayCeilingEndCapRotationOffset = FRotator::ZeroRotator;
	HallwayCeilingEndCapScaleMultiplier = FVector::OneVector;
	// Hallway floor/ceiling variants default to null (fall back to base mesh)
}

bool UDungeonTileSet::IsValid() const
{
	return !RoomFloor.IsNull()
		|| !HallwayFloor.IsNull()
		|| !RoomCeiling.IsNull()
		|| !HallwayCeiling.IsNull()
		|| !WallSegment.IsNull()
		|| !DoorFrame.IsNull()
		|| !EntranceFrame.IsNull()
		|| !StaircaseMesh.IsNull();
}

void UDungeonTileSet::GetAllUniqueMeshes(TArray<TPair<FName, TSoftObjectPtr<UStaticMesh>>>& OutMeshes) const
{
	OutMeshes.Reset();

#define ADD_SLOT(SlotName) \
	if (!SlotName.IsNull()) { OutMeshes.Emplace(FName(TEXT(#SlotName)), SlotName); }

	ADD_SLOT(RoomFloor);
	ADD_SLOT(HallwayFloor);
	ADD_SLOT(RoomCeiling);
	ADD_SLOT(HallwayCeiling);
	ADD_SLOT(WallSegment);
	ADD_SLOT(DoorFrame);
	ADD_SLOT(EntranceFrame);
	ADD_SLOT(StaircaseMesh);
	ADD_SLOT(HallwayFloorStraight);
	ADD_SLOT(HallwayFloorCorner);
	ADD_SLOT(HallwayFloorTJunction);
	ADD_SLOT(HallwayFloorCrossroad);
	ADD_SLOT(HallwayFloorEndCap);
	ADD_SLOT(HallwayCeilingStraight);
	ADD_SLOT(HallwayCeilingCorner);
	ADD_SLOT(HallwayCeilingTJunction);
	ADD_SLOT(HallwayCeilingCrossroad);
	ADD_SLOT(HallwayCeilingEndCap);

#undef ADD_SLOT
}
