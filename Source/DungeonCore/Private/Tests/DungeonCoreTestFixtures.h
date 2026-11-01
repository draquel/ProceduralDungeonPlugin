// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DungeonConfig.h"

/** The dungeon core suites' shared fixtures (code review CR2 row 17, DNG-35). */
namespace DungeonCoreTesting
{
	/** Remove a rooted test configuration from the root (null allowed): the generation, grid-validation and room-semantics suites' cleanup. */
	inline void CleanupConfig(UDungeonConfiguration* Config)
	{
		if (Config)
		{
			Config->RemoveFromRoot();
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
