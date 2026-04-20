#pragma once
#include "LightComponent.h"

class ENGINE_API UAmbientLightComponent : public ULightComponent
{
public:
	DECLARE_RTTI(UAmbientLightComponent, ULightComponent);
	void PostConstruct() override;

protected:
	void MarkTransformDirty() override;
	bool SupportsIntensityUnit(ELightUnits UnitType) const override;
	float ComputePhotometricScale() const override;
};
