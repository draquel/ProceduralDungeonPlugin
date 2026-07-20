#pragma once

#include "CoreMinimal.h"

/**
 * The target voxel world's sampling lattice.
 *
 * Voxel centres sit at WorldOrigin + (Index + 0.5) * VoxelSize — the same mapping
 * UVoxelEditManager::WorldToLocalPos applies when it floors (WorldPos - WorldOrigin) / VoxelSize
 * to resolve an edit to a voxel.
 *
 * Why this exists: a dungeon grid is laid out from an arbitrary WorldOffset (pad centre minus the
 * entrance-cell offset), so the cell grid has no fixed phase against this lattice, and
 * CellWorldSize is rarely an integer multiple of VoxelSize (the demo runs 400 / 75). Enumerating
 * edit positions in CELL space — CellMin + (V + 0.5) * VoxelSize for V in [0, VoxelsPerCell) —
 * therefore both misses voxels at the far edge of every cell and writes others twice, leaving a
 * ragged solid rind through every carved room. Enumerate in LATTICE space instead: each voxel
 * belongs to exactly one cell by centre containment, so adjacent cells tile with no gaps and no
 * double writes, and the union of the open cells carves clean.
 */
struct FDungeonVoxelLattice
{
	/** Lattice anchor — the voxel world's WorldOrigin. */
	FVector Origin = FVector::ZeroVector;

	/** Lattice pitch — the voxel world's VoxelSize. */
	double VoxelSize = 100.0;

	FDungeonVoxelLattice() = default;

	FDungeonVoxelLattice(const FVector& InOrigin, double InVoxelSize)
		: Origin(InOrigin)
		, VoxelSize(InVoxelSize > 0.0 ? InVoxelSize : 100.0)
	{
	}

	/** World-space centre of the voxel at Index. */
	FVector Center(const FIntVector& Index) const
	{
		return Origin + (FVector(Index) + FVector(0.5)) * VoxelSize;
	}

	/**
	 * Inclusive index range of every voxel whose CENTRE lies in [BoxMin, BoxMax).
	 * Half-open on the max side so abutting boxes partition the lattice exactly.
	 */
	void RangeForBox(const FVector& BoxMin, const FVector& BoxMax, FIntVector& OutMin, FIntVector& OutMax) const
	{
		OutMin = FIntVector(
			FirstCentreAtOrAfter(BoxMin.X, Origin.X),
			FirstCentreAtOrAfter(BoxMin.Y, Origin.Y),
			FirstCentreAtOrAfter(BoxMin.Z, Origin.Z));

		OutMax = FIntVector(
			FirstCentreAtOrAfter(BoxMax.X, Origin.X) - 1,
			FirstCentreAtOrAfter(BoxMax.Y, Origin.Y) - 1,
			FirstCentreAtOrAfter(BoxMax.Z, Origin.Z) - 1);
	}

	/** True when the range is non-empty on every axis. */
	static bool IsRangeValid(const FIntVector& Min, const FIntVector& Max)
	{
		return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
	}

private:
	/** Lowest index on one axis whose centre is >= Bound. */
	int32 FirstCentreAtOrAfter(double Bound, double OriginAxis) const
	{
		return FMath::CeilToInt32((Bound - OriginAxis) / VoxelSize - 0.5);
	}
};
