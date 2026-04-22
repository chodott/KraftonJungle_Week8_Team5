#include "Math/LinearColor.h"

#include <algorithm>
#include <cmath>

float FLinearColor::SRGBToLinearChannel(float Color)
{
	const float Clamped = std::clamp(Color, 0.0f, 1.0f);
	return (Clamped <= 0.04045f)
		? (Clamped / 12.92f)
		: std::pow((Clamped + 0.055f) / 1.055f, 2.4f);
}

float FLinearColor::LinearToSRGBChannel(float Color)
{
	const float Clamped = (std::max)(Color, 0.0f);
	return (Clamped <= 0.0031308f)
		? (12.92f * Clamped)
		: (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
}

FLinearColor FLinearColor::FromSRGB(float InR, float InG, float InB, float InA)
{
	return FLinearColor(
		SRGBToLinearChannel(InR),
		SRGBToLinearChannel(InG),
		SRGBToLinearChannel(InB),
		InA);
}

FLinearColor FLinearColor::FromSRGB(const FVector4& Color)
{
	return FromSRGB(Color.X, Color.Y, Color.Z, Color.W);
}

FVector4 FLinearColor::ToSRGBVector4() const
{
	return FVector4(
		LinearToSRGBChannel(R),
		LinearToSRGBChannel(G),
		LinearToSRGBChannel(B),
		A);
}

const FLinearColor FLinearColor::White = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
const FLinearColor FLinearColor::Black = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
