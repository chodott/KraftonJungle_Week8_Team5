#include "DecalComponent.h"

#include <algorithm>

#include "Core/Paths.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UDecalComponent, UPrimitiveComponent)

void UDecalComponent::PostConstruct()
{
	bCanEverTick = true;
	bTickInEditor = true;
	bDrawDebugBounds = true;
	UpdateBounds();
	FadeIn(FadeInDuration);
}

void UDecalComponent::Tick(float DeltaTime)
{
	UPrimitiveComponent::Tick(DeltaTime);

	if (FadeState == EDecalFadeState::None)
	{
		return;
	}

	FadeElapsed += DeltaTime;
	const float T = (FadeDuration > 0.0f)
		? std::clamp(FadeElapsed / FadeDuration, 0.0f, 1.0f)
		: 1.0f;

	if (FadeState == EDecalFadeState::FadeIn)
	{
		BaseColorTint.A = T;
		if (T >= 1.0f)
		{
			FadeState = EDecalFadeState::None;
		}
	}
	else // FadeOut
	{
		BaseColorTint.A = 1.0f - T;
		if (T >= 1.0f)
		{
			FadeState = EDecalFadeState::None;
			if (bDisableOnFadeOutComplete)
			{
				bEnabled = false;
			}
		}
	}
}

void UDecalComponent::FadeIn(float Duration)
{
	bEnabled = true;
	FadeState = EDecalFadeState::FadeIn;
	FadeDuration = Duration;
	FadeElapsed = 0.0f;
	BaseColorTint.A = 0.0f;
}

void UDecalComponent::FadeOut(float Duration, bool bDisableOnComplete)
{
	FadeState = EDecalFadeState::FadeOut;
	FadeDuration = Duration;
	FadeElapsed = 0.0f;
	bDisableOnFadeOutComplete = bDisableOnComplete;
	BaseColorTint.A = 1.0f;
}

FBoxSphereBounds UDecalComponent::GetLocalBounds() const
{
	const FVector Extents = GetExtents();
	return { FVector::ZeroVector, Extents.Size(), Extents };
}

void UDecalComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FString TexturePathString;
	if (!TexturePath.empty())
	{
		TexturePathString = FPaths::ToRelativePath(FPaths::FromWide(TexturePath));
	}

	FVector4 BaseColorTintVector = BaseColorTint.ToVector4();

	Ar.Serialize("Enabled", bEnabled);
	Ar.Serialize("Size", Size);
	Ar.Serialize("ProjectionDepth", ProjectionDepth);
	Ar.Serialize("UVMin", UVMin);
	Ar.Serialize("UVMax", UVMax);
	Ar.Serialize("TexturePath", TexturePathString);
	Ar.Serialize("RenderFlags", RenderFlags);
	Ar.Serialize("Priority", Priority);
	Ar.Serialize("ReceiverLayerMask", ReceiverLayerMask);
	Ar.Serialize("BaseColorTint", BaseColorTintVector);
	Ar.Serialize("NormalBlend", NormalBlend);
	Ar.Serialize("RoughnessBlend", RoughnessBlend);
	Ar.Serialize("EmissiveBlend", EmissiveBlend);
	Ar.Serialize("EdgeFade", EdgeFade);

	if (Ar.IsLoading())
	{
		Size.X = (std::max)(0.0f, Size.X);
		Size.Y = (std::max)(0.0f, Size.Y);
		ProjectionDepth = (std::max)(0.0f, ProjectionDepth);

		if (UVMax.X < UVMin.X)
		{
			std::swap(UVMin.X, UVMax.X);
		}
		if (UVMax.Y < UVMin.Y)
		{
			std::swap(UVMin.Y, UVMax.Y);
		}

		TexturePath = TexturePathString.empty()
			? std::wstring()
			: FPaths::ToWide(FPaths::ToAbsolutePath(TexturePathString));

		TextureIndex = 0;

		BaseColorTint = FLinearColor(
			BaseColorTintVector.X,
			BaseColorTintVector.Y,
			BaseColorTintVector.Z,
			BaseColorTintVector.W);
		NormalBlend = std::clamp(NormalBlend, 0.0f, 1.0f);
		RoughnessBlend = std::clamp(RoughnessBlend, 0.0f, 1.0f);
		EmissiveBlend = std::clamp(EmissiveBlend, 0.0f, 1.0f);
		EdgeFade = (std::max)(0.0f, EdgeFade);

		UpdateBounds();
	}
}

void UDecalComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	UDecalComponent* DuplicatedDecalComponent = static_cast<UDecalComponent*>(DuplicatedObject);
	DuplicatedDecalComponent->bEnabled = bEnabled;
	DuplicatedDecalComponent->Size = Size;
	DuplicatedDecalComponent->ProjectionDepth = ProjectionDepth;
	DuplicatedDecalComponent->UVMin = UVMin;
	DuplicatedDecalComponent->UVMax = UVMax;
	DuplicatedDecalComponent->TexturePath = TexturePath;
	DuplicatedDecalComponent->TextureIndex = 0;
	DuplicatedDecalComponent->RenderFlags = RenderFlags;
	DuplicatedDecalComponent->Priority = Priority;
	DuplicatedDecalComponent->ReceiverLayerMask = ReceiverLayerMask;
	DuplicatedDecalComponent->BaseColorTint = BaseColorTint;
	DuplicatedDecalComponent->NormalBlend = NormalBlend;
	DuplicatedDecalComponent->RoughnessBlend = RoughnessBlend;
	DuplicatedDecalComponent->EmissiveBlend = EmissiveBlend;
	DuplicatedDecalComponent->EdgeFade = EdgeFade;
	DuplicatedDecalComponent->UpdateBounds();
}

