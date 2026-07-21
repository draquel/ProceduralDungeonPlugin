// Test_DungeonModuleTools.cpp — the authoring capture must be the exact inverse of the runtime
// expansion. Capture: Element.RelativeTransform = ComponentWorld relative to Anchor. Expansion
// (ADungeonActor): world instance = Element.RelativeTransform * Anchor. So capture-then-expand
// must reproduce the authored world transform, or a module authored around the origin lands wrong.
#include "Misc/AutomationTest.h"
#include "DungeonModuleTools.h"
#include "DungeonTileModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonModuleCaptureRoundTrip,
	"Dungeon.TileModule.Capture.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonModuleCaptureRoundTrip::RunTest(const FString& Parameters)
{
	// A non-trivial anchor (rotated, translated, scaled) and a piece placed elsewhere in the world.
	const FTransform Anchor(FRotator(0.0f, 30.0f, 0.0f), FVector(100.0f, 200.0f, 300.0f), FVector(2.0f, 2.0f, 2.0f));

	const FTransform Pieces[] = {
		FTransform(FRotator(0.0f, 90.0f, 0.0f),  FVector(150.0f, 250.0f, 350.0f), FVector(1.0f, 1.0f, 1.0f)),
		FTransform(FRotator(10.0f, -45.0f, 5.0f), FVector(-50.0f, 0.0f, 120.0f),  FVector(0.5f, 1.5f, 1.0f)),
		FTransform::Identity,
	};

	for (int32 i = 0; i < UE_ARRAY_COUNT(Pieces); ++i)
	{
		const FTransform& ComponentWorld = Pieces[i];
		const FDungeonModuleElement Element =
			UDungeonModuleTools::MakeElement(nullptr, ComponentWorld, Anchor, nullptr);

		// Runtime expansion, exactly as ADungeonActor composes it.
		const FTransform Reproduced = Element.RelativeTransform * Anchor;

		TestTrue(FString::Printf(TEXT("Piece %d: position round-trips"), i),
			Reproduced.GetLocation().Equals(ComponentWorld.GetLocation(), 0.01f));
		TestTrue(FString::Printf(TEXT("Piece %d: rotation round-trips"), i),
			Reproduced.GetRotation().Equals(ComponentWorld.GetRotation(), 0.0001f));
		TestTrue(FString::Printf(TEXT("Piece %d: scale round-trips"), i),
			Reproduced.GetScale3D().Equals(ComponentWorld.GetScale3D(), 0.01f));
	}

	return true;
}
