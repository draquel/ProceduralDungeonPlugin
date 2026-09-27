#include "RoomPlacement.h"
#include "DungeonTypes.h"
#include "DungeonSeed.h"
#include "DungeonConfig.h"
#include "RoomSemantics.h"

DEFINE_LOG_CATEGORY_STATIC(LogDungeonRooms, Log, All);

namespace
{
	/** True when every in-bounds cell of the inclusive box is Empty. */
	bool BoxIsEmpty(const FDungeonGrid& Grid, const FIntVector& Min, const FIntVector& Max)
	{
		for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					if (Grid.IsInBounds(X, Y, Z) && Grid.GetCell(X, Y, Z).CellType != EDungeonCellType::Empty)
					{
						return false;
					}
				}
			}
		}
		return true;
	}
}

// ---------------------------------------------------------------------------
// PlaceEntranceRoom
// ---------------------------------------------------------------------------

bool FRoomPlacement::PlaceEntranceRoom(
	FDungeonGrid& Grid,
	const UDungeonConfiguration& Config,
	FDungeonSeed& Seed,
	TArray<FDungeonRoom>& OutRooms,
	FDungeonEntranceApproachInfo& OutApproach)
{
	const FDungeonEntranceSpec& Spec = Config.Entrance;

	OutApproach = FDungeonEntranceApproachInfo();
	OutApproach.Approach = Spec.Approach;
	OutApproach.bSatisfied = false;

	if (Spec.Approach == EDungeonEntranceApproach::None)
	{
		return false;
	}
	if (OutRooms.Num() != 0)
	{
		UE_LOG(LogDungeonRooms, Warning, TEXT("PlaceEntranceRoom: rooms already placed; the entrance must be first"));
		return false;
	}

	// Own fork: the room fork (1) is untouched, so Approach None reproduces pre-spec layouts.
	FDungeonSeed EntranceSeed = Seed.Fork(5);

	const int32 Buffer = FMath::Max(Config.RoomBuffer, 0);
	const bool bVertical = Spec.Approach == EDungeonEntranceApproach::FromAbove
		|| Spec.Approach == EDungeonEntranceApproach::FromBelow;

	// Resolve the face once so every attempt targets the same side of the grid.
	EDungeonGridFace Face = Spec.Face;
	if (Spec.Approach == EDungeonEntranceApproach::FromSide && Face == EDungeonGridFace::Any)
	{
		Face = static_cast<EDungeonGridFace>(EntranceSeed.RandRange(
			static_cast<int32>(EDungeonGridFace::MinX), static_cast<int32>(EDungeonGridFace::MaxY)));
	}
	OutApproach.Face = (Spec.Approach == EDungeonEntranceApproach::FromSide) ? Face : EDungeonGridFace::Any;

	for (int32 Attempt = 0; Attempt < Config.MaxPlacementAttempts; ++Attempt)
	{
		const int32 SizeX = EntranceSeed.RandRange(Config.MinRoomSize.X, Config.MaxRoomSize.X);
		const int32 SizeY = EntranceSeed.RandRange(Config.MinRoomSize.Y, Config.MaxRoomSize.Y);
		// A single-floor entrance room makes the opening the room's own lid / floor.
		const int32 SizeZ = (bVertical && Spec.bSingleFloorEntranceRoom)
			? 1
			: EntranceSeed.RandRange(Config.MinRoomSize.Z, Config.MaxRoomSize.Z);

		const int32 MaxPosX = Config.GridSize.X - SizeX - Buffer;
		const int32 MaxPosY = Config.GridSize.Y - SizeY - Buffer;
		const int32 MaxPosZ = Config.GridSize.Z - SizeZ;
		if (MaxPosX < Buffer || MaxPosY < Buffer || MaxPosZ < 0)
		{
			continue; // Room too large to fit
		}

		int32 PosZ = 0;
		switch (Spec.Floor)
		{
		case EDungeonEntranceFloor::Top:    PosZ = MaxPosZ; break;
		case EDungeonEntranceFloor::Bottom: PosZ = 0; break;
		case EDungeonEntranceFloor::Explicit:
			if (Spec.ExplicitFloor < 0 || Spec.ExplicitFloor > MaxPosZ)
			{
				// Not a retry case: no room of any allowed height can sit on that floor.
				UE_LOG(LogDungeonRooms, Warning,
					TEXT("PlaceEntranceRoom: ExplicitFloor %d is outside [0, %d] for a %d-floor room in a %d-floor grid"),
					Spec.ExplicitFloor, MaxPosZ, SizeZ, Config.GridSize.Z);
				return false;
			}
			PosZ = Spec.ExplicitFloor;
			break;
		default: PosZ = EntranceSeed.RandRange(0, MaxPosZ); break;
		}

		int32 PosX = EntranceSeed.RandRange(Buffer, MaxPosX);
		int32 PosY = EntranceSeed.RandRange(Buffer, MaxPosY);
		if (Spec.Approach == EDungeonEntranceApproach::FromSide)
		{
			// On the buffer line of the chosen face: the corridor is exactly the buffer cells
			// between the room face and the grid edge.
			switch (Face)
			{
			case EDungeonGridFace::MinX: PosX = Buffer; break;
			case EDungeonGridFace::MaxX: PosX = MaxPosX; break;
			case EDungeonGridFace::MinY: PosY = Buffer; break;
			case EDungeonGridFace::MaxY: PosY = MaxPosY; break;
			default: break;
			}
		}

		FDungeonRoom Room;
		Room.RoomIndex = 1;
		Room.RoomType = EDungeonRoomType::Entrance;
		Room.Position = FIntVector(PosX, PosY, PosZ);
		Room.Size = FIntVector(SizeX, SizeY, SizeZ);
		Room.Center = Room.Position + FIntVector(SizeX / 2, SizeY / 2, SizeZ / 2);
		Room.FloorLevel = PosZ;

		// Same formula as the generator's EntranceCell (ground-floor centre).
		const FIntVector EntranceCell = Room.Position + FIntVector(SizeX / 2, SizeY / 2, 0);
		FDungeonEntranceApproachInfo Info = FRoomSemantics::ComputeEntranceApproach(
			Room, EntranceCell, Spec, Face, Config.GridSize);

		if (DoesRoomOverlap(Room.Position, Room.Size, OutRooms, Buffer)
			|| OverlapsReserved(Grid, Room.Position, Room.Size)
			|| (Info.HasKeepOut() && !BoxIsEmpty(Grid, Info.KeepOutMin, Info.KeepOutMax)))
		{
			continue;
		}

		StampRoomToGrid(Grid, Room);
		if (Info.HasKeepOut())
		{
			FillEmptyBox(Grid, Info.KeepOutMin, Info.KeepOutMax, EDungeonCellType::Reserved);
		}
		OutRooms.Add(Room);

		Info.bSatisfied = true;
		OutApproach = Info;

		UE_LOG(LogDungeonRooms, Log,
			TEXT("PlaceEntranceRoom: approach %d at (%d,%d,%d) size (%d,%d,%d), opening (%d,%d,%d), keep-out (%d,%d,%d)-(%d,%d,%d)%s"),
			static_cast<int32>(Spec.Approach), PosX, PosY, PosZ, SizeX, SizeY, SizeZ,
			Info.OpeningCell.X, Info.OpeningCell.Y, Info.OpeningCell.Z,
			Info.KeepOutMin.X, Info.KeepOutMin.Y, Info.KeepOutMin.Z,
			Info.KeepOutMax.X, Info.KeepOutMax.Y, Info.KeepOutMax.Z,
			Info.HasKeepOut() ? TEXT("") : TEXT(" (nothing to reserve)"));
		return true;
	}

	UE_LOG(LogDungeonRooms, Warning,
		TEXT("PlaceEntranceRoom: approach %d could not be satisfied after %d attempts"),
		static_cast<int32>(Spec.Approach), Config.MaxPlacementAttempts);
	return false;
}

// ---------------------------------------------------------------------------
// PlaceRooms
// ---------------------------------------------------------------------------

bool FRoomPlacement::PlaceRooms(
	FDungeonGrid& Grid,
	const UDungeonConfiguration& Config,
	FDungeonSeed& Seed,
	TArray<FDungeonRoom>& OutRooms)
{
	FDungeonSeed RoomSeed = Seed.Fork(1);

	for (int32 i = OutRooms.Num(); i < Config.RoomCount; ++i)
	{
		bool bPlaced = false;

		for (int32 Attempt = 0; Attempt < Config.MaxPlacementAttempts; ++Attempt)
		{
			// Random size within configured bounds
			const int32 SizeX = RoomSeed.RandRange(Config.MinRoomSize.X, Config.MaxRoomSize.X);
			const int32 SizeY = RoomSeed.RandRange(Config.MinRoomSize.Y, Config.MaxRoomSize.Y);
			const int32 SizeZ = RoomSeed.RandRange(Config.MinRoomSize.Z, Config.MaxRoomSize.Z);

			// Valid position range (buffer from grid edges on XY, no buffer on Z)
			const int32 MinPos = Config.RoomBuffer;
			const int32 MaxPosX = Config.GridSize.X - SizeX - Config.RoomBuffer;
			const int32 MaxPosY = Config.GridSize.Y - SizeY - Config.RoomBuffer;
			const int32 MaxPosZ = Config.GridSize.Z - SizeZ;

			if (MaxPosX < MinPos || MaxPosY < MinPos || MaxPosZ < 0)
			{
				continue; // Room too large to fit
			}

			const int32 PosX = RoomSeed.RandRange(MinPos, MaxPosX);
			const int32 PosY = RoomSeed.RandRange(MinPos, MaxPosY);
			const int32 PosZ = RoomSeed.RandRange(0, MaxPosZ);

			const FIntVector Position(PosX, PosY, PosZ);
			const FIntVector Size(SizeX, SizeY, SizeZ);

			// Reserved cells are the entrance approach volume; a room in it would sit under
			// the shaft (or across the tunnel). Never true on the legacy path (nothing reserved).
			if (!DoesRoomOverlap(Position, Size, OutRooms, Config.RoomBuffer)
				&& !OverlapsReserved(Grid, Position, Size))
			{
				FDungeonRoom Room;
				Room.RoomIndex = static_cast<uint8>(OutRooms.Num() + 1);
				Room.RoomType = EDungeonRoomType::Generic;
				Room.Position = Position;
				Room.Size = Size;
				Room.Center = Position + FIntVector(SizeX / 2, SizeY / 2, SizeZ / 2);
				Room.FloorLevel = PosZ;

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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool FRoomPlacement::OverlapsReserved(const FDungeonGrid& Grid, const FIntVector& Position, const FIntVector& Size)
{
	for (int32 Z = Position.Z; Z < Position.Z + Size.Z; ++Z)
	{
		for (int32 Y = Position.Y; Y < Position.Y + Size.Y; ++Y)
		{
			for (int32 X = Position.X; X < Position.X + Size.X; ++X)
			{
				if (Grid.IsInBounds(X, Y, Z) && Grid.GetCell(X, Y, Z).CellType == EDungeonCellType::Reserved)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void FRoomPlacement::FillEmptyBox(FDungeonGrid& Grid, const FIntVector& Min, const FIntVector& Max, EDungeonCellType Type)
{
	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				if (Grid.IsInBounds(X, Y, Z))
				{
					FDungeonCell& Cell = Grid.GetCell(X, Y, Z);
					if (Cell.CellType == EDungeonCellType::Empty)
					{
						Cell.CellType = Type;
					}
				}
			}
		}
	}
}

bool FRoomPlacement::DoesRoomOverlap(
	const FIntVector& Position,
	const FIntVector& Size,
	const TArray<FDungeonRoom>& ExistingRooms,
	int32 Buffer)
{
	for (const FDungeonRoom& Other : ExistingRooms)
	{
		// AABB overlap test: buffer on XY axes (hallway space), no buffer on Z (floors)
		const bool bOverlapX =
			Position.X < (Other.Position.X + Other.Size.X + Buffer) &&
			(Position.X + Size.X + Buffer) > Other.Position.X;

		const bool bOverlapY =
			Position.Y < (Other.Position.Y + Other.Size.Y + Buffer) &&
			(Position.Y + Size.Y + Buffer) > Other.Position.Y;

		const bool bOverlapZ =
			Position.Z < (Other.Position.Z + Other.Size.Z) &&
			(Position.Z + Size.Z) > Other.Position.Z;

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
					Cell.FloorIndex = static_cast<uint8>(Z);
				}
			}
		}
	}
}
