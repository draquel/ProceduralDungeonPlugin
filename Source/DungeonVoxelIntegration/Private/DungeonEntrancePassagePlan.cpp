// DungeonEntrancePassagePlan.cpp — Pure passage geometry for the entrance stitcher.
#include "DungeonEntrancePassagePlan.h"

EDungeonEntranceApproach FDungeonEntrancePassagePlan::RequiredApproach(EDungeonEntranceStyle Style)
{
	switch (Style)
	{
	case EDungeonEntranceStyle::VerticalShaft:
	case EDungeonEntranceStyle::CaveOpening:
	case EDungeonEntranceStyle::Trapdoor:
		return EDungeonEntranceApproach::FromAbove;
	case EDungeonEntranceStyle::SlopedTunnel:
		return EDungeonEntranceApproach::FromSide;
	default:
		return EDungeonEntranceApproach::None;
	}
}

bool FDungeonEntrancePassagePlan::IsStyleCompatible(EDungeonEntranceStyle Style, EDungeonEntranceApproach Approach, FString* OutWhy)
{
	if (Approach == EDungeonEntranceApproach::None)
	{
		return true; // legacy: every style, best effort
	}
	if (Approach == EDungeonEntranceApproach::FromBelow)
	{
		if (OutWhy) { *OutWhy = TEXT("no entrance style enters from below"); }
		return false;
	}
	const EDungeonEntranceApproach Required = RequiredApproach(Style);
	if (Required != Approach)
	{
		if (OutWhy)
		{
			*OutWhy = FString::Printf(TEXT("style %d needs approach %d but the dungeon was generated for approach %d"),
				static_cast<int32>(Style), static_cast<int32>(Required), static_cast<int32>(Approach));
		}
		return false;
	}
	return true;
}

namespace
{
	void FaceDirection(EDungeonGridFace Face, int32& DX, int32& DY)
	{
		DX = 0; DY = 0;
		switch (Face)
		{
		case EDungeonGridFace::MinX: DX = -1; break;
		case EDungeonGridFace::MaxX: DX = +1; break;
		case EDungeonGridFace::MinY: DY = -1; break;
		case EDungeonGridFace::MaxY: DY = +1; break;
		default: break;
		}
	}

	bool InGridXY(const FIntVector& GS, const FIntVector& C)
	{
		return C.X >= 0 && C.X < GS.X && C.Y >= 0 && C.Y < GS.Y;
	}
}

FDungeonEntrancePassagePlan FDungeonEntrancePassagePlan::Build(
	const FDungeonResult& Result,
	const FVector& WorldOffset,
	EDungeonEntranceStyle Style,
	float VoxelSize,
	bool bStopAtEntranceCellTop,
	float SideTunnelFloorLift,
	TFunctionRef<float(float, float)> SampleSurfaceZ)
{
	FDungeonEntrancePassagePlan Plan;
	Plan.Style = Style;

	if (Result.EntranceRoomIndex < 0 || !Result.Rooms.IsValidIndex(Result.EntranceRoomIndex))
	{
		Plan.Error = TEXT("no entrance defined in the dungeon result");
		return Plan;
	}

	const FDungeonEntranceApproachInfo& A = Result.EntranceApproach;
	if (A.Approach != EDungeonEntranceApproach::None && !A.bSatisfied)
	{
		Plan.Note = FString::Printf(TEXT("approach %d was requested but the generator could not satisfy it; carving the legacy shape best-effort (the layout may block it)"),
			static_cast<int32>(A.Approach));
	}
	Plan.Approach = (A.Approach != EDungeonEntranceApproach::None && A.bSatisfied) ? A.Approach : EDungeonEntranceApproach::None;

	FString Why;
	if (!IsStyleCompatible(Style, Plan.Approach, &Why))
	{
		Plan.Error = Why;
		return Plan;
	}

	const float CS = Result.CellWorldSize;
	const float HalfCS = CS * 0.5f;
	const float Overshoot = SurfaceOvershootVoxels * VoxelSize;
	const FIntVector& GS = Result.Grid.GridSize;

	auto CellCenterXY = [&](const FIntVector& C)
	{
		return FVector(WorldOffset.X + (C.X + 0.5f) * CS, WorldOffset.Y + (C.Y + 0.5f) * CS, 0.0f);
	};

	// ------------------------------------------------------------------
	// FromSide: corridor through the reserved cells, then a ramp outside the grid.
	// ------------------------------------------------------------------
	if (Plan.Approach == EDungeonEntranceApproach::FromSide)
	{
		int32 DX, DY;
		FaceDirection(A.Face, DX, DY);
		if (DX == 0 && DY == 0)
		{
			Plan.Error = TEXT("FromSide approach has no resolved face");
			return Plan;
		}

		// The tunnel floor meets the room's walkable floor (cell bottom + lining); its ceiling is
		// the cell top, so the passage is cell height minus the lining.
		const float CellBottomZ = WorldOffset.Z + A.OpeningCell.Z * CS;
		const float FloorZ = CellBottomZ + FMath::Max(SideTunnelFloorLift, 0.0f);
		const float CeilingZ = CellBottomZ + CS;
		Plan.EntranceZ = FloorZ;

		// Voxel-lined dungeons: carve the opening cell itself so the room's wall lining opens
		// (no shell: it would ring the inside of the room). Tile-dressed: the mapper opens the
		// wall tile; start outside the room.
		FIntVector Cell = A.OpeningCell;
		if (bStopAtEntranceCellTop)
		{
			Cell += FIntVector(DX, DY, 0);
		}
		while (InGridXY(GS, Cell))
		{
			FDungeonPassageSegment& Seg = Plan.Segments.AddDefaulted_GetRef();
			Seg.Center = CellCenterXY(Cell);
			Seg.HalfExtentXY = HalfCS;
			Seg.BottomZ = FloorZ;
			Seg.TopZ = CeilingZ;
			Seg.bWalls = (Cell != A.OpeningCell);
			Seg.bFloorCeilingShell = Seg.bWalls;
			Seg.Cell = Cell;
			Seg.bInsideGrid = true;
			Cell += FIntVector(DX, DY, 0);
		}

		// The ramp climbs from the first cell outside the grid. Sample the surface at the exit,
		// estimate where the ramp breaks out, and resample there (terrain is not flat).
		const FVector Exit = CellCenterXY(Cell);
		const float Rise = VoxelSize * TunnelRiseOverRun;
		float Surface = SampleSurfaceZ(Exit.X, Exit.Y);
		{
			const float Run = FMath::Max(Surface + Overshoot - FloorZ, 0.0f) / TunnelRiseOverRun;
			const FVector Mouth(Exit.X + DX * Run, Exit.Y + DY * Run, 0.0f);
			Surface = SampleSurfaceZ(Mouth.X, Mouth.Y);
		}
		Plan.SurfaceZ = Surface;
		Plan.CarveTopZ = Surface + Overshoot;

		int32 Step = 0;
		FVector Center = Exit;
		float Bottom = FloorZ;
		// Always at least one column past the edge so the corridor is never capped by the grid line.
		do
		{
			FDungeonPassageSegment& Seg = Plan.Segments.AddDefaulted_GetRef();
			Seg.Center = Center;
			Seg.HalfExtentXY = HalfCS;
			Seg.BottomZ = Bottom;
			Seg.TopZ = Bottom + (CeilingZ - FloorZ);
			Seg.bWalls = true;
			Seg.bFloorCeilingShell = true;
			Seg.bInsideGrid = false;

			++Step;
			Center = FVector(Exit.X + DX * Step * VoxelSize, Exit.Y + DY * Step * VoxelSize, 0.0f);
			Bottom = FloorZ + Step * Rise;
		}
		while (Bottom < Plan.CarveTopZ);

		Plan.MouthXY = FVector2D(Center.X, Center.Y);
		return Plan;
	}

	// ------------------------------------------------------------------
	// Vertical styles (FromAbove, or legacy None): one column over the opening cell.
	// ------------------------------------------------------------------
	const FIntVector Opening = Result.GetEntranceOpeningCell();
	const FVector Center = CellCenterXY(Opening);
	Plan.EntranceZ = bStopAtEntranceCellTop
		? WorldOffset.Z + (Opening.Z + 1) * CS          // the lid the mapper opens
		: WorldOffset.Z + Result.EntranceCell.Z * CS;   // through the lining down to the floor
	Plan.SurfaceZ = SampleSurfaceZ(Center.X, Center.Y);
	Plan.CarveTopZ = Plan.SurfaceZ + Overshoot;
	Plan.MouthXY = FVector2D(Center.X, Center.Y);

	if (Style == EDungeonEntranceStyle::SlopedTunnel)
	{
		// Legacy (None only, by the compatibility table): a 45° stair of cell-high columns from
		// the entrance cell toward the nearest grid edge, INSIDE the grid volume. Nothing keeps
		// the layout out of it — kept for pre-approach configs.
		const FIntVector& EC = Result.EntranceCell;
		int32 DX = -1, DY = 0, BestDist = EC.X;
		if (GS.X - 1 - EC.X < BestDist) { DX = +1; DY = 0; BestDist = GS.X - 1 - EC.X; }
		if (EC.Y < BestDist)            { DX = 0; DY = -1; BestDist = EC.Y; }
		if (GS.Y - 1 - EC.Y < BestDist) { DX = 0; DY = +1; }

		const int32 NumSteps = FMath::Max(FMath::CeilToInt32((Plan.CarveTopZ - Plan.EntranceZ) / CS), 1);
		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			const FIntVector Cell(EC.X + DX * Step, EC.Y + DY * Step, EC.Z + Step);
			FDungeonPassageSegment& Seg = Plan.Segments.AddDefaulted_GetRef();
			Seg.Center = CellCenterXY(Cell);
			Seg.HalfExtentXY = HalfCS;
			Seg.BottomZ = Plan.EntranceZ + Step * CS;
			Seg.TopZ = FMath::Min(Seg.BottomZ + CS, Plan.CarveTopZ);
			Seg.bWalls = true;
			Seg.Cell = Cell;
			Seg.bInsideGrid = InGridXY(GS, Cell) && Cell.Z < GS.Z;
		}
		return Plan;
	}

	FDungeonPassageSegment& Seg = Plan.Segments.AddDefaulted_GetRef();
	Seg.Center = Center;
	Seg.BottomZ = Plan.EntranceZ;
	Seg.TopZ = Plan.CarveTopZ;
	Seg.Cell = Opening;
	Seg.bInsideGrid = true;
	switch (Style)
	{
	case EDungeonEntranceStyle::Trapdoor:
		Seg.HalfExtentXY = VoxelSize * 0.5f; // minimal 1x1 hole, no shell
		Seg.bWalls = false;
		break;
	case EDungeonEntranceStyle::CaveOpening:
		// The stitcher displaces the radius per slice around this column (see CarveCaveOpening);
		// the footprint can exceed the cell by up to ~0.7 cells, so Clearance 1 is advisable.
		Seg.HalfExtentXY = HalfCS;
		Seg.bWalls = true;
		break;
	default: // VerticalShaft
		Seg.HalfExtentXY = HalfCS;
		Seg.bWalls = true;
		break;
	}
	return Plan;
}
