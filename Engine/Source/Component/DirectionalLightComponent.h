#pragma once
#include "LightComponent.h"

class ENGINE_API UDirectionalLightComponent : public ULightComponent
{
public:
	DECLARE_RTTI(UDirectionalLightComponent, ULightComponent);
	void PostConstruct() override;

	int32 GetCascadeCount() const { return CascadeCount; }
	float GetShadowFarZ() const { return ShadowFarZ; }
	float GetSplitLambda() const { return SplitLambda; }
	float GetCascadeTransitionValue() const { return CascadeTransitionValue; }

	void GetCascadeCount(int32 InCascadeCount) { CascadeCount = InCascadeCount; }
	void GetShadowFarZ(float InShadowFarZ) { ShadowFarZ = InShadowFarZ; }
	void GetSplitLambda(float InSplitLambda) { SplitLambda = InSplitLambda; }
	void GetCascadeTransitionValue(float InCascadeTransitionValue) { CascadeTransitionValue = InCascadeTransitionValue; }

protected:
	void MarkTransformDirty() override;
	bool SupportsIntensityUnit(ELightUnits UnitType) const override;
	float ComputePhotometricScale() const override;

private:
	int32 CascadeCount = 0;
	float ShadowFarZ = 0.0f;
	float SplitLambda = 0.0f;
	float CascadeTransitionValue = 0.0f;
};