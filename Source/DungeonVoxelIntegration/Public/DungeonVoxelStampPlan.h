#pragma once

#include "CoreMinimal.h"
#include "DungeonVoxelLattice.h"

struct FDungeonGrid;

/**
 * Pure lattice-index planning for the voxel stamper: which samples belong to the carved void, and
 * which samples each outer seal slab writes. No voxel world involved, so the decisions can be unit
 * tested on a hand-built grid.
 *
 * Every open (traversable) cell of the grid claims the lattice samples whose SAMPLE position lies
 * in its box, exactly as CarveCell enumerates them. The outer seal of a face is the slab just
 * outside that face, widened laterally by the wall thickness so the six slabs of a cell close at
 * its edges and corners. That widening, and the seal of a face shared with another open cell, reach
 * samples that belong to the void: a floor seal written for the cell above dips WallThickness into
 * the cell below, and a wall seal on a room / hallway face lies entirely inside the hallway. Those
 * samples must never be re-solidified, so the seal is filtered against the carved-sample set.
 *
 * The set is keyed by lattice index, not by world position: the first implementation classified a
 * seal sample by flooring its EDIT position (sample + half a voxel on every axis) into cell space,
 * so the last sample layer before every +X / +Y / +Z cell plane was attributed to the next cell over
 * whenever the lattice phase was under half a voxel, and any slab reaching it (WallThickness of
 * lateral widening from a vertically adjacent cell's floor or ceiling seal) wrote it solid: one
 * full voxel of rock standing inside the room, varying with height along the wall. Symmetrically,
 * the sample just outside every -X / -Y / -Z plane was attributed to the cell and left unsealed.
 */
struct DUNGEONVOXELINTEGRATION_API FDungeonVoxelStampPlan
{
	/** Cell faces, in the stamper's order: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z (ceiling), 5=-Z (floor). */
	static constexpr int32 NumFaces = 6;

	/**
	 * World-space box of one face slab of a cell.
	 *
	 * bOutward=false: the slab lies INSIDE the cell (the stone lining of a voxel-carved dungeon).
	 * bOutward=true: the slab lies just OUTSIDE the cell and is widened laterally by Thickness so
	 * the six slabs overlap at the cell's edges and corners and form a closed shell.
	 *
	 * @param CellWorldMin  Min corner of the cell.
	 * @param CellWorldSize Cell edge length in world units.
	 * @param Face          Face index (see NumFaces).
	 * @param Thickness     Slab depth in world units.
	 * @param bOutward      Inside (lining) or outside (seal) the cell.
	 * @param OutMin        Receives the slab's min corner.
	 * @param OutMax        Receives the slab's max corner.
	 */
	static void FaceSlabBox(
		const FVector& CellWorldMin,
		float CellWorldSize,
		int32 Face,
		float Thickness,
		bool bOutward,
		FVector& OutMin,
		FVector& OutMax);

	/**
	 * Add the lattice index of every sample inside every open cell of the grid to OutSamples.
	 * Mirrors CarveCell's enumeration exactly (same lattice, same half-open cell boxes, each
	 * expanded by CarveMargin on every side).
	 *
	 * @param Grid          Dungeon grid.
	 * @param Lattice       Target voxel world's lattice.
	 * @param WorldOffset   World position of grid cell (0,0,0)'s min corner.
	 * @param CellWorldSize Cell edge length in world units.
	 * @param CarveMargin   How far past every cell plane the void is carved, in world units
	 *                      (UDungeonVoxelConfig::CarveMarginVoxels * VoxelSize; 0 = the cell box).
	 * @param OutSamples    Receives the indices (existing entries are kept).
	 */
	static void CollectOpenCellSamples(
		const FDungeonGrid& Grid,
		const FDungeonVoxelLattice& Lattice,
		const FVector& WorldOffset,
		float CellWorldSize,
		float CarveMargin,
		TSet<FIntVector>& OutSamples);

	/**
	 * The samples one face's outer seal writes: every sample of the outward slab that is not part
	 * of any open cell and has not already been sealed.
	 *
	 * @param Lattice         Target voxel world's lattice.
	 * @param CellWorldMin    Min corner of the cell whose face is sealed.
	 * @param CellWorldSize   Cell edge length in world units.
	 * @param Face            Face index (see NumFaces).
	 * @param Thickness       Seal depth in world units (WallThickness * VoxelSize).
	 * @param OpenCellSamples Indices of every sample inside any open cell (CollectOpenCellSamples).
	 * @param SealedSamples   Indices already sealed by this stamp; every index returned is added, so
	 *                        overlapping slabs write each sample once.
	 * @param OutToWrite      Receives the indices to write solid, in lattice order (appended).
	 */
	static void CollectOuterSealSamples(
		const FDungeonVoxelLattice& Lattice,
		const FVector& CellWorldMin,
		float CellWorldSize,
		int32 Face,
		float Thickness,
		const TSet<FIntVector>& OpenCellSamples,
		TSet<FIntVector>& SealedSamples,
		TArray<FIntVector>& OutToWrite);
};
