#include "DungeonTileSet.h"
#include "DungeonWallProfileConformance.h"

UDungeonTileSet::UDungeonTileSet()
{
	// A fresh tileset gets the base tile types pointing at the engine cube, so it renders something
	// immediately. Assets saved with the Slots layout overwrite it on Serialize.
	PopulateDefaultSlots();
}

void UDungeonTileSet::PopulateDefaultSlots()
{
	const FSoftObjectPath DefaultCube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	// Do NOT add new tile types here once tilesets exist: a TMap property serializes as a delta
	// against the class default, so a key added to the default appears in every already-saved
	// tileset on load (as the engine cube) until that asset is re-saved. WallPartition is left
	// out for that reason — an unset partition slot falls back to WallSegment in the mapper.
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

TArray<FString> UDungeonTileSet::CheckWallProfile() const
{
	TArray<FString> Issues;
	FDungeonWallProfileConformance::Check(*this, Issues);
	FDungeonWallProfileConformance::CheckCollision(*this, Issues);
	return Issues;
}
