#include "Component/LocalHeightFogComponent.h"

#include <algorithm>

#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(ULocalHeightFogComponent, UPrimitiveComponent)

void ULocalHeightFogComponent::PostConstruct()
{
	bDrawDebugBounds = true;
}

FBoxSphereBounds ULocalHeightFogComponent::GetLocalBounds() const
{
	return { FVector::ZeroVector, FogExtents.Size(), FogExtents };
}

void ULocalHeightFogComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FVector4 FogColor = FogInscatteringColor.ToVector4();

	Ar.Serialize("FogDensity", FogDensity);
	Ar.Serialize("FogHeightFalloff", FogHeightFalloff);
	Ar.Serialize("StartDistance", StartDistance);
	Ar.Serialize("FogCutoffDistance", FogCutoffDistance);
	Ar.Serialize("FogMaxOpacity", FogMaxOpacity);
	Ar.Serialize("FogInscatteringColor", FogColor);
	Ar.Serialize("FogAllowBackground", AllowBackground);
	Ar.Serialize("FogExtents", FogExtents);

	if (Ar.IsLoading())
	{
		FogDensity = (std::max)(0.0f, FogDensity);
		FogHeightFalloff = (std::max)(0.0f, FogHeightFalloff);
		StartDistance = (std::max)(0.0f, StartDistance);
		FogCutoffDistance = (std::max)(0.0f, FogCutoffDistance);
		FogMaxOpacity = std::clamp(FogMaxOpacity, 0.0f, 1.0f);
		FogExtents.X = (std::max)(0.0f, FogExtents.X);
		FogExtents.Y = (std::max)(0.0f, FogExtents.Y);
		FogExtents.Z = (std::max)(0.0f, FogExtents.Z);
		FogInscatteringColor = FLinearColor(FogColor.X, FogColor.Y, FogColor.Z, FogColor.W);
	}
}

void ULocalHeightFogComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	ULocalHeightFogComponent* DuplicatedFogComponent = static_cast<ULocalHeightFogComponent*>(DuplicatedObject);
	DuplicatedFogComponent->FogDensity = FogDensity;
	DuplicatedFogComponent->FogHeightFalloff = FogHeightFalloff;
	DuplicatedFogComponent->StartDistance = StartDistance;
	DuplicatedFogComponent->FogCutoffDistance = FogCutoffDistance;
	DuplicatedFogComponent->FogMaxOpacity = FogMaxOpacity;
	DuplicatedFogComponent->FogInscatteringColor = FogInscatteringColor;
	DuplicatedFogComponent->AllowBackground = AllowBackground;
	DuplicatedFogComponent->FogExtents = FogExtents;
}
