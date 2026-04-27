#pragma once
#include "CoreMinimal.h"

struct FCasCadeMatrix
{
	FMatrix ViewMatrix;
	FMatrix ProjectionMatrix;
	FMatrix ViewProjMatrix;
};

class FCasCade
{
public:
	static TArray<float> CalculateCascadeSplits(int CascadeCount, float NearZ, float FarZ, float Lambda = 0.5f)
	{
		TArray<float> splits(CascadeCount + 1);

		splits[0] = NearZ;
		splits[CascadeCount] = FarZ;

		for (int i = 1; i < CascadeCount; i++)
		{
			// 선형 분할
			float fraction = static_cast<float>(i) / CascadeCount;
			float uniformSplit = NearZ + (FarZ - NearZ) * fraction;

			// 로그 분할
			float logSplit = NearZ * std::pow(FarZ / NearZ, fraction);

			// 실전 분할 - Lambda에 의해 변형 가능.
			splits[i] = Lambda * logSplit + (1.0f - Lambda) * uniformSplit;
		}

		return splits;
	}
};