#pragma once
#include "PointLightComponent.h"

class ENGINE_API USpotLightComponent : public UPointLightComponent
{
public:
	DECLARE_RTTI(USpotLightComponent, UPointLightComponent);
	
	void PostConstruct() override;

	void SetInnerConeAngle(float innerConeAngle);
	void SetOuterConeAngle(float outerConeAngle);

	float GetInnerConeAngle() const { return InnerConeAngle; }
	float GetOuterConeAngle() const { return OuterConeAngle; }

	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

protected:
	void MarkTransformDirty() override;
	float ComputePhotometricScale() const override;

private:
	float InnerConeAngle = 0.0f;
	float OuterConeAngle = 44.0f;
};
