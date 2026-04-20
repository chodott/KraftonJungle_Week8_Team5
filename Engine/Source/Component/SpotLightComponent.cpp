#include "SpotLightComponent.h"

#include <cmath>

#include "Math/MathUtility.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(USpotLightComponent, UPointLightComponent)

void USpotLightComponent::PostConstruct()
{
	UPointLightComponent::PostConstruct();
	
	Intensity = 8.f;
}

void USpotLightComponent::SetInnerConeAngle(float innerConeAngle)
{
	const float ClampedInnerConeAngle = FMath::Clamp(innerConeAngle, 0.0f, 179.0f);
	const float NewInnerConeAngle = FMath::Clamp(ClampedInnerConeAngle, 0.0f, OuterConeAngle);
	if (InnerConeAngle == NewInnerConeAngle)
	{
		return;
	}

	InnerConeAngle = NewInnerConeAngle;
	NotifyOwnerLightPropertyChanged();
}

void USpotLightComponent::SetOuterConeAngle(float outerConeAngle)
{
	const float NewOuterConeAngle = FMath::Clamp(outerConeAngle, 0.0f, 80.0f);
	const float NewInnerConeAngle = FMath::Clamp(InnerConeAngle, 0.0f, NewOuterConeAngle);
	const bool bOuterChanged = (OuterConeAngle != NewOuterConeAngle);
	const bool bInnerChanged = (InnerConeAngle != NewInnerConeAngle);

	if (!bOuterChanged && !bInnerChanged)
	{
		return;
	}

	OuterConeAngle = NewOuterConeAngle;
	InnerConeAngle = NewInnerConeAngle;
	NotifyOwnerLightPropertyChanged();
}

void USpotLightComponent::MarkTransformDirty()
{
	UPointLightComponent::MarkTransformDirty();
}

float USpotLightComponent::ComputePhotometricScale() const
{
	switch (IntensityUnits)
	{
	case ELightUnits::Candelas:
		return 1.0f;
	case ELightUnits::Lumens:
	{
		const float ClampedOuterAngle = FMath::Clamp(OuterConeAngle, 0.0f, 179.0f);
		const float HalfAngleRad = FMath::DegreesToRadians(ClampedOuterAngle * 0.5f);
		const float SolidAngle = FMath::TwoPi * (1.0f - std::cos(HalfAngleRad));
		return SolidAngle > FMath::SmallNumber ? (1.0f / SolidAngle) : 1.0f;
	}
	case ELightUnits::Unitless:
		return 1.0f / 625.0f;
	case ELightUnits::Lux:
	default:
		return 1.0f;
	}
}
