// Copyright Daniel Raquel. All Rights Reserved.

#include "DungeonTorchActor.h"
#include "DungeonTileSet.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

ADungeonTorchActor::ADungeonTorchActor()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetNetUpdateFrequency(5.0f);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetMobility(EComponentMobility::Movable);
	// A bracket on a wall: queryable for an interaction trace, never a physical obstacle.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetCanEverAffectNavigation(false);
}

void ADungeonTorchActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyMesh();
	ApplyLitVisuals();
}

void ADungeonTorchActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADungeonTorchActor, bLit);
	DOREPLIFETIME(ADungeonTorchActor, TorchMesh);
	DOREPLIFETIME(ADungeonTorchActor, MeshRotationOffset);
	DOREPLIFETIME(ADungeonTorchActor, MeshScale);
}

void ADungeonTorchActor::SetupFromFixture_Implementation(const FDungeonFixture& InFixture, const UDungeonTileSet* TileSet)
{
	UStaticMesh* SconceMesh = TileSet ? TileSet->WallLightMesh.LoadSynchronous() : nullptr;
	const FRotator Offset = TileSet ? TileSet->WallLightRotationOffset : FRotator::ZeroRotator;
	SetupTorch(InFixture, SconceMesh, Offset, /*bInitiallyLit=*/true);
}

void ADungeonTorchActor::SetupTorch(const FDungeonFixture& InFixture, UStaticMesh* InMesh, const FRotator& InRotationOffset, bool bInitiallyLit)
{
	Fixture = InFixture;
	InteractableId = static_cast<int32>(FDungeonTileMapper::MakeInteractableId(
		InFixture.Cell, InFixture.FaceDX, InFixture.FaceDY, FDungeonTileMapper::InteractableKindFixture));
	TorchMesh = InMesh;
	MeshRotationOffset = InRotationOffset;
	MeshScale = static_cast<float>(InFixture.Anchor.GetScale3D().X);
	bLit = bInitiallyLit;

	ApplyMesh();
	ApplyLitVisuals();
}

bool ADungeonTorchActor::TryToggle(AActor* /*Interactor*/)
{
	return HasAuthority() && SetLit(!bLit);
}

bool ADungeonTorchActor::SetLit(bool bNewLit)
{
	if (!HasAuthority() || bNewLit == bLit)
	{
		return false;
	}
	bLit = bNewLit;
	RecordState();
	ApplyLitVisuals();
	OnLitChanged.Broadcast(this, bLit);
	return true;
}

void ADungeonTorchActor::OnRep_Lit()
{
	ApplyLitVisuals();
	OnLitChanged.Broadcast(this, bLit);
}

void ADungeonTorchActor::OnRep_Mesh()
{
	ApplyMesh();
}

void ADungeonTorchActor::ApplyMesh()
{
	if (!Mesh)
	{
		return;
	}
	Mesh->SetStaticMesh(TorchMesh);
	Mesh->SetRelativeRotation(MeshRotationOffset);
	Mesh->SetRelativeScale3D(FVector(MeshScale));
}

void ADungeonTorchActor::ApplyLitVisuals()
{
	if (bLit && !Light)
	{
		// Created on demand: an unlit torch has NO light component (the light budget rule).
		Light = NewObject<UPointLightComponent>(this, TEXT("Light"));
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(GetRootComponent());
		Light->SetRelativeLocation(LightOffset * MeshScale);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(LightIntensityLumens);
		Light->SetLightColor(LightColor);
		Light->SetAttenuationRadius(LightAttenuationRadius * MeshScale);
		Light->SetCastShadows(false);
		Light->SetIsReplicated(false);
		Light->RegisterComponent();
	}
	else if (!bLit && Light)
	{
		Light->DestroyComponent();
		Light = nullptr;
	}
	OnLitVisualsApplied();
}
