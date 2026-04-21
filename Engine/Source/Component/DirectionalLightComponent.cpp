#include "DirectionalLightComponent.h"

#include "Object/Class.h"

IMPLEMENT_RTTI(UDirectionalLightComponent, ULightComponent)

void UDirectionalLightComponent::PostConstruct()
{
	ULightComponent::PostConstruct();
	IntensityUnits = ELightUnits::Lux;
	
	Intensity = 2.0f;
}

void UDirectionalLightComponent::MarkTransformDirty()
{
	ULightComponent::MarkTransformDirty();
}

bool UDirectionalLightComponent::SupportsIntensityUnit(ELightUnits UnitType) const
{
	return UnitType == ELightUnits::Lux;
}

float UDirectionalLightComponent::ComputePhotometricScale() const
{
	return 1.0f;
}
