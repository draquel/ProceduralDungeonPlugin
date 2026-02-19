// RoomSemantics.cpp — Entrance selection, graph analysis, and room type assignment
#include "RoomSemantics.h"
#include "DungeonConfig.h"
#include "DungeonSeed.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoomSemantics, Log, All);

// ---------------------------------------------------------------------------
// SelectEntranceRoom
// ---------------------------------------------------------------------------

int32 FRoomSemantics::SelectEntranceRoom(
	const FDungeonResult& Result,
	const UDungeonConfiguration& Config,
	FDungeonSeed& Seed)
{
	if (Result.Rooms.Num() == 0)
	{
		return -1;
	}

	TArray<int32> Candidates;

	for (int32 i = 0; i < Result.Rooms.Num(); ++i)
	{
		const FDungeonRoom& Room = Result.Rooms[i];

		switch (Config.EntrancePlacement)
		{
		case EDungeonEntrancePlacement::BoundaryEdge:
		{
			const bool bTouchesMinX = Room.Position.X == 0;
			const bool bTouchesMaxX = (Room.Position.X + Room.Size.X) >= Result.GridSize.X;
			const bool bTouchesMinY = Room.Position.Y == 0;
			const bool bTouchesMaxY = (Room.Position.Y + Room.Size.Y) >= Result.GridSize.Y;
			if (bTouchesMinX || bTouchesMaxX || bTouchesMinY || bTouchesMaxY)
			{
				Candidates.Add(i);
			}
			break;
		}
		case EDungeonEntrancePlacement::BottomFloor:
			if (Room.Position.Z == 0)
			{
				Candidates.Add(i);
			}
			break;
		case EDungeonEntrancePlacement::TopFloor:
			if ((Room.Position.Z + Room.Size.Z) >= Result.GridSize.Z)
			{
				Candidates.Add(i);
			}
			break;
		case EDungeonEntrancePlacement::Any:
			Candidates.Add(i);
			break;
		}
	}

	// Fallback: if no candidates matched the placement rule, use all rooms
	if (Candidates.Num() == 0)
	{
		UE_LOG(LogRoomSemantics, Warning,
			TEXT("SelectEntranceRoom: No rooms match EntrancePlacement=%d, falling back to all rooms"),
			static_cast<int32>(Config.EntrancePlacement));
		for (int32 i = 0; i < Result.Rooms.Num(); ++i)
		{
			Candidates.Add(i);
		}
	}

	const int32 ChosenIdx = Seed.RandRange(0, Candidates.Num() - 1);
	return Candidates[ChosenIdx];
}

// ---------------------------------------------------------------------------
// ComputeGraphMetrics
// ---------------------------------------------------------------------------

TArray<FRoomSemanticContext> FRoomSemantics::ComputeGraphMetrics(FDungeonResult& Result)
{
	const int32 NumRooms = Result.Rooms.Num();
	TArray<FRoomSemanticContext> Contexts;
	Contexts.SetNum(NumRooms);

	if (NumRooms == 0 || Result.EntranceRoomIndex < 0)
	{
		return Contexts;
	}

	// Build adjacency map from FinalEdges
	TMap<int32, TArray<int32>> Adjacency;
	for (const auto& Edge : Result.FinalEdges)
	{
		const int32 A = static_cast<int32>(Edge.Key);
		const int32 B = static_cast<int32>(Edge.Value);
		Adjacency.FindOrAdd(A).AddUnique(B);
		Adjacency.FindOrAdd(B).AddUnique(A);
	}

	// BFS from entrance
	TArray<int32> Distance;
	Distance.SetNumUninitialized(NumRooms);
	for (int32 i = 0; i < NumRooms; ++i)
	{
		Distance[i] = -1;
	}

	TArray<int32> Parent;
	Parent.SetNumUninitialized(NumRooms);
	for (int32 i = 0; i < NumRooms; ++i)
	{
		Parent[i] = -1;
	}

	TArray<int32> Queue;
	Queue.Reserve(NumRooms);
	Queue.Add(Result.EntranceRoomIndex);
	Distance[Result.EntranceRoomIndex] = 0;

	int32 QueueHead = 0;
	while (QueueHead < Queue.Num())
	{
		const int32 Current = Queue[QueueHead++];

		if (const TArray<int32>* Neighbors = Adjacency.Find(Current))
		{
			for (const int32 Neighbor : *Neighbors)
			{
				if (Distance[Neighbor] == -1)
				{
					Distance[Neighbor] = Distance[Current] + 1;
					Parent[Neighbor] = Current;
					Queue.Add(Neighbor);
				}
			}
		}
	}

	// Find max distance and farthest room
	int32 MaxDistance = 0;
	int32 FarthestRoom = Result.EntranceRoomIndex;
	for (int32 i = 0; i < NumRooms; ++i)
	{
		if (Distance[i] > MaxDistance)
		{
			MaxDistance = Distance[i];
			FarthestRoom = i;
		}
		else if (Distance[i] == MaxDistance && i < FarthestRoom)
		{
			// Break ties by lower room index for determinism
			FarthestRoom = i;
		}
	}

	// Trace main path from farthest room back to entrance via parent pointers
	TSet<int32> MainPathSet;
	{
		int32 Current = FarthestRoom;
		while (Current != -1)
		{
			MainPathSet.Add(Current);
			Current = Parent[Current];
		}
	}

	// Build contexts
	for (int32 i = 0; i < NumRooms; ++i)
	{
		FRoomSemanticContext& Ctx = Contexts[i];
		Ctx.RoomArrayIndex = i;
		Ctx.GraphDistance = Distance[i];
		Ctx.NormalizedDistance = (MaxDistance > 0 && Distance[i] >= 0)
			? static_cast<float>(Distance[i]) / static_cast<float>(MaxDistance)
			: 0.0f;

		// Leaf node = degree 1 in adjacency graph
		if (const TArray<int32>* Neighbors = Adjacency.Find(i))
		{
			Ctx.bIsLeafNode = (Neighbors->Num() == 1);
		}
		else
		{
			Ctx.bIsLeafNode = true; // isolated node is a leaf
		}

		Ctx.bOnMainPath = MainPathSet.Contains(i);
		Ctx.bSpansMultipleFloors = Result.Rooms[i].Size.Z > 1;

		// Write back to room struct
		Result.Rooms[i].GraphDistanceFromEntrance = Distance[i];
		Result.Rooms[i].bOnMainPath = Ctx.bOnMainPath;
	}

	UE_LOG(LogRoomSemantics, Log,
		TEXT("ComputeGraphMetrics: %d rooms, maxDist=%d, farthestRoom=%d, mainPath=%d rooms"),
		NumRooms, MaxDistance, FarthestRoom, MainPathSet.Num());

	return Contexts;
}

// ---------------------------------------------------------------------------
// AssignRoomTypes
// ---------------------------------------------------------------------------

void FRoomSemantics::AssignRoomTypes(
	FDungeonResult& Result,
	const UDungeonConfiguration& Config,
	const TArray<FRoomSemanticContext>& Contexts,
	FDungeonSeed& Seed)
{
	const int32 NumRooms = Result.Rooms.Num();
	if (NumRooms == 0)
	{
		return;
	}

	// Mark entrance room
	TSet<int32> AssignedIndices;
	if (Result.EntranceRoomIndex >= 0 && Result.EntranceRoomIndex < NumRooms)
	{
		Result.Rooms[Result.EntranceRoomIndex].RoomType = EDungeonRoomType::Entrance;
		AssignedIndices.Add(Result.EntranceRoomIndex);
	}

	// Sort rules by priority descending (stable sort for determinism)
	TArray<FDungeonRoomTypeRule> SortedRules = Config.RoomTypeRules;
	SortedRules.StableSort([](const FDungeonRoomTypeRule& A, const FDungeonRoomTypeRule& B)
	{
		return A.Priority > B.Priority;
	});

	bool bBossAssigned = false;

	for (const FDungeonRoomTypeRule& Rule : SortedRules)
	{
		// Skip entrance rules — already handled
		if (Rule.RoomType == EDungeonRoomType::Entrance)
		{
			continue;
		}

		// Build eligible list with hard filters
		TArray<TPair<int32, float>> ScoredCandidates; // <RoomIndex, Score>

		for (int32 i = 0; i < NumRooms; ++i)
		{
			if (AssignedIndices.Contains(i))
			{
				continue;
			}

			const FRoomSemanticContext& Ctx = Contexts[i];
			const FDungeonRoom& Room = Result.Rooms[i];

			// Hard filter: unreachable rooms
			if (Ctx.GraphDistance < 0)
			{
				continue;
			}

			// Hard filter: normalized distance range
			if (Ctx.NormalizedDistance < Rule.MinGraphDistanceFromEntrance ||
				Ctx.NormalizedDistance > Rule.MaxGraphDistanceFromEntrance)
			{
				continue;
			}

			// Hard filter: multi-floor requirement
			if (Rule.bRequireMultiFloor && !Ctx.bSpansMultipleFloors)
			{
				continue;
			}

			// Hard filter: minimum size
			if (Rule.MinSize != FIntVector::ZeroValue)
			{
				if (Room.Size.X < Rule.MinSize.X ||
					Room.Size.Y < Rule.MinSize.Y ||
					Room.Size.Z < Rule.MinSize.Z)
				{
					continue;
				}
			}

			// Compute score
			float Score = 0.0f;
			if (Rule.bPreferLeafNodes && Ctx.bIsLeafNode)
			{
				Score += 1.0f;
			}
			if (Rule.bPreferMainPath && Ctx.bOnMainPath)
			{
				Score += 1.0f;
			}

			// Center preference: bonus for being close to the midpoint of the distance range
			const float Midpoint = (Rule.MinGraphDistanceFromEntrance + Rule.MaxGraphDistanceFromEntrance) * 0.5f;
			Score += 0.5f * (1.0f - FMath::Abs(Ctx.NormalizedDistance - Midpoint));

			ScoredCandidates.Add(TPair<int32, float>(i, Score));
		}

		// Sort by score descending, break ties by room index for determinism
		ScoredCandidates.StableSort([](const TPair<int32, float>& A, const TPair<int32, float>& B)
		{
			if (!FMath::IsNearlyEqual(A.Value, B.Value, KINDA_SMALL_NUMBER))
			{
				return A.Value > B.Value;
			}
			return A.Key < B.Key;
		});

		// Assign up to Rule.Count rooms
		int32 Assigned = 0;
		for (const auto& Candidate : ScoredCandidates)
		{
			if (Assigned >= Rule.Count)
			{
				break;
			}

			Result.Rooms[Candidate.Key].RoomType = Rule.RoomType;
			AssignedIndices.Add(Candidate.Key);
			Assigned++;

			if (Rule.RoomType == EDungeonRoomType::Boss)
			{
				bBossAssigned = true;
			}
		}

		// Boss guarantee fallback: relax distance constraint if Boss rule failed
		if (Rule.RoomType == EDungeonRoomType::Boss && Assigned == 0 && Config.bGuaranteeBossRoom)
		{
			UE_LOG(LogRoomSemantics, Log,
				TEXT("Boss rule matched 0 rooms with distance filter, relaxing constraints"));

			// Find best unassigned room by score without distance filter
			int32 BestIdx = -1;
			float BestScore = -1.0f;

			for (int32 i = 0; i < NumRooms; ++i)
			{
				if (AssignedIndices.Contains(i) || Contexts[i].GraphDistance < 0)
				{
					continue;
				}

				float Score = 0.0f;
				if (Rule.bPreferMainPath && Contexts[i].bOnMainPath)
				{
					Score += 1.0f;
				}
				Score += Contexts[i].NormalizedDistance; // prefer farther rooms

				if (Score > BestScore || (FMath::IsNearlyEqual(Score, BestScore) && i < BestIdx))
				{
					BestScore = Score;
					BestIdx = i;
				}
			}

			if (BestIdx >= 0)
			{
				Result.Rooms[BestIdx].RoomType = EDungeonRoomType::Boss;
				AssignedIndices.Add(BestIdx);
				bBossAssigned = true;
			}
		}
	}

	// Global boss guarantee: if no Boss rule existed at all but bGuaranteeBossRoom is true
	if (Config.bGuaranteeBossRoom && !bBossAssigned)
	{
		UE_LOG(LogRoomSemantics, Log,
			TEXT("No Boss rule in config, auto-assigning farthest main-path room as Boss"));

		int32 BestIdx = -1;
		float BestDistance = -1.0f;

		for (int32 i = 0; i < NumRooms; ++i)
		{
			if (AssignedIndices.Contains(i) || Contexts[i].GraphDistance < 0)
			{
				continue;
			}

			// Prefer main-path rooms, then farthest
			float Score = Contexts[i].NormalizedDistance;
			if (Contexts[i].bOnMainPath)
			{
				Score += 10.0f; // strong preference for main path
			}

			if (Score > BestDistance || (FMath::IsNearlyEqual(Score, BestDistance) && i < BestIdx))
			{
				BestDistance = Score;
				BestIdx = i;
			}
		}

		if (BestIdx >= 0)
		{
			Result.Rooms[BestIdx].RoomType = EDungeonRoomType::Boss;
			AssignedIndices.Add(BestIdx);
		}
	}

	// Log summary
	TMap<EDungeonRoomType, int32> TypeCounts;
	for (const FDungeonRoom& Room : Result.Rooms)
	{
		TypeCounts.FindOrAdd(Room.RoomType, 0)++;
	}

	UE_LOG(LogRoomSemantics, Log,
		TEXT("AssignRoomTypes: Entrance=%d, Boss=%d, Treasure=%d, Generic=%d (of %d total)"),
		TypeCounts.FindRef(EDungeonRoomType::Entrance),
		TypeCounts.FindRef(EDungeonRoomType::Boss),
		TypeCounts.FindRef(EDungeonRoomType::Treasure),
		TypeCounts.FindRef(EDungeonRoomType::Generic),
		NumRooms);
}
