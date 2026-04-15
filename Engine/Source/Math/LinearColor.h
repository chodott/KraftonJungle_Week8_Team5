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

	FLinearColor(float InR, float InG, float InB, float InA) :
		R(InR), G(InG), B(InB), A(InA)
	{
	}

	FVector4 ToVector4() const
	{
		return FVector4(R, G, B, A);
	}

	static const FLinearColor White;
	static const FLinearColor Black;
};
