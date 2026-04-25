#pragma once
#include "LightComponentBase.h"
#include "Math/LinearColor.h"

enum class ELightUnits : uint8
{
	Unitless,
	Candelas,
	Lumens,
	Lux,
};

class ENGINE_API ULightComponent : public ULightComponentBase
{
public:
	DECLARE_RTTI(ULightComponent, ULightComponentBase);

	void SetIntensity(float NewIntensity);
	void SetIntensityUnits(ELightUnits NewUnit);
	void SetColor(FLinearColor NewColor);
	void SetVisible(bool bNewVisible);

	void SetShadowResolutionScale(float NewScale);
	void SetShadowBias(float NewBias);
	void SetShadowSlopeBias(float NewSlopeBias);
	void SetShadowSharpen(float NewSharpen);


	float GetIntensity() const
	{
		return Intensity;
	}

	ELightUnits GetIntensityUnits() const
	{
		return IntensityUnits;
	}

	float GetEffectiveIntensity() const
	{
		return Intensity * ComputePhotometricScale();
	}

	FLinearColor GetColor() const
	{
		return LightColor;
	}

	bool GetVisible() const
	{
		return bVisible;
	}

	float GetShadowResolutionScale() const
	{
		return ShadowResoultionScale;
	}

	float GetShadowBias() const
	{
		return ShadowBias;
	}

	float GetShadowSlopeBias() const
	{
		return ShadowSlopeBias;
	}

	float GetShadowSharpen() const
	{
		return ShadowSharpen;
	}

	void         DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	virtual bool SupportsIntensityUnit(ELightUnits UnitType) const;
	void         Serialize(FArchive& Ar) override;

protected:
	void          NotifyOwnerLightPropertyChanged();
	virtual float ComputePhotometricScale() const;

protected:
	FLinearColor LightColor     = FLinearColor::White;
	float        Intensity      = 0.0f;
	ELightUnits  IntensityUnits = ELightUnits::Unitless;
	bool         bVisible       = true;

	//Shadow
	float ShadowResoultionScale = 1.0f;
	float ShadowBias = 0.0f;
	float ShadowSlopeBias = 0.0f;
	float ShadowSharpen = 0.0f;
};
