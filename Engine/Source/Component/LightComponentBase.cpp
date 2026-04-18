#include "LightComponentBase.h"

#include "Actor/Actor.h"
#include "Level/Level.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(ULightComponentBase, USceneComponent)

FVector ULightComponentBase::GetEmissionDirectionWS() const
{
	const FVector ForwardAxis = GetWorldTransform().GetUnitAxis(EAxis::X).GetSafeNormal();
	return ForwardAxis.IsNearlyZero() ? FVector::ForwardVector : ForwardAxis;
}

//	Shader에서 음수로 뒤집지 않아도 됨
FVector ULightComponentBase::GetDirectionToLightWS() const
{
	return -GetEmissionDirectionWS();
}

void ULightComponentBase::MarkTransformDirty()
{
	USceneComponent::MarkTransformDirty();

	if (AActor* Owner = GetOwner())
	{
		if (ULevel* Level = Owner->GetLevel())
		{
			Level->MarkSpatialDirty();
		}
	}
}
