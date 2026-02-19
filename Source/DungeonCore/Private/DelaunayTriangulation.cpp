#include "DelaunayTriangulation.h"

// ============================================================================
// FEdge
// ============================================================================

FDelaunayTriangulation::FEdge::FEdge(int32 InA, int32 InB)
	: A(FMath::Min(InA, InB))
	, B(FMath::Max(InA, InB))
{
}

bool FDelaunayTriangulation::FEdge::operator==(const FEdge& Other) const
{
	return A == Other.A && B == Other.B;
}

// ============================================================================
// FTriangle
// ============================================================================

bool FDelaunayTriangulation::FTriangle::ContainsVertex(int32 Idx) const
{
	return V[0] == Idx || V[1] == Idx || V[2] == Idx;
}

// ============================================================================
// Circumcircle test
// ============================================================================

bool FDelaunayTriangulation::IsInCircumcircle(
	const TArray<FVector2D>& Points,
	const FTriangle& Tri,
	const FVector2D& P)
{
	// Incircle test via 3x3 determinant (coordinates relative to P).
	// For CCW-oriented triangle ABC, det > 0 means P is inside circumcircle.
	const FVector2D& A = Points[Tri.V[0]];
	const FVector2D& B = Points[Tri.V[1]];
	const FVector2D& C = Points[Tri.V[2]];

	const double ax = A.X - P.X, ay = A.Y - P.Y;
	const double bx = B.X - P.X, by = B.Y - P.Y;
	const double cx = C.X - P.X, cy = C.Y - P.Y;

	const double aSq = ax * ax + ay * ay;
	const double bSq = bx * bx + by * by;
	const double cSq = cx * cx + cy * cy;

	const double det = ax * (by * cSq - cy * bSq)
	                 - bx * (ay * cSq - cy * aSq)
	                 + cx * (ay * bSq - by * aSq);

	return det > 0.0;
}

// ============================================================================
// Bowyer-Watson
// ============================================================================

void FDelaunayTriangulation::Triangulate(
	const TArray<FVector2D>& Points,
	TArray<TPair<int32, int32>>& OutEdges)
{
	OutEdges.Reset();
	const int32 NumPoints = Points.Num();

	// Edge cases
	if (NumPoints < 2)
	{
		return;
	}

	if (NumPoints == 2)
	{
		OutEdges.Add(TPair<int32, int32>(0, 1));
		return;
	}

	// Build extended point array: original points + 3 super-triangle vertices
	TArray<FVector2D> AllPoints = Points;

	// Compute bounding box
	FVector2D Min = Points[0];
	FVector2D Max = Points[0];
	for (int32 i = 1; i < NumPoints; ++i)
	{
		Min.X = FMath::Min(Min.X, Points[i].X);
		Min.Y = FMath::Min(Min.Y, Points[i].Y);
		Max.X = FMath::Max(Max.X, Points[i].X);
		Max.Y = FMath::Max(Max.Y, Points[i].Y);
	}

	const double DeltaX = Max.X - Min.X;
	const double DeltaY = Max.Y - Min.Y;
	const double DeltaMax = FMath::Max(DeltaX, DeltaY);
	const double MidX = (Min.X + Max.X) * 0.5;
	const double MidY = (Min.Y + Max.Y) * 0.5;

	// Super-triangle vertices (large enough to contain all points)
	const int32 SuperA = AllPoints.Num();
	const int32 SuperB = SuperA + 1;
	const int32 SuperC = SuperA + 2;

	AllPoints.Add(FVector2D(MidX - 2.0 * DeltaMax, MidY - DeltaMax));
	AllPoints.Add(FVector2D(MidX + 2.0 * DeltaMax, MidY - DeltaMax));
	AllPoints.Add(FVector2D(MidX, MidY + 2.0 * DeltaMax));

	// Ensure super-triangle is CCW
	// Cross product (B-A) x (C-A) should be positive
	{
		const FVector2D& SA = AllPoints[SuperA];
		const FVector2D& SB = AllPoints[SuperB];
		const FVector2D& SC = AllPoints[SuperC];
		const double Cross = (SB.X - SA.X) * (SC.Y - SA.Y) - (SB.Y - SA.Y) * (SC.X - SA.X);
		if (Cross < 0.0)
		{
			AllPoints.Swap(SuperB, SuperC);
		}
	}

	// Initial triangulation with super-triangle
	TArray<FTriangle> Triangles;
	{
		FTriangle Super;
		Super.V[0] = SuperA;
		Super.V[1] = SuperB;
		Super.V[2] = SuperC;
		Triangles.Add(Super);
	}

	// Insert each point
	for (int32 PointIdx = 0; PointIdx < NumPoints; ++PointIdx)
	{
		const FVector2D& P = AllPoints[PointIdx];

		// Find all triangles whose circumcircle contains P
		TArray<FTriangle> BadTriangles;
		for (const FTriangle& Tri : Triangles)
		{
			if (IsInCircumcircle(AllPoints, Tri, P))
			{
				BadTriangles.Add(Tri);
			}
		}

		// Find boundary polygon (edges of bad triangles not shared with other bad triangles)
		TArray<FEdge> BoundaryPolygon;
		for (int32 i = 0; i < BadTriangles.Num(); ++i)
		{
			const FTriangle& BadTri = BadTriangles[i];

			FEdge Edges[3] = {
				FEdge(BadTri.V[0], BadTri.V[1]),
				FEdge(BadTri.V[1], BadTri.V[2]),
				FEdge(BadTri.V[2], BadTri.V[0]),
			};

			for (const FEdge& Edge : Edges)
			{
				bool bShared = false;
				for (int32 j = 0; j < BadTriangles.Num(); ++j)
				{
					if (i == j) continue;

					const FTriangle& OtherTri = BadTriangles[j];
					FEdge OtherEdges[3] = {
						FEdge(OtherTri.V[0], OtherTri.V[1]),
						FEdge(OtherTri.V[1], OtherTri.V[2]),
						FEdge(OtherTri.V[2], OtherTri.V[0]),
					};

					for (const FEdge& OtherEdge : OtherEdges)
					{
						if (Edge == OtherEdge)
						{
							bShared = true;
							break;
						}
					}
					if (bShared) break;
				}

				if (!bShared)
				{
					BoundaryPolygon.Add(Edge);
				}
			}
		}

		// Remove bad triangles
		for (const FTriangle& BadTri : BadTriangles)
		{
			Triangles.RemoveAll([&BadTri](const FTriangle& T)
			{
				return T.V[0] == BadTri.V[0] && T.V[1] == BadTri.V[1] && T.V[2] == BadTri.V[2];
			});
		}

		// Create new triangles from boundary edges to the inserted point
		for (const FEdge& Edge : BoundaryPolygon)
		{
			FTriangle NewTri;

			// Ensure CCW winding
			const FVector2D& EA = AllPoints[Edge.A];
			const FVector2D& EB = AllPoints[Edge.B];
			const double Cross = (EB.X - EA.X) * (P.Y - EA.Y)
			                   - (EB.Y - EA.Y) * (P.X - EA.X);

			if (Cross > 0.0)
			{
				NewTri.V[0] = Edge.A;
				NewTri.V[1] = Edge.B;
				NewTri.V[2] = PointIdx;
			}
			else
			{
				NewTri.V[0] = Edge.B;
				NewTri.V[1] = Edge.A;
				NewTri.V[2] = PointIdx;
			}

			Triangles.Add(NewTri);
		}
	}

	// Remove triangles that share a vertex with the super-triangle
	Triangles.RemoveAll([SuperA, SuperB, SuperC](const FTriangle& T)
	{
		return T.ContainsVertex(SuperA) || T.ContainsVertex(SuperB) || T.ContainsVertex(SuperC);
	});

	// Extract unique edges from remaining triangles
	TSet<FEdge> UniqueEdges;
	for (const FTriangle& Tri : Triangles)
	{
		UniqueEdges.Add(FEdge(Tri.V[0], Tri.V[1]));
		UniqueEdges.Add(FEdge(Tri.V[1], Tri.V[2]));
		UniqueEdges.Add(FEdge(Tri.V[2], Tri.V[0]));
	}

	for (const FEdge& Edge : UniqueEdges)
	{
		OutEdges.Add(TPair<int32, int32>(Edge.A, Edge.B));
	}

	// Sort edges for determinism (TSet iteration order is not guaranteed)
	OutEdges.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value);
	});
}
