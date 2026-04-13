#include "RotatingMovementComponent.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(URotatingMovementComponent, UMovementComponent)

void URotatingMovementComponent::Tick(float DeltaTime)
{
	if (ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	MoveUpdatedComponent(FVector::ZeroVector, RotationRate * DeltaTime);
}

void URotatingMovementComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UMovementComponent::DuplicateShallow(DuplicatedObject, Context);

	URotatingMovementComponent* Duplicated = static_cast<URotatingMovementComponent*>(DuplicatedObject);
	Duplicated->RotationRate = RotationRate;
}

void URotatingMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);

	Ar.Serialize("RotationRatePitch", RotationRate.Pitch);
	Ar.Serialize("RotationRateYaw", RotationRate.Yaw);
	Ar.Serialize("RotationRateRoll", RotationRate.Roll);
}
