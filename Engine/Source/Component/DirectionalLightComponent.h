#pragma once
#include "LightComponent.h"

class ENGINE_API UDirectionalLightComponent : public ULightComponent
{
public:
	DECLARE_RTTI(UDirectionalLightComponent, ULightComponent);
	void PostConstruct() override;

protected:
	void MarkTransformDirty() override;
	bool SupportsIntensityUnit(ELightUnits UnitType) const override;
	float ComputePhotometricScale() const override;
};
