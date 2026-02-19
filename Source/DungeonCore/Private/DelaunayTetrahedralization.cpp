#include "DelaunayTetrahedralization.h"

// ============================================================================
// Helper types
// ============================================================================

FDelaunayTetrahedralization::FFace::FFace(int32 A, int32 B, int32 C)
{
	V[0] = A; V[1] = B; V[2] = C;
	// Bubble sort 3 elements for canonical order
	if (V[0] > V[1]) Swap(V[0], V[1]);
	if (V[1] > V[2]) Swap(V[1], V[2]);
	if (V[0] > V[1]) Swap(V[0], V[1]);
}

bool FDelaunayTetrahedralization::FFace::operator==(const FFace& Other) const
{
	return V[0] == Other.V[0] && V[1] == Other.V[1] && V[2] == Other.V[2];
}

FDelaunayTetrahedralization::FEdge::FEdge(int32 InA, int32 InB)
	: A(FMath::Min(InA, InB))
	, B(FMath::Max(InA, InB))
{
}

bool FDelaunayTetrahedralization::FEdge::operator==(const FEdge& Other) const
{
	return A == Other.A && B == Other.B;
}

bool FDelaunayTetrahedralization::FTetrahedron::ContainsVertex(int32 Idx) const
{
	return V[0] == Idx || V[1] == Idx || V[2] == Idx || V[3] == Idx;
}

// ============================================================================
// Orientation test
// ============================================================================

double FDelaunayTetrahedralization::Orientation(
	const FVector& A, const FVector& B,
	const FVector& C, const FVector& D)
{
	// dot(cross(B-A, C-A), D-A) — positive = right-handed orientation
	const FVector AB(B.X - A.X, B.Y - A.Y, B.Z - A.Z);
	const FVector AC(C.X - A.X, C.Y - A.Y, C.Z - A.Z);
	const FVector AD(D.X - A.X, D.Y - A.Y, D.Z - A.Z);

	const double CrossX = (double)AB.Y * AC.Z - (double)AB.Z * AC.Y;
	const double CrossY = (double)AB.Z * AC.X - (double)AB.X * AC.Z;
	const double CrossZ = (double)AB.X * AC.Y - (double)AB.Y * AC.X;

	return CrossX * AD.X + CrossY * AD.Y + CrossZ * AD.Z;
}

// ============================================================================
// Circumsphere test (4x4 determinant)
// ============================================================================

namespace
{
	FORCEINLINE double Det3x3(
		double a, double b, double c,
		double d, double e, double f,
		double g, double h, double i)
	{
		return a * (e * i - f * h)
		     - b * (d * i - f * g)
		     + c * (d * h - e * g);
	}
}

bool FDelaunayTetrahedralization::IsInCircumsphere(
	const TArray<FVector>& Points,
	const FTetrahedron& Tet,
	const FVector& P)
{
	// Coordinates relative to test point P
	const FVector& A = Points[Tet.V[0]];
	const FVector& B = Points[Tet.V[1]];
	const FVector& C = Points[Tet.V[2]];
	const FVector& D = Points[Tet.V[3]];

	const double ax = A.X - P.X, ay = A.Y - P.Y, az = A.Z - P.Z;
	const double bx = B.X - P.X, by = B.Y - P.Y, bz = B.Z - P.Z;
	const double cx = C.X - P.X, cy = C.Y - P.Y, cz = C.Z - P.Z;
	const double dx = D.X - P.X, dy = D.Y - P.Y, dz = D.Z - P.Z;

	const double aw = ax * ax + ay * ay + az * az;
	const double bw = bx * bx + by * by + bz * bz;
	const double cw = cx * cx + cy * cy + cz * cz;
	const double dw = dx * dx + dy * dy + dz * dz;

	// 4x4 determinant via Laplace expansion along the 4th column:
	// | ax ay az aw |
	// | bx by bz bw |
	// | cx cy cz cw |
	// | dx dy dz dw |
	const double det =
		  aw * Det3x3(bx, by, bz, cx, cy, cz, dx, dy, dz)
		- bw * Det3x3(ax, ay, az, cx, cy, cz, dx, dy, dz)
		+ cw * Det3x3(ax, ay, az, bx, by, bz, dx, dy, dz)
		- dw * Det3x3(ax, ay, az, bx, by, bz, cx, cy, cz);

	// For a positively-oriented tetrahedron ABCD, det > 0 means P is inside circumsphere.
	// Check the orientation of the tetrahedron and flip sign if needed.
	const double Orient = Orientation(A, B, C, D);
	if (Orient > 0.0)
	{
		return det > 0.0;
	}
	else if (Orient < 0.0)
	{
		return det < 0.0;
	}

	// Degenerate tetrahedron (zero volume) — treat as not containing the point
	return false;
}

// ============================================================================
// Bowyer-Watson 3D
// ============================================================================

void FDelaunayTetrahedralization::Tetrahedralize(
	const TArray<FVector>& Points,
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

	if (NumPoints == 3)
	{
		OutEdges.Add(TPair<int32, int32>(0, 1));
		OutEdges.Add(TPair<int32, int32>(0, 2));
		OutEdges.Add(TPair<int32, int32>(1, 2));
		return;
	}

	// Build extended point array: original points + 4 super-tetrahedron vertices
	TArray<FVector> AllPoints = Points;

	// Compute bounding box
	FVector Min = Points[0];
	FVector Max = Points[0];
	for (int32 i = 1; i < NumPoints; ++i)
	{
		Min.X = FMath::Min(Min.X, Points[i].X);
		Min.Y = FMath::Min(Min.Y, Points[i].Y);
		Min.Z = FMath::Min(Min.Z, Points[i].Z);
		Max.X = FMath::Max(Max.X, Points[i].X);
		Max.Y = FMath::Max(Max.Y, Points[i].Y);
		Max.Z = FMath::Max(Max.Z, Points[i].Z);
	}

	const FVector Center = (Min + Max) * 0.5f;
	float Extent = FMath::Max3(Max.X - Min.X, Max.Y - Min.Y, Max.Z - Min.Z);
	if (Extent < 1.0f) Extent = 100.0f;
	Extent *= 3.0f;

	// Super-tetrahedron: regular tetrahedron enclosing all points (positively oriented)
	const int32 SuperA = AllPoints.Num();
	const int32 SuperB = SuperA + 1;
	const int32 SuperC = SuperA + 2;
	const int32 SuperD = SuperA + 3;

	AllPoints.Add(Center + FVector( Extent,  Extent,  Extent));
	AllPoints.Add(Center + FVector( Extent, -Extent, -Extent));
	AllPoints.Add(Center + FVector(-Extent, -Extent,  Extent));
	AllPoints.Add(Center + FVector(-Extent,  Extent, -Extent));

	// Initial tetrahedralization with super-tetrahedron
	TArray<FTetrahedron> Tetrahedra;
	{
		FTetrahedron Super;
		Super.V[0] = SuperA;
		Super.V[1] = SuperB;
		Super.V[2] = SuperC;
		Super.V[3] = SuperD;

		// Ensure positive orientation
		if (Orientation(AllPoints[SuperA], AllPoints[SuperB],
		                AllPoints[SuperC], AllPoints[SuperD]) < 0.0)
		{
			Swap(Super.V[2], Super.V[3]);
		}

		Tetrahedra.Add(Super);
	}

	// Insert each point
	for (int32 PointIdx = 0; PointIdx < NumPoints; ++PointIdx)
	{
		const FVector& P = AllPoints[PointIdx];

		// Find bad tetrahedra (circumsphere contains the new point)
		TArray<int32> BadIndices;
		for (int32 i = 0; i < Tetrahedra.Num(); ++i)
		{
			if (IsInCircumsphere(AllPoints, Tetrahedra[i], P))
			{
				BadIndices.Add(i);
			}
		}

		if (BadIndices.Num() == 0)
		{
			// Point is outside all circumspheres — shouldn't happen with a proper super-tet
			continue;
		}

		// Extract boundary faces (faces appearing in exactly one bad tetrahedron)
		TMap<FFace, int32> FaceCount;
		for (int32 BadIdx : BadIndices)
		{
			const FTetrahedron& BadTet = Tetrahedra[BadIdx];
			FFace Faces[4] = {
				FFace(BadTet.V[0], BadTet.V[1], BadTet.V[2]),
				FFace(BadTet.V[0], BadTet.V[1], BadTet.V[3]),
				FFace(BadTet.V[0], BadTet.V[2], BadTet.V[3]),
				FFace(BadTet.V[1], BadTet.V[2], BadTet.V[3]),
			};

			for (const FFace& Face : Faces)
			{
				int32& Count = FaceCount.FindOrAdd(Face, 0);
				Count++;
			}
		}

		TArray<FFace> BoundaryFaces;
		for (const auto& Pair : FaceCount)
		{
			if (Pair.Value == 1)
			{
				BoundaryFaces.Add(Pair.Key);
			}
		}

		// Remove bad tetrahedra (reverse order to preserve indices with RemoveAtSwap)
		BadIndices.Sort([](int32 A, int32 B) { return A > B; });
		for (int32 BadIdx : BadIndices)
		{
			Tetrahedra.RemoveAtSwap(BadIdx);
		}

		// Create new tetrahedra from each boundary face + the new point
		for (const FFace& Face : BoundaryFaces)
		{
			FTetrahedron NewTet;
			NewTet.V[0] = Face.V[0];
			NewTet.V[1] = Face.V[1];
			NewTet.V[2] = Face.V[2];
			NewTet.V[3] = PointIdx;

			// Ensure positive orientation
			if (Orientation(AllPoints[NewTet.V[0]], AllPoints[NewTet.V[1]],
			                AllPoints[NewTet.V[2]], AllPoints[NewTet.V[3]]) < 0.0)
			{
				Swap(NewTet.V[1], NewTet.V[2]);
			}

			Tetrahedra.Add(NewTet);
		}
	}

	// Extract unique edges between original points from ALL tetrahedra
	// (including those containing super-tet vertices — we just skip super-tet endpoints)
	TSet<FEdge> UniqueEdges;
	for (const FTetrahedron& Tet : Tetrahedra)
	{
		// A tetrahedron has 6 edges — only keep edges where both endpoints are original points
		const int32 Verts[4] = { Tet.V[0], Tet.V[1], Tet.V[2], Tet.V[3] };
		for (int32 i = 0; i < 4; ++i)
		{
			for (int32 j = i + 1; j < 4; ++j)
			{
				if (Verts[i] < NumPoints && Verts[j] < NumPoints)
				{
					UniqueEdges.Add(FEdge(Verts[i], Verts[j]));
				}
			}
		}
	}

	for (const FEdge& Edge : UniqueEdges)
	{
		OutEdges.Add(TPair<int32, int32>(Edge.A, Edge.B));
	}

	// Sort for determinism (TSet iteration order is not guaranteed)
	OutEdges.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value);
	});
}
