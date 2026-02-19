// Test_DungeonSeed.cpp — Unit tests for FDungeonSeed determinism and correctness
#include "Misc/AutomationTest.h"
#include "DungeonSeed.h"

// ============================================================================
// Same seed produces same sequence
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSeedSameSequence, "Dungeon.Seed.SameSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonSeedSameSequence::RunTest(const FString& Parameters)
{
	FDungeonSeed SeedA(42);
	FDungeonSeed SeedB(42);

	for (int32 i = 0; i < 100; ++i)
	{
		const int32 ValA = SeedA.RandRange(0, 1000);
		const int32 ValB = SeedB.RandRange(0, 1000);
		TestEqual(FString::Printf(TEXT("RandRange iteration %d"), i), ValA, ValB);
	}

	return true;
}

// ============================================================================
// Child calls do not affect parent state after fork
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSeedForkIndependence, "Dungeon.Seed.ForkIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonSeedForkIndependence::RunTest(const FString& Parameters)
{
	// Fork advances parent by one RNG step (to derive child seed).
	// After that, child calls must not affect the parent's subsequent values.

	// Parent A: fork, then call child many times, then read parent
	FDungeonSeed ParentA(42);
	ParentA.RandRange(0, 100); // Advance once
	FDungeonSeed ChildA = ParentA.Fork(1);
	ChildA.RandRange(0, 1000);
	ChildA.RandRange(0, 1000);
	ChildA.RandRange(0, 1000);
	const int32 ParentANext = ParentA.RandRange(0, 1000);

	// Parent B: fork, do NOT call child, then read parent
	FDungeonSeed ParentB(42);
	ParentB.RandRange(0, 100); // Same advance
	FDungeonSeed ChildB = ParentB.Fork(1);
	(void)ChildB; // Unused — child not called
	const int32 ParentBNext = ParentB.RandRange(0, 1000);

	TestEqual(TEXT("Child calls do not affect parent state"), ParentANext, ParentBNext);

	return true;
}

// ============================================================================
// Same fork ID produces deterministic child
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSeedForkDeterministic, "Dungeon.Seed.ForkDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonSeedForkDeterministic::RunTest(const FString& Parameters)
{
	FDungeonSeed SeedA(42);
	FDungeonSeed SeedB(42);

	FDungeonSeed ChildA = SeedA.Fork(7);
	FDungeonSeed ChildB = SeedB.Fork(7);

	for (int32 i = 0; i < 50; ++i)
	{
		TestEqual(FString::Printf(TEXT("Fork child iteration %d"), i),
			ChildA.RandRange(0, 1000), ChildB.RandRange(0, 1000));
	}

	return true;
}

// ============================================================================
// Different fork IDs produce different sequences
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSeedDifferentForks, "Dungeon.Seed.DifferentForks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonSeedDifferentForks::RunTest(const FString& Parameters)
{
	FDungeonSeed Seed(42);
	FDungeonSeed ChildA = Seed.Fork(1);
	FDungeonSeed ChildB = Seed.Fork(2);

	int32 DifferentCount = 0;
	for (int32 i = 0; i < 50; ++i)
	{
		if (ChildA.RandRange(0, 10000) != ChildB.RandRange(0, 10000))
		{
			DifferentCount++;
		}
	}

	TestTrue(TEXT("Different fork IDs produce different sequences"), DifferentCount > 25);

	return true;
}

// ============================================================================
// RandRange respects bounds
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSeedRandRangeBounds, "Dungeon.Seed.RandRangeBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonSeedRandRangeBounds::RunTest(const FString& Parameters)
{
	FDungeonSeed Seed(123);

	for (int32 i = 0; i < 10000; ++i)
	{
		const int32 Val = Seed.RandRange(5, 10);
		TestTrue(FString::Printf(TEXT("RandRange(%d) >= 5"), Val), Val >= 5);
		TestTrue(FString::Printf(TEXT("RandRange(%d) <= 10"), Val), Val <= 10);
	}

	return true;
}

// ============================================================================
// FRand in [0, 1)
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSeedFRandBounds, "Dungeon.Seed.FRandBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonSeedFRandBounds::RunTest(const FString& Parameters)
{
	FDungeonSeed Seed(456);

	for (int32 i = 0; i < 10000; ++i)
	{
		const float Val = Seed.FRand();
		TestTrue(FString::Printf(TEXT("FRand(%f) >= 0.0"), Val), Val >= 0.0f);
		TestTrue(FString::Printf(TEXT("FRand(%f) < 1.0"), Val), Val < 1.0f);
	}

	return true;
}

// ============================================================================
// RandBool distribution is approximately 50/50
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonSeedRandBoolDistribution, "Dungeon.Seed.RandBoolDistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonSeedRandBoolDistribution::RunTest(const FString& Parameters)
{
	FDungeonSeed Seed(789);

	int32 TrueCount = 0;
	constexpr int32 Iterations = 10000;

	for (int32 i = 0; i < Iterations; ++i)
	{
		if (Seed.RandBool(0.5f))
		{
			TrueCount++;
		}
	}

	const float Ratio = static_cast<float>(TrueCount) / static_cast<float>(Iterations);
	TestTrue(FString::Printf(TEXT("RandBool ratio %f near 0.5 (within 0.05)"), Ratio),
		FMath::Abs(Ratio - 0.5f) < 0.05f);

	return true;
}
