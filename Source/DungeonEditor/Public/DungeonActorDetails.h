#pragma once

#include "IDetailCustomization.h"

class ADungeonActor;

/**
 * Custom detail panel for ADungeonActor.
 * Organizes properties into Generation, Stats, and Debug Visualization sections.
 */
class FDungeonActorDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Build the generation stats text from the current dungeon result. */
	FText GetStatsText() const;

	TWeakObjectPtr<ADungeonActor> CachedActor;
};
