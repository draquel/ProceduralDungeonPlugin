#include "DungeonSeed.h"

FDungeonSeed::FDungeonSeed(int64 InSeed)
	: Stream(static_cast<int32>(InSeed ^ (InSeed >> 32)))
{
}

int32 FDungeonSeed::RandRange(int32 Min, int32 Max)
{
	return Stream.RandRange(Min, Max);
}

float FDungeonSeed::FRand()
{
	return Stream.FRand();
}

bool FDungeonSeed::RandBool(float Probability)
{
	return FRand() < Probability;
}

FDungeonSeed FDungeonSeed::Fork(int32 SubsystemID)
{
	// Combine current stream state with subsystem ID for a deterministic sub-seed.
	// Knuth multiplicative hash ensures good distribution across subsystem IDs.
	int32 Derived = Stream.RandRange(0, MAX_int32 - 1);
	int64 NewSeed = static_cast<int64>(Derived) ^ (static_cast<int64>(SubsystemID) * 2654435761LL);
	return FDungeonSeed(NewSeed);
}

int64 FDungeonSeed::GetCurrentSeed() const
{
	return Stream.GetCurrentSeed();
}
