#pragma once
#include "LightComponent.h"

class ENGINE_API UPointLightComponent : public ULightComponent
{
public:
	DECLARE_RTTI(UPointLightComponent, ULightComponent);
	void PostConstruct() override;

	void SetAttenuationRadius(float radius);
	void SetLightFalloffExponent(float exponent);

	float GetAttenuationRadius() const { return AttenuationRadius; };
	float GetLightFalloffExponent() const { return LightFalloffExponent; };

protected:
	void MarkTransformDirty() override;
	float ComputePhotometricScale() const override;
	bool SupportsIntensityUnit(ELightUnits UnitType) const override;

private:
	float AttenuationRadius = 10.0f;
	float LightFalloffExponent = 0.f;
};
