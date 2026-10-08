// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonInteractable.h"
#include "DungeonTileMapper.h"
#include "DungeonDoorActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UDungeonTileSet;

/** What a dungeon door is doing. Replicated; the swing animation is local. */
UENUM(BlueprintType)
enum class EDungeonDoorState : uint8
{
	Closed,
	Open,
	Locked,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDungeonDoorStateChanged, ADungeonDoorActor*, Door, EDungeonDoorState, NewState);

/**
 * A door leaf hung in a dungeon Doorway opening (DungeonOutput system): a replicating actor
 * spawned on authority by ADungeonActor from the tile mapper's FDungeonOpening records, while the
 * frame around it stays an instanced tile.
 *
 * Placement: the actor sits at FDungeonOpening::LeafHinge (hinge line on the finished-face plane
 * at floor level, +X across the face into the neighbour, +Y along the wall toward the opening
 * centre). The leaf mesh (the tileset's DoorLeafMesh, replicated so clients build the same leaf)
 * is scaled so its bounds match the opening's LeafWidth x LeafHeight, and swings about the hinge
 * toward -X: into the Door cell's jamb passage, never into the corridor beyond. The leaf's
 * collision blocks while it stands in the opening.
 *
 * State replicates (Closed / Open / Locked). The plugin keeps no persistence and no interaction
 * prompt: subclasses override RecordState() to persist a change and OnStateVisualsApplied() to
 * update a prompt, and call TryToggle / SetDoorState from whatever interaction system they use.
 */
UCLASS(BlueprintType, Blueprintable)
class DUNGEONOUTPUT_API ADungeonDoorActor : public AActor, public IDungeonInteractable
{
	GENERATED_BODY()

public:
	ADungeonDoorActor();

	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	// IDungeonInteractable: hangs the tileset's DoorLeafMesh, starts Closed.
	virtual void SetupFromOpening_Implementation(const FDungeonOpening& Opening, const UDungeonTileSet* TileSet) override;
	virtual void SetupFromFixture_Implementation(const FDungeonFixture& Fixture, const UDungeonTileSet* TileSet) override {}
	virtual int32 GetDungeonInteractableId_Implementation() const override { return InteractableId; }

	/**
	 * Server-side setup right after spawn (the actor must already stand at Opening.LeafHinge).
	 * @param Opening       The mapper opening the leaf hangs in (size, cell, face -> identity).
	 * @param InLeafMesh    The leaf mesh (null = no visible leaf; the door still has a state).
	 * @param InLeafRotationOffset Mesh-local rotation to the leaf convention (see UDungeonTileSet::DoorLeafRotationOffset).
	 * @param InitialState  State to start in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Door")
	void SetupDoor(const FDungeonOpening& Opening, UStaticMesh* InLeafMesh, const FRotator& InLeafRotationOffset,
		EDungeonDoorState InitialState);

	/**
	 * Open a closed door or close an open one. Authority only; a Locked door refuses.
	 * @return True if the state changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Door")
	bool TryToggle(AActor* Interactor);

	/**
	 * Force a state (authority only): locking / unlocking, or scripted opens. Recorded like an
	 * interaction (RecordState).
	 * @return True if the state changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Door")
	bool SetDoorState(EDungeonDoorState NewState);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Door")
	EDungeonDoorState GetDoorState() const { return State; }

	/** The deterministic id of the opening (see FDungeonTileMapper::MakeInteractableId); 0 before setup. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Door")
	int32 GetInteractableId() const { return InteractableId; }

	/** The opening this door was set up from (authority; default-constructed on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Door")
	const FDungeonOpening& GetOpening() const { return Opening; }

	/**
	 * The scale that fits a leaf mesh into an opening: after LeafRotationOffset the mesh spans
	 * local +Y (width) and +Z (height) from the hinge, so Y and Z scale the mesh bounds' size to
	 * the opening's LeafWidth / LeafHeight while X keeps the cell scale. Scaling is about the mesh
	 * origin (the hinge), so a leaf authored from the floor top up moves with the scale: author the
	 * tileset's DoorLeafWidth / Height equal to the mesh bounds for a 1:1 fit.
	 * @param Opening          The opening (LeafWidth, LeafHeight, LeafHinge scale).
	 * @param LeafLocalBounds  The leaf mesh's local bounding box (before the rotation offset).
	 * @param LeafRotationOffset Mesh-local rotation to the leaf convention.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Door")
	static FVector ComputeLeafScale(const FDungeonOpening& Opening, const FBox& LeafLocalBounds, const FRotator& LeafRotationOffset);

	/** Fires on every machine when the replicated state changes (after the local visuals update). */
	UPROPERTY(BlueprintAssignable, Category = "Dungeon|Door")
	FOnDungeonDoorStateChanged OnDoorStateChanged;

	/** Swing of an open leaf about the hinge, degrees (90 lays it along the jamb wall). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon|Door", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float OpenAngleDegrees = 90.0f;

	/** Swing speed, degrees per second (the swing is local; only the end state replicates). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dungeon|Door", meta = (ClampMin = "10.0"))
	float SwingDegreesPerSecond = 160.0f;

protected:
	/** Persist State after an authority change. The plugin keeps nothing; game layers override. */
	virtual void RecordState() {}

	/** After the swing target (and so the visible state) was applied on any machine: update prompts here. */
	virtual void OnStateVisualsApplied() {}

	/** Hinge line: the leaf rotates under this about local Z. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon|Door")
	TObjectPtr<USceneComponent> HingePivot;

	/** The leaf; collision blocks while it stands in the opening. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dungeon|Door")
	TObjectPtr<UStaticMeshComponent> Leaf;

private:
	UPROPERTY(ReplicatedUsing = OnRep_State)
	EDungeonDoorState State = EDungeonDoorState::Closed;

	// Leaf appearance, set on authority and replicated so every client hangs the same leaf.
	UPROPERTY(ReplicatedUsing = OnRep_Leaf)
	TObjectPtr<UStaticMesh> LeafMesh;

	UPROPERTY(Replicated)
	FVector LeafScale = FVector::OneVector;

	UPROPERTY(Replicated)
	FRotator LeafRotationOffset = FRotator::ZeroRotator;

	/** The opening (authority only). */
	FDungeonOpening Opening;
	int32 InteractableId = 0;

	float TargetYaw = 0.0f;
	bool bSwinging = false;

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnRep_Leaf();

	/** Mesh, scale and offset onto the leaf component (every machine). */
	void ApplyLeaf();

	/** Swing target for the current state; bInstant snaps (initial placement). */
	void ApplyStateVisuals(bool bInstant);
};
