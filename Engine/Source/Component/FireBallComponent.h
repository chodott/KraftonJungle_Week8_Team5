#pragma once

#include "PrimitiveComponent.h"

#include "Renderer/LinearColor.h"

class FArchive;

class ENGINE_API UFireBallComponent : public UPrimitiveComponent
{
	DECLARE_RTTI(UFireBallComponent, UPrimitiveComponent)

	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

	float GetIntensity() const { return Intensity; }
	void SetIntensity(float NewIntensity) { Intensity = NewIntensity; }
	float GetRadius() const { return Radius; }
	void SetRadius(float NewRadius) { Radius = NewRadius; }
	FLinearColor GetColor() const { return Color; }
	void SetColor(FLinearColor NewColor) { Color = NewColor; }
	float GetRadiusFallOff() const { return RadiusFallOff; }
	void SetRadiusFallOff(float NewRadiusFallOff) { RadiusFallOff = NewRadiusFallOff; }
	
private:
	float Intensity = 1.0f;
	float Radius = 1.0f;
	FLinearColor Color = FLinearColor::White;
	float RadiusFallOff = 2.0f;
};
