#include "Component/FireBallComponent.h"

#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UFireBallComponent, UPrimitiveComponent)

void UFireBallComponent::Serialize(FArchive& Ar)
{
	UPrimitiveComponent::Serialize(Ar);

	FVector4 FireColor = Color.ToVector4();

	Ar.Serialize("Color", FireColor);
	Ar.Serialize("Intensity", Intensity);
	Ar.Serialize("Radius", Radius);
	Ar.Serialize("RadiusFallOff", RadiusFallOff);

	if (Ar.IsLoading())
	{
		Intensity = (std::max)(0.0f, Intensity);
		Radius = (std::max)(0.0f, Radius);
		RadiusFallOff = (std::max)(0.0f, RadiusFallOff);
		Color = FLinearColor(FireColor.X, FireColor.Y, FireColor.Z, FireColor.W);
	}
}

void UFireBallComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	UFireBallComponent* DuplicatedFogComponent = static_cast<UFireBallComponent*>(DuplicatedObject);
	DuplicatedFogComponent->Intensity = Intensity;
	DuplicatedFogComponent->Radius = Radius;
	DuplicatedFogComponent->RadiusFallOff = RadiusFallOff;
	DuplicatedFogComponent->Color = Color;
}