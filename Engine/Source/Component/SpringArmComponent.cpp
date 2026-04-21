#include "Component/SpringArmComponent.h"

#include "Object/Class.h"
#include "Serializer/Archive.h"
#include "Math/Quat.h"
#include <algorithm>

IMPLEMENT_RTTI(USpringArmComponent, USceneComponent)

void USpringArmComponent::SetTargetArmLength(float InTargetArmLength)
{
	TargetArmLength = std::max(0.0f, InTargetArmLength);
}

void USpringArmComponent::SetSocketOffset(const FVector& InSocketOffset)
{
	SocketOffset = InSocketOffset;
}

FVector USpringArmComponent::GetSocketWorldLocation() const
{
	const FMatrix& WorldTransform = GetWorldTransform();
	const FVector Pivot = WorldTransform.GetTranslation();
	const FRotator Rotation = FQuat(WorldTransform).Rotator();
	const FVector Forward = Rotation.Vector().GetSafeNormal();
	const FVector RotatedSocketOffset = Rotation.RotateVector(SocketOffset);
	return Pivot - Forward * TargetArmLength + RotatedSocketOffset;
}

FRotator USpringArmComponent::GetSocketWorldRotation() const
{
	return FQuat(GetWorldTransform()).Rotator();
}

void USpringArmComponent::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar.Serialize("TargetArmLength", TargetArmLength);
	Ar.Serialize("SocketOffset", SocketOffset);
}

void USpringArmComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	USceneComponent::DuplicateShallow(DuplicatedObject, Context);
	USpringArmComponent* DuplicatedSpringArmComponent = static_cast<USpringArmComponent*>(DuplicatedObject);
	DuplicatedSpringArmComponent->TargetArmLength = TargetArmLength;
	DuplicatedSpringArmComponent->SocketOffset = SocketOffset;
}
