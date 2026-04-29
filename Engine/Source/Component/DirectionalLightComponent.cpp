#include "DirectionalLightComponent.h"

#include "Math/MathUtility.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UDirectionalLightComponent, ULightComponent)

void UDirectionalLightComponent::PostConstruct()
{
	ULightComponent::PostConstruct();
	IntensityUnits = ELightUnits::Lux;
	
	Intensity = 2.0f;
	ShadowResolutionScale = 8.0f;

	ShadowProjectionMode = EDirectionalShadowProjectionMode::CSM;
	CascadeCount = 4;
	ShadowFarZ = 300.0f;
	SplitLambda = 0.9f;
	CascadeTransitionValue = 0.1f;
}

void UDirectionalLightComponent::SetShadowProjectionMode(EDirectionalShadowProjectionMode InProjectionMode)
{
	if (ShadowProjectionMode != InProjectionMode)
	{
		ShadowProjectionMode = InProjectionMode;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetCascadeCount(int32 InCascadeCount)
{
	const int32 ClampedCascadeCount = FMath::Clamp(InCascadeCount, 1, 4);
	if (CascadeCount != ClampedCascadeCount)
	{
		CascadeCount = ClampedCascadeCount;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetShadowFarZ(float InShadowFarZ)
{
	if (ShadowFarZ != InShadowFarZ)
	{
		ShadowFarZ = InShadowFarZ;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetSplitLambda(float InSplitLambda)
{
	if (SplitLambda != InSplitLambda)
	{
		SplitLambda = InSplitLambda;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetCascadeTransitionValue(float InCascadeTransitionValue)
{
	if (CascadeTransitionValue != InCascadeTransitionValue)
	{
		CascadeTransitionValue = InCascadeTransitionValue;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetCascadeBias(int32 Index, float InBias)
{
	if (Index >= 0 && Index < 4 && CascadeBiases[Index] != InBias)
	{
		CascadeBiases[Index] = InBias;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::SetCascadeSlopeBias(int32 Index, float InBias)
{
	if (Index >= 0 && Index < 4 && CascadeSlopeBiases[Index] != InBias)
	{
		CascadeSlopeBiases[Index] = InBias;
		NotifyOwnerLightPropertyChanged();
	}
}

void UDirectionalLightComponent::Serialize(FArchive& Ar)
{
	ULightComponent::Serialize(Ar);

	int32 ProjectionModeValue = static_cast<int32>(ShadowProjectionMode);
	Ar.Serialize("ShadowProjectionMode", ProjectionModeValue);

	Ar.Serialize("CascadeCount", CascadeCount);
	Ar.Serialize("ShadowFarZ", ShadowFarZ);
	Ar.Serialize("SplitLambda", SplitLambda);
	Ar.Serialize("CascadeTransitionValue", CascadeTransitionValue);

	for (int32 i = 0; i < 4; ++i)
	{
		Ar.Serialize(std::string("CascadeBias") + std::to_string(i), CascadeBiases[i]);
		Ar.Serialize(std::string("CascadeSlopeBias") + std::to_string(i), CascadeSlopeBiases[i]);
	}

	if (Ar.IsLoading())
	{
		ShadowProjectionMode = ProjectionModeValue == static_cast<int32>(EDirectionalShadowProjectionMode::PSM)
			? EDirectionalShadowProjectionMode::PSM
			: EDirectionalShadowProjectionMode::CSM;

		CascadeCount = FMath::Clamp(CascadeCount, 1, 4);

		// PSM에서는 ShadowFarZ <= 0 을 auto-far 의도로 쓸 수 있게 보존합니다.
		// CSM 경로에서만 안전값으로 보정합니다.
		if (ShadowProjectionMode == EDirectionalShadowProjectionMode::CSM)
		{
			ShadowFarZ = (std::max)(ShadowFarZ, 1.0f);
		}

		SplitLambda = FMath::Clamp(SplitLambda, 0.0f, 1.0f);
	}
}

void UDirectionalLightComponent::MarkTransformDirty()
{
	ULightComponent::MarkTransformDirty();
}

bool UDirectionalLightComponent::SupportsIntensityUnit(ELightUnits UnitType) const
{
	return UnitType == ELightUnits::Lux;
}

float UDirectionalLightComponent::ComputePhotometricScale() const
{
	return 1.0f;
}