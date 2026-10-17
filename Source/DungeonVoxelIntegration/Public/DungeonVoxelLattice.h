#pragma once

#include "CoreMinimal.h"

/**
 * The target voxel world's sampling lattice.
 *
 * VoxelWorlds is a CORNER-sampled grid: the density for index I is generated at
 * WorldOrigin + I * VoxelSize (GenerateVoxelDensity.usf), the meshers place geometry for index I
 * at I * VoxelSize (P0 = float3(VX,VY,VZ) * VoxelSize), and UVoxelEditManager::WorldToLocalPos
 * resolves a world position W to index Floor((W - WorldOrigin) / VoxelSize), i.e. the half-open
 * interval [I*s, (I+1)*s) resolves to I. There is no "+0.5": index I IS the world point I*s.
 *
 * The first version of this lattice assumed centre sampling (I + 0.5) * VoxelSize. Every carve
 * then landed half a voxel low on each axis, so the rock surface ranged over +/- a full voxel
 * around each cell plane instead of +/- half a voxel: rock intruded up to 70 cm past the +X/+Y
 * walls of the demo dungeon (measured by line trace on 2026-09-24) and receded on the -X/-Y
 * walls. SamplePosition / RangeForBox now use the corner convention; EditPosition keeps the
 * half-voxel offset purely so the engine's Floor resolves it to the intended index without
 * floating-point risk at the interval boundary.
 *
 * Why the lattice exists at all: a dungeon grid is laid out from an arbitrary WorldOffset (pad
 * centre minus the entrance-cell offset), so the cell grid has no fixed phase against this
 * lattice, and CellWorldSize is rarely an integer multiple of VoxelSize (the demo runs 400 / 75).
 * Enumerating edit positions in CELL space (CellMin + (V + 0.5) * VoxelSize for V in
 * [0, VoxelsPerCell)) both misses voxels at the far edge of every cell and writes others twice,
 * leaving a ragged solid rind through every carved room. Enumerating in LATTICE space assigns
 * each sample to exactly one cell by containment, so adjacent cells tile with no gaps and no
 * double writes, and the union of the open cells carves clean.
 *
 * Contract for tile authoring: with this convention the meshed rock surface lies within half a
 * voxel of every cell plane (it is the midpoint between the last carved sample and the first
 * solid one). Module faces inset by >= VoxelSize / 2 from the cell plane are never buried. With
 * UDungeonVoxelConfig::CarveMarginVoxels = 0.5 the band moves outward to [plane, plane + VoxelSize)
 * and rock never crosses a plane inward, so faces and the recesses behind them may sit at any
 * inset (the outer seal moves out by the same margin, so its thickness is unchanged).
 */
struct FDungeonVoxelLattice
{
	/** Lattice anchor: the voxel world's WorldOrigin. */
	FVector Origin = FVector::ZeroVector;

	/** Lattice pitch: the voxel world's VoxelSize. */
	double VoxelSize = 100.0;

	FDungeonVoxelLattice() = default;

	FDungeonVoxelLattice(const FVector& InOrigin, double InVoxelSize)
		: Origin(InOrigin)
		, VoxelSize(InVoxelSize > 0.0 ? InVoxelSize : 100.0)
	{
	}

	/** World-space point the sample at Index represents: where it is generated and meshed. */
	FVector SamplePosition(const FIntVector& Index) const
	{
		return Origin + FVector(Index) * VoxelSize;
	}

	/**
	 * World-space position to hand UVoxelEditManager::ApplyEdit so that it resolves to Index.
	 * The engine floors (W - Origin) / VoxelSize, so the middle of the index's interval is the
	 * safest point: exact multiples can round to the neighbour below in floating point.
	 */
	FVector EditPosition(const FIntVector& Index) const
	{
		return Origin + (FVector(Index) + FVector(0.5)) * VoxelSize;
	}

	/**
	 * Inclusive index range of every sample whose position lies in [BoxMin, BoxMax).
	 * Half-open on the max side so abutting boxes partition the lattice exactly.
	 */
	void RangeForBox(const FVector& BoxMin, const FVector& BoxMax, FIntVector& OutMin, FIntVector& OutMax) const
	{
		OutMin = FIntVector(
			FirstSampleAtOrAfter(BoxMin.X, Origin.X),
			FirstSampleAtOrAfter(BoxMin.Y, Origin.Y),
			FirstSampleAtOrAfter(BoxMin.Z, Origin.Z));
		OutMax = FIntVector(
			FirstSampleAtOrAfter(BoxMax.X, Origin.X) - 1,
			FirstSampleAtOrAfter(BoxMax.Y, Origin.Y) - 1,
			FirstSampleAtOrAfter(BoxMax.Z, Origin.Z) - 1);
	}

	/** True when the range is non-empty on every axis. */
	static bool IsRangeValid(const FIntVector& Min, const FIntVector& Max)
	{
		return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
	}

	// ==================== The dungeon's frame (spherical Phase 5, SP-G6) ====================
	// A dungeon's cells, passages and boxes are laid out in its frame: the world on a heightmap world (identity), a planet
	// POI's pad space (the tangent frame at its centre) on a planet. The lattice stays the world's (axis-aligned); with a
	// rotated frame a box in frame space is enumerated by testing each sample of its world bounds in frame space.

	/** Frame space to world. Identity (the default) is the heightmap case: frame space is the world. */
	FTransform Frame = FTransform::Identity;

	/** The frame is not the identity (set by SetFrame). */
	bool bRotated = false;

	/** Lay the dungeon out in a frame (identity: the world). */
	void SetFrame(const FTransform& InFrame)
	{
		Frame = InFrame;
		Frame.SetScale3D(FVector::OneVector);
		bRotated = !Frame.Equals(FTransform::Identity, 0.0);
	}

	/** A sample's position in frame space (SamplePosition with the identity frame). */
	FVector FramePosition(const FIntVector& Index) const
	{
		return bRotated ? Frame.InverseTransformPositionNoScale(SamplePosition(Index)) : SamplePosition(Index);
	}

	/** A sample's edit position in frame space (EditPosition with the identity frame). */
	FVector FrameEditPosition(const FIntVector& Index) const
	{
		return bRotated ? Frame.InverseTransformPositionNoScale(EditPosition(Index)) : EditPosition(Index);
	}

	/** A frame-space point in the world. */
	FVector ToWorld(const FVector& FramePoint) const
	{
		return bRotated ? Frame.TransformPositionNoScale(FramePoint) : FramePoint;
	}

	/**
	 * Every sample whose frame-space position lies in the frame-space box [BoxMin, BoxMax), in lattice order (Z, Y, X).
	 * With the identity frame exactly RangeForBox's samples in its order; with a rotated frame the samples of the box's
	 * world bounds whose FramePosition lies inside it.
	 */
	template <typename FFunc>
	void ForEachSampleInBox(const FVector& BoxMin, const FVector& BoxMax, FFunc&& Func) const
	{
		FIntVector Min, Max;
		if (!bRotated)
		{
			RangeForBox(BoxMin, BoxMax, Min, Max);
		}
		else
		{
			FBox World(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				World += Frame.TransformPositionNoScale(FVector(
					(Corner & 1) ? BoxMax.X : BoxMin.X, (Corner & 2) ? BoxMax.Y : BoxMin.Y, (Corner & 4) ? BoxMax.Z : BoxMin.Z));
			}
			RangeForBox(World.Min, World.Max + FVector(VoxelSize), Min, Max);
		}
		if (!IsRangeValid(Min, Max))
		{
			return;
		}
		for (int32 IZ = Min.Z; IZ <= Max.Z; ++IZ)
		{
			for (int32 IY = Min.Y; IY <= Max.Y; ++IY)
			{
				for (int32 IX = Min.X; IX <= Max.X; ++IX)
				{
					const FIntVector Index(IX, IY, IZ);
					if (bRotated)
					{
						const FVector P = FramePosition(Index);
						if (P.X < BoxMin.X || P.Y < BoxMin.Y || P.Z < BoxMin.Z || P.X >= BoxMax.X || P.Y >= BoxMax.Y || P.Z >= BoxMax.Z)
						{
							continue;
						}
					}
					Func(Index);
				}
			}
		}
	}

private:
	/** Lowest index on one axis whose sample position is >= Bound. */
	int32 FirstSampleAtOrAfter(double Bound, double OriginAxis) const
	{
		return FMath::CeilToInt32((Bound - OriginAxis) / VoxelSize);
	}
};
