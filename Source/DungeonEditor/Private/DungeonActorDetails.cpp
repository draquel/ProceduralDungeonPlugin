#include "DungeonActorDetails.h"
#include "DungeonActor.h"
#include "DungeonTypes.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FDungeonActorDetails"

TSharedRef<IDetailCustomization> FDungeonActorDetails::MakeInstance()
{
	return MakeShareable(new FDungeonActorDetails);
}

void FDungeonActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Cache the actor pointer
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() == 1 && Objects[0].IsValid())
	{
		CachedActor = Cast<ADungeonActor>(Objects[0].Get());
	}

	// --- Section 1: Generation (reorder existing properties to top) ---
	IDetailCategoryBuilder& GenerationCategory = DetailBuilder.EditCategory(
		"Dungeon", LOCTEXT("GenerationSection", "Dungeon"), ECategoryPriority::Important);

	// Properties are already in this category via UPROPERTY — UE auto-populates them.
	// CallInEditor buttons (Generate, Clear, RandomizeSeed) auto-appear here.

	// --- Section 2: Generation Stats ---
	IDetailCategoryBuilder& StatsCategory = DetailBuilder.EditCategory(
		"Dungeon|Stats", LOCTEXT("StatsSection", "Generation Stats"), ECategoryPriority::Important);

	StatsCategory.AddCustomRow(LOCTEXT("StatsRow", "Stats"))
		.WholeRowWidget
		[
			SNew(STextBlock)
			.Text_Raw(this, &FDungeonActorDetails::GetStatsText)
			.AutoWrapText(true)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];

	// --- Section 3: Debug Visualization ---
	// The "Dungeon|Debug Visualization" category is auto-populated from UPROPERTY.
	// Ensure it appears after Stats.
	DetailBuilder.EditCategory(
		"Dungeon|Debug Visualization", LOCTEXT("DebugVizSection", "Debug Visualization"), ECategoryPriority::Default);

	// --- Section 4: Editor ---
	DetailBuilder.EditCategory(
		"Dungeon|Editor", LOCTEXT("EditorSection", "Editor"), ECategoryPriority::Default);
}

FText FDungeonActorDetails::GetStatsText() const
{
	if (!CachedActor.IsValid() || !CachedActor->HasDungeon())
	{
		return LOCTEXT("NoStats", "No dungeon generated.");
	}

	const FDungeonResult& Result = CachedActor->GetDungeonResult();

	// Count main-path rooms
	int32 MainPathRooms = 0;
	for (const FDungeonRoom& Room : Result.Rooms)
	{
		if (Room.bOnMainPath)
		{
			++MainPathRooms;
		}
	}

	// Count hallways with staircases
	int32 HallwaysWithStaircases = 0;
	for (const FDungeonHallway& Hallway : Result.Hallways)
	{
		if (Hallway.bHasStaircase)
		{
			++HallwaysWithStaircases;
		}
	}

	const FString StatsString = FString::Printf(
		TEXT("Rooms: %d (%d on main path)\nHallways: %d (%d with staircases)\nStaircases: %d\nGrid: %d x %d x %d\nGeneration Time: %.1fms\nTotal Instances: %d\nSeed: %lld"),
		Result.Rooms.Num(), MainPathRooms,
		Result.Hallways.Num(), HallwaysWithStaircases,
		Result.Staircases.Num(),
		Result.GridSize.X, Result.GridSize.Y, Result.GridSize.Z,
		Result.GenerationTimeMs,
		CachedActor->GetTotalInstanceCount(),
		Result.Seed);

	return FText::FromString(StatsString);
}

#undef LOCTEXT_NAMESPACE
