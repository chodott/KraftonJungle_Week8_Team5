#include "Component/ProjectileMovementComponent.h"
#include "Actor/Actor.h"
#include "World/World.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"
#include <cmath>

IMPLEMENT_RTTI(UProjectileMovementComponent, UMovementComponent)

void UProjectileMovementComponent::PostConstruct()
{
	UMovementComponent::PostConstruct();
	SetAutoStartSimulation(bAutoStartSimulation);
}

void UProjectileMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();

	bSimulationEnabled = bAutoStartSimulation && IsComponentTickEnabled() && !Velocity.IsNearlyZero();
}

void UProjectileMovementComponent::LaunchWithVelocity(const FVector& InVelocity)
{
	Velocity = InVelocity;
	StartSimulation();
}

void UProjectileMovementComponent::StartSimulation()
{
	SetComponentTickEnabled(true);
	bSimulationEnabled = !Velocity.IsNearlyZero();
}

void UProjectileMovementComponent::StopSimulation()
{
	bSimulationEnabled = false;
}

void UProjectileMovementComponent::SetAutoStartSimulation(bool bInAutoStartSimulation)
{
	bAutoStartSimulation = bInAutoStartSimulation;
	SetTickInEditor(bAutoStartSimulation);
	if (AActor* OwnerActor = GetOwner(); OwnerActor && bAutoStartSimulation)
	{
		OwnerActor->SetTickInEditor(true);
	}
}

void UProjectileMovementComponent::Tick(float DeltaTime)
{
	if (!bSimulationEnabled && bAutoStartSimulation && !Velocity.IsNearlyZero())
	{
		UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
		if (World && World->GetWorldType() == EWorldType::Editor)
		{
			bSimulationEnabled = IsComponentTickEnabled();
		}
	}

	if (!bSimulationEnabled)
	{
		return;
	}

	if (ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	Velocity.Z += GravityZ * GravityScale * DeltaTime;

	if (MaxSpeed > 0.0f)
	{
		const float SpeedSq = Velocity.X * Velocity.X + Velocity.Y * Velocity.Y + Velocity.Z * Velocity.Z;
		if (SpeedSq > MaxSpeed * MaxSpeed)
		{
			const float Scale = MaxSpeed / std::sqrt(SpeedSq);
			Velocity.X *= Scale;
			Velocity.Y *= Scale;
			Velocity.Z *= Scale;
		}
	}

	MoveUpdatedComponent(Velocity * DeltaTime);
}

void UProjectileMovementComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UMovementComponent::DuplicateShallow(DuplicatedObject, Context);

	UProjectileMovementComponent* Duplicated = static_cast<UProjectileMovementComponent*>(DuplicatedObject);
	Duplicated->Velocity = Velocity;
	Duplicated->GravityScale = GravityScale;
	Duplicated->MaxSpeed = MaxSpeed;
	Duplicated->bAutoStartSimulation = bAutoStartSimulation;
	Duplicated->bSimulationEnabled = false;
	Duplicated->SetAutoStartSimulation(Duplicated->bAutoStartSimulation);
}

void UProjectileMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);

	Ar.Serialize("VelocityX", Velocity.X);
	Ar.Serialize("VelocityY", Velocity.Y);
	Ar.Serialize("VelocityZ", Velocity.Z);
	Ar.Serialize("GravityScale", GravityScale);
	Ar.Serialize("MaxSpeed", MaxSpeed);
	if (Ar.IsSaving())
	{
		Ar.Serialize("AutoStartSimulation", bAutoStartSimulation);
	}
	else if (Ar.Contains("AutoStartSimulation"))
	{
		Ar.Serialize("AutoStartSimulation", bAutoStartSimulation);
	}

	if (Ar.IsLoading())
	{
		SetAutoStartSimulation(bAutoStartSimulation);
		bSimulationEnabled = false;
	}
}
