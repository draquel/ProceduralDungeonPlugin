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
	HallwayFloorTJunctionRotationOffset = FRotator::ZeroRotator;
	// Hallway floor variants default to null (fall back to HallwayFloor)
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

#undef ADD_SLOT
}
