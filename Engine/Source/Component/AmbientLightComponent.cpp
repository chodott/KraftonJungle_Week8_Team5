#include "AmbientLightComponent.h"

#include "Object/Class.h"

IMPLEMENT_RTTI(UAmbientLightComponent, ULightComponent)

void UAmbientLightComponent::PostConstruct()
{
	ULightComponent::PostConstruct();
	IntensityUnits = ELightUnits::Unitless;
	
	Intensity = 1.0f;
}

void UAmbientLightComponent::MarkTransformDirty()
{
	ULightComponent::MarkTransformDirty();
}

bool UAmbientLightComponent::SupportsIntensityUnit(ELightUnits UnitType) const
{
	return UnitType == ELightUnits::Unitless;
}

float UAmbientLightComponent::ComputePhotometricScale() const
{
	return 1.0f;
}
