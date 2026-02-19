#pragma once

#include "CoreMinimal.h"

/**
 * FDelaunayTetrahedralization
 * 3D Bowyer-Watson algorithm. Takes room center points in 3D and produces connectivity edges.
 * Handles coplanar points (single-floor dungeons) via caller-provided jitter.
 */
struct DUNGEONCORE_API FDelaunayTetrahedralization
{
	/**
	 * Compute 3D Delaunay tetrahedralization and extract unique edges.
	 * @param Points     Room center positions in 3D.
	 * @param OutEdges   Unique edges as pairs of point indices (0-based).
	 */
	static void Tetrahedralize(
		const TArray<FVector>& Points,
		TArray<TPair<int32, int32>>& OutEdges);

private:
	struct FTetrahedron
	{
		int32 V[4];
		bool ContainsVertex(int32 Idx) const;
	};

	struct FFace
	{
		int32 V[3]; // Sorted ascending for canonical comparison

		FFace(int32 A, int32 B, int32 C);
		bool operator==(const FFace& Other) const;

		friend uint32 GetTypeHash(const FFace& F)
		{
			return HashCombine(
				HashCombine(::GetTypeHash(F.V[0]), ::GetTypeHash(F.V[1])),
				::GetTypeHash(F.V[2]));
		}
	};

	struct FEdge
	{
		int32 A, B; // A < B

		FEdge(int32 InA, int32 InB);
		bool operator==(const FEdge& Other) const;

		friend uint32 GetTypeHash(const FEdge& E)
		{
			return HashCombine(::GetTypeHash(E.A), ::GetTypeHash(E.B));
		}
	};

	/** Returns true if Point is inside the circumsphere of the tetrahedron. */
	static bool IsInCircumsphere(
		const TArray<FVector>& Points,
		const FTetrahedron& Tet,
		const FVector& Point);

	/** Compute the orientation sign of tetrahedron (positive = right-handed). */
	static double Orientation(
		const FVector& A, const FVector& B,
		const FVector& C, const FVector& D);
};
