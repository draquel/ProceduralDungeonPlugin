// DungeonBoundaryRules.cpp — Shared wall / floor / ceiling boundary predicates
#include "DungeonBoundaryRules.h"

bool FDungeonBoundaryRules::IsOpenCell(EDungeonCellType Type)
{
	return Type != EDungeonCellType::Empty && Type != EDungeonCellType::RoomWall;
}

bool FDungeonBoundaryRules::IsRoomFamily(EDungeonCellType Type)
{
	return Type == EDungeonCellType::Room
		|| Type == EDungeonCellType::Door
		|| Type == EDungeonCellType::Entrance;
}

bool FDungeonBoundaryRules::IsHallwayFamily(EDungeonCellType Type)
{
	return Type == EDungeonCellType::Hallway
		|| Type == EDungeonCellType::Staircase
		|| Type == EDungeonCellType::StaircaseHead;
}

bool FDungeonBoundaryRules::IsStairSideFace(const FDungeonCell& Cell, int32 DX, int32 DY)
{
	if (Cell.CellType != EDungeonCellType::Staircase && Cell.CellType != EDungeonCellType::StaircaseHead)
	{
		return false;
	}
	// Climb axis: directions 0/1 run along X, 2/3 along Y. A flank is the other axis.
	const bool bClimbAlongX = (Cell.StaircaseDirection <= 1);
	return bClimbAlongX ? (DY != 0) : (DX != 0);
}

namespace
{
	/**
	 * Rules 1 to 4, identical for horizontal and vertical faces. Returns true when a decision was
	 * reached (written to OutNeedsBoundary); false when the face-specific hallway-family rule
	 * must decide, in which case OutNeighbor is the in-bounds open neighbour.
	 */
	bool SharedRules(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ,
		bool& OutNeedsBoundary, const FDungeonCell*& OutNeighbor)
	{
		OutNeighbor = nullptr;

		// 1. Out of bounds or solid neighbour: boundary.
		if (!Grid.IsInBounds(NX, NY, NZ))
		{
			OutNeedsBoundary = true;
			return true;
		}
		const FDungeonCell& Neighbor = Grid.GetCell(NX, NY, NZ);
		OutNeighbor = &Neighbor;
		if (!FDungeonBoundaryRules::IsOpenCell(Neighbor.CellType))
		{
			OutNeedsBoundary = true;
			return true;
		}

		// 2. Door / Entrance neighbours place their own frames; never boundary them off.
		if (Neighbor.CellType == EDungeonCellType::Door || Neighbor.CellType == EDungeonCellType::Entrance)
		{
			OutNeedsBoundary = false;
			return true;
		}

		// 3. Door / Entrance opening toward the hallway family = the doorway itself. Without this
		//    every doorway got a wall tile across its own opening while the voxels stayed carved.
		if ((Current.CellType == EDungeonCellType::Door || Current.CellType == EDungeonCellType::Entrance)
			&& FDungeonBoundaryRules::IsHallwayFamily(Neighbor.CellType))
		{
			OutNeedsBoundary = false;
			return true;
		}

		// 4. Same room = open (also covers multi-floor room interiors vertically).
		if (FDungeonBoundaryRules::IsRoomFamily(Current.CellType)
			&& FDungeonBoundaryRules::IsRoomFamily(Neighbor.CellType)
			&& Current.RoomIndex == Neighbor.RoomIndex)
		{
			OutNeedsBoundary = false;
			return true;
		}

		return false;
	}
}

bool FDungeonBoundaryRules::NeedsWall(const FDungeonGrid& Grid, const FIntVector& CurrentCoord, int32 NX, int32 NY, int32 NZ)
{
	const FDungeonCell& Current = Grid.GetCell(CurrentCoord);

	bool bNeeds = true;
	const FDungeonCell* Neighbor = nullptr;
	if (SharedRules(Grid, Current, NX, NY, NZ, bNeeds, Neighbor))
	{
		return bNeeds;
	}
	check(Neighbor);

	// 5. Staircase flanks are walls from both sides of the face. The ramp and its shaft are
	//    entered along the climb axis only; a hallway running alongside, or a landing beside
	//    another staircase's shaft, must not open into it.
	const int32 DX = NX - CurrentCoord.X;
	const int32 DY = NY - CurrentCoord.Y;
	if (IsStairSideFace(Current, DX, DY) || IsStairSideFace(*Neighbor, -DX, -DY))
	{
		return true;
	}

	// 6. Hallway family on both sides = no wall (hallways merge naturally at intersections).
	//    Exception: a StaircaseHead only opens toward cells of the same staircase, which includes
	//    the plain Hallway it exits into, keyed by HallwayIndex. (Requiring BOTH sides to be
	//    staircase-family walled the top of every staircase off from its own exit corridor.)
	if (IsHallwayFamily(Current.CellType) && IsHallwayFamily(Neighbor->CellType))
	{
		const bool bEitherIsHead = (Current.CellType == EDungeonCellType::StaircaseHead
			|| Neighbor->CellType == EDungeonCellType::StaircaseHead);
		if (bEitherIsHead)
		{
			return Current.HallwayIndex != Neighbor->HallwayIndex;
		}
		return false;
	}

	// 7. Different spaces (room vs hallway, different rooms) = wall.
	return true;
}

bool FDungeonBoundaryRules::NeedsVerticalBoundary(const FDungeonGrid& Grid, const FIntVector& CurrentCoord, int32 NX, int32 NY, int32 NZ)
{
	const FDungeonCell& Current = Grid.GetCell(CurrentCoord);

	bool bNeeds = true;
	const FDungeonCell* Neighbor = nullptr;
	if (SharedRules(Grid, Current, NX, NY, NZ, bNeeds, Neighbor))
	{
		return bNeeds;
	}
	check(Neighbor);

	// 5. Same-hallway vertical opening applies ONLY to the staircase shaft (a Staircase or
	//    StaircaseHead on at least one side): the ramp climbs through those cells and must not be
	//    floored over. Two FLAT Hallway cells stacked at different Z are separate walkable levels.
	if (IsHallwayFamily(Current.CellType) && IsHallwayFamily(Neighbor->CellType)
		&& Current.HallwayIndex == Neighbor->HallwayIndex)
	{
		const bool bShaft =
			Current.CellType == EDungeonCellType::Staircase || Current.CellType == EDungeonCellType::StaircaseHead
			|| Neighbor->CellType == EDungeonCellType::Staircase || Neighbor->CellType == EDungeonCellType::StaircaseHead;
		if (bShaft)
		{
			return false;
		}
	}

	// 6. Different spaces, or two stacked flat hallways = boundary.
	return true;
}
