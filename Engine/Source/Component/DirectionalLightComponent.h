#pragma once
#include "LightComponent.h"

enum class EDirectionalShadowProjectionMode : int32
{
	CSM = 0,
	PSM = 1,
};

class ENGINE_API UDirectionalLightComponent : public ULightComponent
{
public:
	DECLARE_RTTI(UDirectionalLightComponent, ULightComponent);
	void PostConstruct() override;

	EDirectionalShadowProjectionMode GetShadowProjectionMode() const { return ShadowProjectionMode; }
	int32 GetCascadeCount() const { return CascadeCount; }
	float GetShadowFarZ() const { return ShadowFarZ; }
	float GetSplitLambda() const { return SplitLambda; }
	float GetCascadeTransitionValue() const { return CascadeTransitionValue; }
	float GetCascadeBias(int32 Index) const { return CascadeBiases[Index]; }
	float GetCascadeSlopeBias(int32 Index) const { return CascadeSlopeBiases[Index]; }

	void SetShadowProjectionMode(EDirectionalShadowProjectionMode InProjectionMode);
	void SetCascadeCount(int32 InCascadeCount);
	void SetShadowFarZ(float InShadowFarZ);
	void SetSplitLambda(float InSplitLambda);
	void SetCascadeTransitionValue(float InCascadeTransitionValue);
	void SetCascadeBias(int32 Index, float InBias);
	void SetCascadeSlopeBias(int32 Index, float InBias);

	void Serialize(FArchive& Ar) override;

protected:
	void MarkTransformDirty() override;
	bool SupportsIntensityUnit(ELightUnits UnitType) const override;
	float ComputePhotometricScale() const override;

private:
	EDirectionalShadowProjectionMode ShadowProjectionMode = EDirectionalShadowProjectionMode::CSM;
	int32 CascadeCount = 0;
	float ShadowFarZ = 25.0f;
	float SplitLambda = 0.0f;
	float CascadeTransitionValue = 0.0f;
	float CascadeBiases[4] = { 0.00005f, 0.00005f, 0.00005f, 0.00005f };
	float CascadeSlopeBiases[4] = { 0.0001f, 0.0001f, 0.0001f, 0.0001f };
};
