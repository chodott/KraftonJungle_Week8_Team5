#pragma once
#include "UPointLightComponent.h"

class ENGINE_API USpotLightComponent : public UPointLightComponent
{
public:
	DECLARE_RTTI(USpotLightComponent, UPointLightComponent);

	void SetInnerConeAngle(float innerConeAngle);
	void SetOuterConeAngle(float outerConeAngle);

	float GetInnerConeAngle() const { return InnerConeAngle; }
	float GetOuterConeAngle() const { return OuterConeAngle; }

protected:
	void MarkTransformDirty() override;
	float ComputePhotometricScale() const override;

private:
	float InnerConeAngle = 0.0f;
	float OuterConeAngle = 44.0f;
};
