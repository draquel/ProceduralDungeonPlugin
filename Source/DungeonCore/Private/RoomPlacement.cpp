#include "RoomPlacement.h"
#include "DungeonTypes.h"
#include "DungeonSeed.h"
#include "DungeonConfig.h"

DEFINE_LOG_CATEGORY_STATIC(LogDungeonRooms, Log, All);

bool FRoomPlacement::PlaceRooms(
	FDungeonGrid& Grid,
	const UDungeonConfiguration& Config,
	FDungeonSeed& Seed,
	TArray<FDungeonRoom>& OutRooms)
{
	FDungeonSeed RoomSeed = Seed.Fork(1);

	for (int32 i = 0; i < Config.RoomCount; ++i)
	{
		bool bPlaced = false;

		for (int32 Attempt = 0; Attempt < Config.MaxPlacementAttempts; ++Attempt)
		{
			// Random size within configured bounds
			const int32 SizeX = RoomSeed.RandRange(Config.MinRoomSize.X, Config.MaxRoomSize.X);
			const int32 SizeZ = RoomSeed.RandRange(Config.MinRoomSize.Z, Config.MaxRoomSize.Z);
			const int32 SizeY = 1; // Phase 1: single floor only

			// Valid position range (buffer from grid edges)
			const int32 MinPos = Config.RoomBuffer;
			const int32 MaxPosX = Config.GridSize.X - SizeX - Config.RoomBuffer;
			const int32 MaxPosZ = Config.GridSize.Z - SizeZ - Config.RoomBuffer;

			if (MaxPosX < MinPos || MaxPosZ < MinPos)
			{
				continue; // Room too large to fit with buffer
			}

			const int32 PosX = RoomSeed.RandRange(MinPos, MaxPosX);
			const int32 PosZ = RoomSeed.RandRange(MinPos, MaxPosZ);
			const int32 PosY = 0; // Phase 1: floor 0

			const FIntVector Position(PosX, PosY, PosZ);
			const FIntVector Size(SizeX, SizeY, SizeZ);

			if (!DoesRoomOverlap(Position, Size, OutRooms, Config.RoomBuffer))
			{
				FDungeonRoom Room;
				Room.RoomIndex = static_cast<uint8>(OutRooms.Num() + 1);
				Room.RoomType = EDungeonRoomType::Generic;
				Room.Position = Position;
				Room.Size = Size;
				Room.Center = Position + FIntVector(SizeX / 2, SizeY / 2, SizeZ / 2);
				Room.FloorLevel = PosY;

				StampRoomToGrid(Grid, Room);
				OutRooms.Add(Room);
				bPlaced = true;
				break;
			}
		}

		if (!bPlaced)
		{
			UE_LOG(LogDungeonRooms, Warning,
				TEXT("Failed to place room %d/%d after %d attempts"),
				i + 1, Config.RoomCount, Config.MaxPlacementAttempts);
		}
	}

	UE_LOG(LogDungeonRooms, Log, TEXT("Placed %d/%d rooms"), OutRooms.Num(), Config.RoomCount);
	return OutRooms.Num() >= 2;
}

bool FRoomPlacement::DoesRoomOverlap(
	const FIntVector& Position,
	const FIntVector& Size,
	const TArray<FDungeonRoom>& ExistingRooms,
	int32 Buffer)
{
	for (const FDungeonRoom& Other : ExistingRooms)
	{
		// AABB overlap test including buffer zone
		const bool bOverlapX =
			Position.X < (Other.Position.X + Other.Size.X + Buffer) &&
			(Position.X + Size.X + Buffer) > Other.Position.X;

		const bool bOverlapY =
			Position.Y < (Other.Position.Y + Other.Size.Y + Buffer) &&
			(Position.Y + Size.Y + Buffer) > Other.Position.Y;

		const bool bOverlapZ =
			Position.Z < (Other.Position.Z + Other.Size.Z + Buffer) &&
			(Position.Z + Size.Z + Buffer) > Other.Position.Z;

		if (bOverlapX && bOverlapY && bOverlapZ)
		{
			return true;
		}
	}
	return false;
}

void FRoomPlacement::StampRoomToGrid(FDungeonGrid& Grid, const FDungeonRoom& Room)
{
	for (int32 X = Room.Position.X; X < Room.Position.X + Room.Size.X; ++X)
	{
		for (int32 Y = Room.Position.Y; Y < Room.Position.Y + Room.Size.Y; ++Y)
		{
			for (int32 Z = Room.Position.Z; Z < Room.Position.Z + Room.Size.Z; ++Z)
			{
				if (Grid.IsInBounds(X, Y, Z))
				{
					FDungeonCell& Cell = Grid.GetCell(X, Y, Z);
					Cell.CellType = EDungeonCellType::Room;
					Cell.RoomIndex = Room.RoomIndex;
					Cell.FloorIndex = static_cast<uint8>(Y);
				}
			}
		}
	}
}
