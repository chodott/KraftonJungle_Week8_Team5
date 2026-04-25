#include "LightComponent.h"

#include <algorithm>

#include "Actor/Actor.h"
#include "Math/MathUtility.h"
#include "Object/Class.h"
#include "Renderer/Features/Shadow/ShadowTypes.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(ULightComponent, ULightComponentBase)

void ULightComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	ULightComponentBase::DuplicateShallow(DuplicatedObject, Context);

	auto DuplicatedLightComponent                   = static_cast<ULightComponent*>(DuplicatedObject);
	DuplicatedLightComponent->LightColor            = LightColor;
	DuplicatedLightComponent->Intensity             = Intensity;
	DuplicatedLightComponent->IntensityUnits        = IntensityUnits;
	DuplicatedLightComponent->bVisible              = bVisible;
	DuplicatedLightComponent->ShadowMapResolution = ShadowMapResolution;
	DuplicatedLightComponent->ShadowBias            = ShadowBias;
	DuplicatedLightComponent->ShadowSlopeBias       = ShadowSlopeBias;
	DuplicatedLightComponent->ShadowSharpen         = ShadowSharpen;
}

void ULightComponent::SetIntensity(float NewIntensity)
{
	const float ClampedIntensity = (std::max)(0.0f, NewIntensity);
	if (Intensity == ClampedIntensity)
	{
		return;
	}

	Intensity = ClampedIntensity;
	NotifyOwnerLightPropertyChanged();
}

void ULightComponent::SetIntensityUnits(ELightUnits NewUnit)
{
	if (!SupportsIntensityUnit(NewUnit))
	{
		return;
	}

	if (IntensityUnits == NewUnit)
	{
		return;
	}

	const float OldScale           = ComputePhotometricScale();
	const float EffectiveIntensity = Intensity * OldScale;

	IntensityUnits       = NewUnit;
	const float NewScale = ComputePhotometricScale();
	if (NewScale > 0.0f)
	{
		Intensity = (std::max)(0.0f, EffectiveIntensity / NewScale);
	}
	NotifyOwnerLightPropertyChanged();
}

void ULightComponent::SetColor(FLinearColor NewColor)
{
	NewColor.A = 1.0f;

	if (LightColor.R == NewColor.R &&
		LightColor.G == NewColor.G &&
		LightColor.B == NewColor.B &&
		LightColor.A == NewColor.A)
	{
		return;
	}

	LightColor = NewColor;
	NotifyOwnerLightPropertyChanged();
}

void ULightComponent::SetVisible(bool bNewVisible)
{
	if (bVisible == bNewVisible)
	{
		return;
	}

	bVisible = bNewVisible;
	NotifyOwnerLightPropertyChanged();
}

void ULightComponent::SetShadowMapResolution(uint32 NewResolution)
{
	if (ShadowMapResolution == NewResolution)
	{
		return;
	}
	ShadowMapResolution = NewResolution;
	NotifyOwnerLightPropertyChanged();
}

void ULightComponent::SetShadowBias(float NewBias)
{
	if (ShadowBias == NewBias)
	{
		return;
	}
	ShadowBias = NewBias;
	NotifyOwnerLightPropertyChanged();
}

void ULightComponent::SetShadowSlopeBias(float NewSlopeBias)
{
	if (ShadowSlopeBias == NewSlopeBias)
	{
		return;
	}
	ShadowSlopeBias = NewSlopeBias;
	NotifyOwnerLightPropertyChanged();
}

void ULightComponent::SetShadowSharpen(float NewSharpen)
{
	if (ShadowSharpen == NewSharpen)
	{
		return;
	}
	ShadowSharpen = NewSharpen;
	NotifyOwnerLightPropertyChanged();
}

bool ULightComponent::SupportsIntensityUnit(ELightUnits UnitType) const
{
	switch (UnitType)
	{
	case ELightUnits::Unitless:
	case ELightUnits::Candelas:
	case ELightUnits::Lumens:
	case ELightUnits::Lux:
		return true;
	default:
		return false;
	}
}

void ULightComponent::Serialize(FArchive& Ar)
{
	FVector4 Color = LightColor.ToVector4();
	uint32   Units = (uint32)IntensityUnits;

	ULightComponentBase::Serialize(Ar);
	Ar.Serialize("Intensity", Intensity);
	Ar.Serialize("LightColor", Color);
	Ar.Serialize("IntensityUnits", Units);
	Ar.Serialize("bVisible", bVisible);
	Ar.Serialize("ShadowMapResolution", ShadowMapResolution);
	Ar.Serialize("ShadowBias", ShadowBias);
	Ar.Serialize("ShadowSlopeBias", ShadowSlopeBias);
	Ar.Serialize("ShadowSharpen", ShadowSharpen);
	if (Ar.IsLoading())
	{
		Intensity      = (std::max)(0.0f, Intensity);
		LightColor     = FLinearColor(Color.X, Color.Y, Color.Z, Color.W);
		IntensityUnits = static_cast<ELightUnits>(Units);

		ShadowMapResolution = FMath::Clamp(ShadowMapResolution, 0u, ShadowConfig::MaxShadowMapResolution);
		ShadowBias          = (std::max)(0.0f, ShadowBias);
		ShadowSlopeBias     = (std::max)(0.0f, ShadowSlopeBias);
		ShadowSharpen       = FMath::Clamp(ShadowSharpen, 0.0f, 1.0f);
	}
}

float ULightComponent::ComputePhotometricScale() const
{
	switch (IntensityUnits)
	{
	case ELightUnits::Unitless:
		return 1.0f / 625.0f;
	case ELightUnits::Candelas:
	case ELightUnits::Lumens:
	case ELightUnits::Lux:
	default:
		return 1.0f;
	}
}

void ULightComponent::NotifyOwnerLightPropertyChanged()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->OnOwnedComponentPropertyChanged(this);
	}
}
