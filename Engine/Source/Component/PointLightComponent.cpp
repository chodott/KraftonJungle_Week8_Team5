#include "PointLightComponent.h"

#include <algorithm>

#include "Math/MathUtility.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(UPointLightComponent, ULightComponent)

void UPointLightComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	ULightComponent::DuplicateShallow(DuplicatedObject, Context);

	UPointLightComponent* DuplicatedPointLightComponent = static_cast<UPointLightComponent*>(DuplicatedObject);
	DuplicatedPointLightComponent->AttenuationRadius = AttenuationRadius;
	DuplicatedPointLightComponent->LightFalloffExponent = LightFalloffExponent;
}

void UPointLightComponent::PostConstruct()
{
	ULightComponent::PostConstruct();
	IntensityUnits = ELightUnits::Candelas;
	
	Intensity = 7.f;
}

void UPointLightComponent::SetAttenuationRadius(float radius)
{
	const float NewRadius = (std::max)(0.0f, radius);
	if (AttenuationRadius == NewRadius)
	{
		return;
	}

	AttenuationRadius = NewRadius;
	NotifyOwnerLightPropertyChanged();
}

void UPointLightComponent::SetLightFalloffExponent(float exponent)
{
	const float NewExponent = (std::max)(0.0f, exponent);
	if (LightFalloffExponent == NewExponent)
	{
		return;
	}

	LightFalloffExponent = NewExponent;
	NotifyOwnerLightPropertyChanged();
}

void UPointLightComponent::MarkTransformDirty()
{
	ULightComponent::MarkTransformDirty();
}

float UPointLightComponent::ComputePhotometricScale() const
{
	switch (IntensityUnits)
	{
	case ELightUnits::Candelas:
		return 1.0f;
	case ELightUnits::Lumens:
		return 1.0f / (4.0f * FMath::PI);
	case ELightUnits::Unitless:
		return 1.0f / 625.0f;
	case ELightUnits::Lux:
	default:
		return 1.0f;
	}
}

bool UPointLightComponent::SupportsIntensityUnit(ELightUnits UnitType) const
{
	return UnitType == ELightUnits::Candelas || UnitType == ELightUnits::Lumens || UnitType == ELightUnits::Unitless;
}
