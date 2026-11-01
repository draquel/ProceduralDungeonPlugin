#pragma once

#include "CoreMinimal.h"

struct FDungeonGrid;
struct FDungeonRoom;
struct FDungeonSeed;
struct FDungeonEntranceApproachInfo;
struct FDungeonEntranceSpec;
enum class EDungeonCellType : uint8;
class UDungeonConfiguration;

/**
 * FDungeonRoomPlacement
 * Places rooms randomly on the grid with non-overlap and buffer constraints. With an entrance
 * approach configured (UDungeonConfiguration::Entrance), the entrance room is placed FIRST under
 * the approach constraints and its approach volume is reserved (EDungeonCellType::Reserved) so
 * the remaining rooms — and later the hallway pathfinder — keep out of it.
 */
struct DUNGEONCORE_API FDungeonRoomPlacement
{
	/**
	 * Place the entrance room first under Spec and reserve its approach volume.
	 * OutRooms must be empty. On success the room is OutRooms[0] (RoomIndex 1), the keep-out box
	 * is stamped Reserved, and OutApproach.bSatisfied is true. On failure nothing is placed,
	 * OutApproach records the request with bSatisfied false, and the caller falls back to
	 * unconstrained placement + post-hoc selection.
	 * Draws from Seed.Fork(5) only, so configs with Approach None keep their layouts.
	 * @param Spec  The entrance spec to honour (Config.Entrance, or a caller override).
	 * @return true when the entrance room was placed under the constraints.
	 */
	static bool PlaceEntranceRoom(
		FDungeonGrid& Grid,
		const UDungeonConfiguration& Config,
		const FDungeonEntranceSpec& Spec,
		FDungeonSeed& Seed,
		TArray<FDungeonRoom>& OutRooms,
		FDungeonEntranceApproachInfo& OutApproach);

	/**
	 * Place rooms into the grid until Config.RoomCount rooms exist, counting rooms already in
	 * OutRooms (a pre-placed entrance). Candidates avoid existing rooms (RoomBuffer on XY) and
	 * any Reserved cell.
	 * @return true if at least 2 rooms exist afterwards (minimum for a dungeon).
	 */
	static bool PlaceRooms(
		FDungeonGrid& Grid,
		const UDungeonConfiguration& Config,
		FDungeonSeed& Seed,
		TArray<FDungeonRoom>& OutRooms);

	/** True when any in-bounds cell of the AABB is Reserved. */
	static bool OverlapsReserved(
		const FDungeonGrid& Grid,
		const FIntVector& Position,
		const FIntVector& Size);

	/**
	 * Whether two room boxes overlap with a buffer of cells between them on X and Y (hallway space) and none on Z (floors):
	 * the one predicate of room placement and the validator's overlap and buffer checks (code review CR2, DNG-13).
	 * @param PosA, SizeA The first box (min cell, cells)
	 * @param PosB, SizeB The second box
	 * @param Buffer Cells kept clear between them on X and Y (0: the boxes may touch)
	 * @return True when they overlap, the buffer included
	 */
	static bool RoomsOverlap(const FIntVector& PosA, const FIntVector& SizeA, const FIntVector& PosB, const FIntVector& SizeB, int32 Buffer);

	/** Set Type on every in-bounds EMPTY cell of the inclusive box (never overwrites placed content). */
	static void FillEmptyBox(
		FDungeonGrid& Grid,
		const FIntVector& Min,
		const FIntVector& Max,
		EDungeonCellType Type);

private:
	static bool DoesRoomOverlap(
		const FIntVector& Position,
		const FIntVector& Size,
		const TArray<FDungeonRoom>& ExistingRooms,
		int32 Buffer);

	static void StampRoomToGrid(
		FDungeonGrid& Grid,
		const FDungeonRoom& Room);
};
