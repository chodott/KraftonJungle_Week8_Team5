#include "DecalComponent.h"

#include <algorithm>

#include "Core/Paths.h"
#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UDecalComponent, UPrimitiveComponent)

void UDecalComponent::PostConstruct()
{
	bDrawDebugBounds = false;
	UpdateBounds();
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
	Ar.Serialize("TextureIndex", TextureIndex);
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
	DuplicatedDecalComponent->TextureIndex = TextureIndex;
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

