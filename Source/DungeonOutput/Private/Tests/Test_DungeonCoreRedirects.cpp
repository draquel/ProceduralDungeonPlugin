// Copyright Daniel Raquel. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/CoreRedirects.h"

#if WITH_DEV_AUTOMATION_TESTS

// The plugin's [CoreRedirects] (Config/DefaultProceduralDungeonPlugin.ini) for the reflected name the code review's CR3
// changed (CODE_REVIEW_PLAN.md §17, D-CR4): the old name resolves to the new one, so an actor saved with it loads.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonCoreRedirectsTest,
	"Dungeon.Config.CoreRedirects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonCoreRedirectsTest::RunTest(const FString& Parameters)
{
	const FCoreRedirectObjectName Old(TEXT("/Script/DungeonOutput.DungeonActor.bOpenEntranceCeiling"));
	const FCoreRedirectObjectName Expected(TEXT("/Script/DungeonOutput.DungeonActor.bOpenEntranceOpening"));
	const FCoreRedirectObjectName Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Property, Old);
	TestTrue(FString::Printf(TEXT("bOpenEntranceCeiling redirects to bOpenEntranceOpening (got %s)"), *Redirected.ToString()),
		Redirected == Expected);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
