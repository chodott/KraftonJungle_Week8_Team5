#include "LightComponent.h"

#include <algorithm>

#include "Actor/Actor.h"
#include "Object/Class.h"

IMPLEMENT_RTTI(ULightComponent, ULightComponentBase)

void ULightComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	ULightComponentBase::DuplicateShallow(DuplicatedObject, Context);

	ULightComponent* DuplicatedLightComponent = static_cast<ULightComponent*>(DuplicatedObject);
	DuplicatedLightComponent->LightColor = LightColor;
	DuplicatedLightComponent->Intensity = Intensity;
	DuplicatedLightComponent->IntensityUnits = IntensityUnits;
	DuplicatedLightComponent->bVisible = bVisible;
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

	const float OldScale = ComputePhotometricScale();
	const float EffectiveIntensity = Intensity * OldScale;

	IntensityUnits = NewUnit;
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
