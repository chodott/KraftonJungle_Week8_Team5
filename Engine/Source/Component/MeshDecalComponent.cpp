#include "Component/MeshDecalComponent.h"

#include <algorithm>

#include "Object/Class.h"
#include "Serializer/Archive.h"

IMPLEMENT_RTTI(UMeshDecalComponent, UDecalComponent)

void UMeshDecalComponent::PostConstruct()
{
	UDecalComponent::PostConstruct();
	SetBaseColorTint(FLinearColor::White);
	bDrawDebugBounds = true;
}

void UMeshDecalComponent::SetSurfaceOffset(float InSurfaceOffset)
{
	SurfaceOffset = (std::max)(0.0f, InSurfaceOffset);
}

void UMeshDecalComponent::Serialize(FArchive& Ar)
{
	UDecalComponent::Serialize(Ar);
	Ar.Serialize("SurfaceOffset", SurfaceOffset);

	if (Ar.IsLoading())
	{
		SurfaceOffset = (std::max)(0.0f, SurfaceOffset);
	}
}

void UMeshDecalComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UDecalComponent::DuplicateShallow(DuplicatedObject, Context);

	UMeshDecalComponent* DuplicatedComponent = static_cast<UMeshDecalComponent*>(DuplicatedObject);
	DuplicatedComponent->SurfaceOffset = SurfaceOffset;
}
