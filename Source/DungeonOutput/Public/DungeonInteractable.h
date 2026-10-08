// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DungeonTileMapper.h"
#include "DungeonInteractable.generated.h"

class UDungeonTileSet;

UINTERFACE(BlueprintType, Blueprintable, MinimalAPI)
class UDungeonInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by actors that ADungeonActor hangs from its tile map (DungeonOutput system).
 *
 * The tile actor spawns one DoorActorClass per Doorway opening (at FDungeonOpening::LeafHinge)
 * and one WallLightActorClass per WallLight fixture (at FDungeonFixture::Anchor), then calls the
 * matching Setup function so the actor can size itself to the opening, pick the tileset's leaf /
 * sconce mesh and derive its deterministic FDungeonTileMapper::MakeInteractableId. A class that
 * does not implement the interface is still spawned at the record's transform, just not told
 * about it. ADungeonDoorActor and ADungeonTorchActor are the plugin's implementations; game
 * layers subclass them (or implement this in Blueprint) to add prompts and persistence.
 */
class DUNGEONOUTPUT_API IDungeonInteractable
{
	GENERATED_BODY()

public:
	/**
	 * Called on authority right after the actor is spawned at Opening.LeafHinge.
	 * @param Opening  The mapper's opening record (cell, face, hinge frame, leaf size).
	 * @param TileSet  The dungeon's tileset (DoorLeafMesh, DoorLeafRotationOffset, WallProfile).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon|Interactables")
	void SetupFromOpening(const FDungeonOpening& Opening, const UDungeonTileSet* TileSet);

	/**
	 * Called on authority right after the actor is spawned at Fixture.Anchor.
	 * @param Fixture  The mapper's fixture record (cell, face, mount frame).
	 * @param TileSet  The dungeon's tileset (WallLightMesh, WallLightRotationOffset).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon|Interactables")
	void SetupFromFixture(const FDungeonFixture& Fixture, const UDungeonTileSet* TileSet);

	/** The deterministic id of the opening / fixture this actor was set up from (0 before setup). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Dungeon|Interactables")
	int32 GetDungeonInteractableId() const;
};
