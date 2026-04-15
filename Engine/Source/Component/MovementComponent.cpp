#include "Component/MovementComponent.h"
#include "Component/SceneComponent.h"
#include "Actor/Actor.h"
#include "Math/Transform.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UMovementComponent, UActorComponent)

void UMovementComponent::PostConstruct()
{
	bCanEverTick = true;
}

void UMovementComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	EnsureUpdatedComponent();
}

void UMovementComponent::OnPostLoad()
{
	ResolveUpdatedComponent();
}

void UMovementComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);

	if (Ar.IsSaving())
	{
		uint32 UpdatedComponentUUID = UpdatedComponent ? UpdatedComponent->UUID : 0;
		Ar.Serialize("UpdatedComponentUUID", UpdatedComponentUUID);
		return;
	}

	if (!Ar.Contains("UpdatedComponentUUID"))
	{
		PendingUpdatedComponentUUID = 0;
		return;
	}

	Ar.Serialize("UpdatedComponentUUID", PendingUpdatedComponentUUID);
}

void UMovementComponent::SetUpdatedComponent(USceneComponent* InComponent)
{
	UpdatedComponent = InComponent;
}

bool UMovementComponent::EnsureUpdatedComponent()
{
	if (UpdatedComponent)
	{
		return true;
	}

	if (AActor* OwnerActor = GetOwner())
	{
		UpdatedComponent = OwnerActor->GetRootComponent();
	}

	return UpdatedComponent != nullptr;
}

void UMovementComponent::ResolveUpdatedComponent()
{
	UpdatedComponent = nullptr;

	if (PendingUpdatedComponentUUID != 0)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			for (UActorComponent* Component : OwnerActor->GetComponents())
			{
				if (!Component || !Component->IsA(USceneComponent::StaticClass()))
				{
					continue;
				}

				if (Component->UUID == PendingUpdatedComponentUUID)
				{
					UpdatedComponent = static_cast<USceneComponent*>(Component);
					break;
				}
			}
		}
	}

	EnsureUpdatedComponent();
	PendingUpdatedComponentUUID = 0;
}

bool UMovementComponent::ShouldSkipUpdate(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return true;
	}

	return !EnsureUpdatedComponent();
}

void UMovementComponent::MoveUpdatedComponent(const FVector& DeltaLocation, const FRotator& DeltaRotation)
{
	if (!EnsureUpdatedComponent())
	{
		return;
	}

	FTransform NewWorldTransform(UpdatedComponent->GetWorldTransform());
	NewWorldTransform.AddToTranslation(DeltaLocation);

	if (!DeltaRotation.IsNearlyZero())
	{
		const FQuat NewWorldRotation =
			(DeltaRotation.Quaternion() * NewWorldTransform.GetRotation()).GetNormalized();
		NewWorldTransform.SetRotation(NewWorldRotation);
	}

	if (USceneComponent* AttachParent = UpdatedComponent->GetAttachParent())
	{
		const FTransform ParentWorldTransform(AttachParent->GetWorldTransform());
		UpdatedComponent->SetRelativeTransform(NewWorldTransform * ParentWorldTransform.Inverse());
		return;
	}

	UpdatedComponent->SetRelativeTransform(NewWorldTransform);
}

void UMovementComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UActorComponent::DuplicateShallow(DuplicatedObject, Context);

	UMovementComponent* Duplicated = static_cast<UMovementComponent*>(DuplicatedObject);
	Duplicated->UpdatedComponent = nullptr;
}

void UMovementComponent::FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const
{
	UActorComponent::FixupDuplicatedReferences(DuplicatedObject, Context);

	UMovementComponent* Duplicated = static_cast<UMovementComponent*>(DuplicatedObject);
	Duplicated->UpdatedComponent = Context.FindDuplicate(UpdatedComponent.Get());
}
