#pragma once

#include "CoreMinimal.h"

/**
 * FMinimumSpanningTree
 * Prim's algorithm on a weighted graph. Produces the MST edge set.
 */
struct DUNGEONCORE_API FMinimumSpanningTree
{
	/**
	 * Compute MST from edges with Euclidean distance weights.
	 * @param VertexPositions  Position of each vertex (used for edge weight = distance).
	 * @param Edges            Input edges as (vertexA, vertexB) pairs.
	 * @param RootVertex       Starting vertex (typically the entrance room).
	 * @param OutMSTEdges      Output MST edges.
	 */
	static void Compute(
		const TArray<FVector>& VertexPositions,
		const TArray<TPair<int32, int32>>& Edges,
		int32 RootVertex,
		TArray<TPair<int32, int32>>& OutMSTEdges);
};
