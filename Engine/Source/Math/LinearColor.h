#pragma once

#include "CoreMinimal.h"

struct ENGINE_API FLinearColor
{
	float R = 0.0f;
	float G = 0.0f;
	float B = 0.0f;
	float A = 1.0f;

	FLinearColor() = default;

	explicit FLinearColor(const float* Color) :
		R(Color[0]), G(Color[1]), B(Color[2]), A(Color[3])
	{
	}

	explicit FLinearColor(const FVector4& Color) :
		R(Color.X), G(Color.Y), B(Color.Z), A(Color.W)
	{
	}

	FLinearColor(float InR, float InG, float InB, float InA) :
		R(InR), G(InG), B(InB), A(InA)
	{
	}

	static float SRGBToLinearChannel(float Color);
	static float LinearToSRGBChannel(float Color);

	static FLinearColor FromSRGB(float InR, float InG, float InB, float InA = 1.0f);
	static FLinearColor FromSRGB(const FVector4& Color);

	FVector4 ToVector4() const
	{
		return FVector4(R, G, B, A);
	}

	FVector4 ToSRGBVector4() const;

	static const FLinearColor White;
	static const FLinearColor Black;
};
