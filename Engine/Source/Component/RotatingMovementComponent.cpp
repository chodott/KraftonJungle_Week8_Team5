#include "Component/RotatingMovementComponent.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"
#include "Component/SceneComponent.h"
#include "Math/Transform.h"

IMPLEMENT_RTTI(URotatingMovementComponent, UMovementComponent)

void URotatingMovementComponent::Tick(float DeltaTime)
{
	if (ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	const FRotator DeltaRotation = RotationRate * DeltaTime;

	if (PivotTranslation.IsNearlyZero())
	{
		MoveUpdatedComponent(FVector::ZeroVector, DeltaRotation);
		return;
	}

	USceneComponent* UpdatedSceneComponent = GetUpdatedComponent();
	if (!UpdatedSceneComponent)
	{
		return;
	}

	const FTransform CurrentWorldTransform(UpdatedSceneComponent->GetWorldTransform());
	const FVector CurrentLocation = CurrentWorldTransform.GetTranslation();
	const FQuat CurrentRotation = CurrentWorldTransform.GetRotation();

	const FVector PivotWorldLocation =
		CurrentLocation + CurrentRotation.RotateVector(PivotTranslation);

	const FQuat NewWorldRotation =
		(DeltaRotation.Quaternion() * CurrentRotation).GetNormalized();

	const FVector NewLocation =
		PivotWorldLocation - NewWorldRotation.RotateVector(PivotTranslation);

	const FVector DeltaLocation = NewLocation - CurrentLocation;

	MoveUpdatedComponent(DeltaLocation, DeltaRotation);
}

void URotatingMovementComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UMovementComponent::DuplicateShallow(DuplicatedObject, Context);

	URotatingMovementComponent* Duplicated = static_cast<URotatingMovementComponent*>(DuplicatedObject);
	Duplicated->RotationRate = RotationRate;
	Duplicated->PivotTranslation = PivotTranslation;
}

void URotatingMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);

	Ar.Serialize("RotationRatePitch", RotationRate.Pitch);
	Ar.Serialize("RotationRateYaw", RotationRate.Yaw);
	Ar.Serialize("RotationRateRoll", RotationRate.Roll);

	Ar.Serialize("PivotTranslationX", PivotTranslation.X);
	Ar.Serialize("PivotTranslationY", PivotTranslation.Y);
	Ar.Serialize("PivotTranslationZ", PivotTranslation.Z);
}
