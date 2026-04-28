#include "LightComponentBase.h"

#include "Actor/Actor.h"
#include "Level/Level.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

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
	if (Mobility != ELightMobility::Static)
	{
		bShadowCacheDirty = true;
	}
	if (AActor* Owner = GetOwner())
	{
		if (ULevel* Level = Owner->GetLevel())
		{
			Level->MarkSpatialDirty();
		}
	}
}


void ULightComponentBase::SetMobility(ELightMobility InMobility)
{
	if (Mobility == InMobility) return;
	Mobility = InMobility;
	if (AActor* Owner = GetOwner())
	{
		if (ULevel* Level = Owner->GetLevel())
			Level->MarkSpatialDirty();
	}
}

void ULightComponentBase::SetCastingShadows(bool bNewCastShadows)
{
	if (bCastShadows == bNewCastShadows)
	{
		return;
	}
	bCastShadows = bNewCastShadows;
	if (AActor* Owner = GetOwner())
	{
		if (ULevel* Level = Owner->GetLevel())
		{
			Level->MarkSpatialDirty();
		}
	}
}

void ULightComponentBase::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	USceneComponent::DuplicateShallow(DuplicatedObject, Context);

	auto DuplicatedLightComponent =
			static_cast<ULightComponentBase*>(DuplicatedObject);

	DuplicatedLightComponent->bCastShadows = bCastShadows;
}

void ULightComponentBase::Serialize(FArchive& Ar)
{
	USceneComponent::Serialize(Ar);
	Ar.Serialize("bCastShadows", bCastShadows);
}
