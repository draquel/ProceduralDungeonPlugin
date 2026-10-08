// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonInteractable.h"
#include "DungeonTileMapper.h"
#include "DungeonTorchActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;
class UDungeonTileSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDungeonTorchLitChanged, ADungeonTorchActor*, Torch, bool, bLit);

/**
 * A wall torch on a dungeon wall (DungeonOutput system): the dungeon's light source, a
 * replicating actor spawned on authority by ADungeonActor from the tile mapper's FDungeonFixture
 * records (the tileset's FDungeonFixtureRules decide where).
 *
 * Placement: the actor sits at FDungeonFixture::Anchor (mount point on the finished wall face,
 * +X off the wall into the cell); the sconce mesh (the tileset's WallLightMesh, replicated) hangs
 * there with its rotation offset. Lit torches carry one shadowless point light, CREATED when lit
 * and DESTROYED when put out: an unlit torch costs nothing in Lumen or in light replication.
 *
 * bLit replicates. The plugin keeps no persistence and no interaction prompt: subclasses override
 * RecordState() and OnLitVisualsApplied(), and call TryToggle / SetLit from their interaction system.
 */
UCLASS(BlueprintType, Blueprintable)
class DUNGEONOUTPUT_API ADungeonTorchActor : public AActor, public IDungeonInteractable
{
	GENERATED_BODY()

public:
	ADungeonTorchActor();

	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// IDungeonInteractable: hangs the tileset's WallLightMesh, starts lit.
	virtual void SetupFromOpening_Implementation(const FDungeonOpening& Opening, const UDungeonTileSet* TileSet) override {}
	virtual void SetupFromFixture_Implementation(const FDungeonFixture& Fixture, const UDungeonTileSet* TileSet) override;
	virtual int32 GetDungeonInteractableId_Implementation() const override { return InteractableId; }

	/**
	 * Server-side setup right after spawn (the actor must already stand at Fixture.Anchor).
	 * @param Fixture    The mapper fixture (cell, face -> identity, mount scale).
	 * @param InMesh     The sconce mesh (null = bare light).
	 * @param InRotationOffset Mesh-local rotation to the fixture convention (see UDungeonTileSet::WallLightRotationOffset).
	 * @param bInitiallyLit Whether the torch starts lit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Torch")
	void SetupTorch(const FDungeonFixture& Fixture, UStaticMesh* InMesh, const FRotator& InRotationOffset, bool bInitiallyLit);

	/** Light a dark torch or put a lit one out. Authority only. @return True if the state changed. */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Torch")
	bool TryToggle(AActor* Interactor);

	/** Force lit / unlit (authority only); recorded like an interaction. @return True if changed. */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Torch")
	bool SetLit(bool bNewLit);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Torch")
	bool IsLit() const { return bLit; }

	/** The deterministic id of the fixture (see FDungeonTileMapper::MakeInteractableId); 0 before setup. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Torch")
	int32 GetInteractableId() const { return InteractableId; }

	/** The fixture this torch was set up from (authority; default-constructed on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Torch")
	const FDungeonFixture& GetFixture() const { return Fixture; }

	/** Fires on every machine when the replicated lit state changes (after the local light update). */
	UPROPERTY(BlueprintAssignable, Category = "Dungeon|Torch")
	FOnDungeonTorchLitChanged OnLitChanged;

	/** Point light colour (warm flame by default). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon|Torch")
	FLinearColor LightColor = FLinearColor(1.0f, 0.72f, 0.45f);

	/** Point light intensity in lumens. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon|Torch", meta = (ClampMin = "0.0"))
	float LightIntensityLumens = 1000.0f;

	/** Point light attenuation radius (world units at reference cell size). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon|Torch", meta = (ClampMin = "50.0"))
	float LightAttenuationRadius = 800.0f;

	/** Light source position relative to the mount (fixture frame: +X off the wall, +Z up): the flame, not the bracket. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon|Torch")
	FVector LightOffset = FVector(35.0f, 0.0f, 50.0f);

protected:
	/** Persist bLit after an authority change. The plugin keeps nothing; game layers override. */
	virtual void RecordState() {}

	/** After the light was created / destroyed for bLit on any machine: update prompts here. */
	virtual void OnLitVisualsApplied() {}

	/** The sconce / torch mesh at the mount point. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon|Torch")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** The light, present only while lit (see class comment). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Dungeon|Torch")
	TObjectPtr<UPointLightComponent> Light;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Lit)
	bool bLit = true;

	UPROPERTY(ReplicatedUsing = OnRep_Mesh)
	TObjectPtr<UStaticMesh> TorchMesh;

	UPROPERTY(Replicated)
	FRotator MeshRotationOffset = FRotator::ZeroRotator;

	UPROPERTY(Replicated)
	float MeshScale = 1.0f;

	/** The fixture (authority only). */
	FDungeonFixture Fixture;
	int32 InteractableId = 0;

	UFUNCTION()
	void OnRep_Lit();

	UFUNCTION()
	void OnRep_Mesh();

	void ApplyMesh();

	/** Create or destroy the light for bLit (every machine). */
	void ApplyLitVisuals();
};
