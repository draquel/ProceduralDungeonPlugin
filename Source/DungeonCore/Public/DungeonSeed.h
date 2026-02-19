#pragma once

#include "CoreMinimal.h"

/**
 * FDungeonSeed
 * Deterministic RNG wrapper. All randomness in the generator flows through this.
 * Wraps FRandomStream for cross-platform determinism.
 */
struct DUNGEONCORE_API FDungeonSeed
{
	explicit FDungeonSeed(int64 InSeed);

	/** Random int in [Min, Max] inclusive. */
	int32 RandRange(int32 Min, int32 Max);

	/** Random float in [0, 1). */
	float FRand();

	/** Random bool with given probability of true. */
	bool RandBool(float Probability = 0.5f);

	/** Fork a deterministic child seed for a sub-system. */
	FDungeonSeed Fork(int32 SubsystemID);

	int64 GetCurrentSeed() const;

private:
	FRandomStream Stream;
};
