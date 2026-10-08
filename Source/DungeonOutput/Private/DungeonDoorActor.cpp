// Copyright Daniel Raquel. All Rights Reserved.

#include "DungeonDoorActor.h"
#include "DungeonTileSet.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

ADungeonDoorActor::ADungeonDoorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	bReplicates = true;
	SetNetUpdateFrequency(10.0f);

	// Unscaled root at the hinge line; the pivot rotates under it and the (scaled) leaf hangs off
	// the pivot, so the swing never runs under a non-uniform scale.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	HingePivot = CreateDefaultSubobject<USceneComponent>(TEXT("HingePivot"));
	HingePivot->SetupAttachment(Root);
	HingePivot->SetMobility(EComponentMobility::Movable);

	Leaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Leaf"));
	Leaf->SetupAttachment(HingePivot);
	Leaf->SetMobility(EComponentMobility::Movable);
	Leaf->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Leaf->SetCollisionResponseToAllChannels(ECR_Block);
	Leaf->SetCanEverAffectNavigation(true);
}

void ADungeonDoorActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Clients: the replicated leaf / state may already have arrived (initial bunch) before this
	// point, or arrive later through the OnReps. Either way apply what we have now.
	ApplyLeaf();
	ApplyStateVisuals(/*bInstant=*/true);
}

void ADungeonDoorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADungeonDoorActor, State);
	DOREPLIFETIME(ADungeonDoorActor, LeafMesh);
	DOREPLIFETIME(ADungeonDoorActor, LeafScale);
	DOREPLIFETIME(ADungeonDoorActor, LeafRotationOffset);
}

void ADungeonDoorActor::SetupFromOpening_Implementation(const FDungeonOpening& InOpening, const UDungeonTileSet* TileSet)
{
	UStaticMesh* Mesh = TileSet ? TileSet->DoorLeafMesh.LoadSynchronous() : nullptr;
	const FRotator Offset = TileSet ? TileSet->DoorLeafRotationOffset : FRotator::ZeroRotator;
	SetupDoor(InOpening, Mesh, Offset, EDungeonDoorState::Closed);
}

FVector ADungeonDoorActor::ComputeLeafScale(const FDungeonOpening& InOpening, const FBox& LeafLocalBounds, const FRotator& InLeafRotationOffset)
{
	const float CellScale = static_cast<float>(InOpening.LeafHinge.GetScale3D().X);
	if (!LeafLocalBounds.IsValid)
	{
		return FVector(CellScale);
	}
	const FBox InFrame = LeafLocalBounds.TransformBy(FTransform(InLeafRotationOffset));
	const FVector Size = InFrame.GetSize();
	return FVector(
		CellScale,
		Size.Y > KINDA_SMALL_NUMBER ? InOpening.LeafWidth / static_cast<float>(Size.Y) : CellScale,
		Size.Z > KINDA_SMALL_NUMBER ? InOpening.LeafHeight / static_cast<float>(Size.Z) : CellScale);
}

void ADungeonDoorActor::SetupDoor(const FDungeonOpening& InOpening, UStaticMesh* InLeafMesh, const FRotator& InLeafRotationOffset,
	EDungeonDoorState InitialState)
{
	Opening = InOpening;
	InteractableId = static_cast<int32>(FDungeonTileMapper::MakeInteractableId(
		InOpening.Cell, InOpening.FaceDX, InOpening.FaceDY, FDungeonTileMapper::InteractableKindDoor));

	LeafMesh = InLeafMesh;
	LeafRotationOffset = InLeafRotationOffset;
	LeafScale = ComputeLeafScale(InOpening, InLeafMesh ? InLeafMesh->GetBoundingBox() : FBox(ForceInit), InLeafRotationOffset);
	State = InitialState;

	ApplyLeaf();
	ApplyStateVisuals(/*bInstant=*/true);
}

bool ADungeonDoorActor::TryToggle(AActor* /*Interactor*/)
{
	if (!HasAuthority() || State == EDungeonDoorState::Locked)
	{
		return false;
	}
	return SetDoorState(State == EDungeonDoorState::Closed ? EDungeonDoorState::Open : EDungeonDoorState::Closed);
}

bool ADungeonDoorActor::SetDoorState(EDungeonDoorState NewState)
{
	if (!HasAuthority() || NewState == State)
	{
		return false;
	}
	State = NewState;
	RecordState();
	// OnRep only fires on clients — apply locally for the server / standalone.
	ApplyStateVisuals(/*bInstant=*/false);
	OnDoorStateChanged.Broadcast(this, State);
	return true;
}

void ADungeonDoorActor::OnRep_State()
{
	ApplyStateVisuals(/*bInstant=*/false);
	OnDoorStateChanged.Broadcast(this, State);
}

void ADungeonDoorActor::OnRep_Leaf()
{
	ApplyLeaf();
}

void ADungeonDoorActor::ApplyLeaf()
{
	if (!Leaf)
	{
		return;
	}
	Leaf->SetStaticMesh(LeafMesh);
	Leaf->SetRelativeRotation(LeafRotationOffset);
	Leaf->SetRelativeScale3D(LeafScale);
}

void ADungeonDoorActor::ApplyStateVisuals(bool bInstant)
{
	// Open swings toward -X: +Y (the leaf) rotated by +90 about Z points at -X — into the Door
	// cell's jamb passage, away from the corridor beyond the frame.
	TargetYaw = (State == EDungeonDoorState::Open) ? OpenAngleDegrees : 0.0f;
	if (bInstant || !HingePivot)
	{
		if (HingePivot)
		{
			HingePivot->SetRelativeRotation(FRotator(0.0f, TargetYaw, 0.0f));
		}
		bSwinging = false;
		SetActorTickEnabled(false);
	}
	else
	{
		bSwinging = true;
		SetActorTickEnabled(true);
	}
	OnStateVisualsApplied();
}

void ADungeonDoorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSwinging || !HingePivot)
	{
		SetActorTickEnabled(false);
		return;
	}
	const float Yaw = HingePivot->GetRelativeRotation().Yaw;
	const float Next = FMath::FInterpConstantTo(Yaw, TargetYaw, DeltaSeconds, SwingDegreesPerSecond);
	HingePivot->SetRelativeRotation(FRotator(0.0f, Next, 0.0f));
	if (FMath::IsNearlyEqual(Next, TargetYaw, 0.01f))
	{
		bSwinging = false;
		SetActorTickEnabled(false);
	}
}
