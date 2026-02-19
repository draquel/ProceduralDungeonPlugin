#pragma once

#include "CoreMinimal.h"

/**
 * FDelaunayTriangulation
 * 2D Bowyer-Watson Delaunay triangulation.
 * Takes room center points and produces connectivity edges.
 * Phase 1: 2D only. Will be extended to 3D tetrahedralization in Phase 2.
 */
struct DUNGEONCORE_API FDelaunayTriangulation
{
	/**
	 * Compute Delaunay triangulation of 2D points.
	 * @param Points     Room center positions projected to 2D (X, Z plane).
	 * @param OutEdges   Unique edges as pairs of point indices (0-based).
	 */
	static void Triangulate(
		const TArray<FVector2D>& Points,
		TArray<TPair<int32, int32>>& OutEdges);

private:
	struct FTriangle
	{
		int32 V[3];
		bool ContainsVertex(int32 Idx) const;
	};

	struct FEdge
	{
		int32 A, B;

		FEdge() : A(0), B(0) {}
		FEdge(int32 InA, int32 InB);
		bool operator==(const FEdge& Other) const;

		friend uint32 GetTypeHash(const FEdge& E)
		{
			return HashCombine(::GetTypeHash(FMath::Min(E.A, E.B)),
				::GetTypeHash(FMath::Max(E.A, E.B)));
		}
	};

	/** Returns true if Point is inside the circumcircle of the triangle. */
	static bool IsInCircumcircle(
		const TArray<FVector2D>& Points,
		const FTriangle& Tri,
		const FVector2D& Point);
};
