#include "MinimumSpanningTree.h"

void FMinimumSpanningTree::Compute(
	const TArray<FVector>& VertexPositions,
	const TArray<TPair<int32, int32>>& Edges,
	int32 RootVertex,
	TArray<TPair<int32, int32>>& OutMSTEdges)
{
	OutMSTEdges.Reset();
	const int32 NumVertices = VertexPositions.Num();

	if (NumVertices <= 1 || Edges.Num() == 0)
	{
		return;
	}

	// Build adjacency list
	struct FWeightedEdge
	{
		int32 To;
		float Weight;
	};

	TArray<TArray<FWeightedEdge>> Adjacency;
	Adjacency.SetNum(NumVertices);

	for (const auto& Edge : Edges)
	{
		if (Edge.Key < 0 || Edge.Key >= NumVertices || Edge.Value < 0 || Edge.Value >= NumVertices)
		{
			continue;
		}

		const float Weight = FVector::Dist(VertexPositions[Edge.Key], VertexPositions[Edge.Value]);
		Adjacency[Edge.Key].Add({Edge.Value, Weight});
		Adjacency[Edge.Value].Add({Edge.Key, Weight});
	}

	// Prim's algorithm with binary heap
	struct FCandidate
	{
		float Weight;
		int32 From;
		int32 To;

		bool operator<(const FCandidate& Other) const
		{
			return Weight < Other.Weight;
		}

		// For HeapPush/HeapPop (UE heap is max-heap, so invert comparison)
		bool operator>(const FCandidate& Other) const
		{
			return Weight > Other.Weight;
		}
	};

	TArray<bool> InMST;
	InMST.SetNumZeroed(NumVertices);
	InMST[RootVertex] = true;
	int32 MSTCount = 1;

	// Priority queue (min-heap via inverted predicate)
	TArray<FCandidate> PQ;

	// Seed with edges from root
	for (const FWeightedEdge& Adj : Adjacency[RootVertex])
	{
		PQ.HeapPush({Adj.Weight, RootVertex, Adj.To},
			[](const FCandidate& A, const FCandidate& B) { return A.Weight > B.Weight; });
	}

	while (PQ.Num() > 0 && MSTCount < NumVertices)
	{
		FCandidate Best;
		PQ.HeapPop(Best,
			[](const FCandidate& A, const FCandidate& B) { return A.Weight > B.Weight; });

		if (InMST[Best.To])
		{
			continue;
		}

		// Add edge to MST
		InMST[Best.To] = true;
		MSTCount++;
		OutMSTEdges.Add(TPair<int32, int32>(
			FMath::Min(Best.From, Best.To),
			FMath::Max(Best.From, Best.To)));

		// Add new candidate edges
		for (const FWeightedEdge& Adj : Adjacency[Best.To])
		{
			if (!InMST[Adj.To])
			{
				PQ.HeapPush({Adj.Weight, Best.To, Adj.To},
					[](const FCandidate& A, const FCandidate& B) { return A.Weight > B.Weight; });
			}
		}
	}
}
