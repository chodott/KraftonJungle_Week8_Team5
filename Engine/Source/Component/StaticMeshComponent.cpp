#include "StaticMeshComponent.h"

#include <algorithm>
#include <filesystem>

#include "Object/Class.h"
#include "Component/PrimitiveComponent.h"
#include "Component/MeshComponent.h"
#include "Asset/ObjManager.h"
#include "Core/Paths.h"
#include "Debug/EngineLog.h"
#include "Renderer/Resources/Material/Material.h"
IMPLEMENT_RTTI(UStaticMeshComponent, UMeshComponent)

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
	StaticMesh = InStaticMesh;

	if (StaticMesh)
	{
		int32 NeededMaterialSlots = StaticMesh->GetNumSections();
		Materials.resize(NeededMaterialSlots, nullptr);
		const TArray<std::shared_ptr<FMaterial>>& DefaultMats = StaticMesh->GetDefaultMaterials();
		for (int32 i = 0; i < NeededMaterialSlots && i < DefaultMats.size(); ++i)
		{
			Materials[i] = DefaultMats[i]->CreateDynamicMaterial();
		}
		UpdateBounds();
	}
	else
	{
		Materials.clear();
	}
}

void UStaticMeshComponent::SetLODEnabled(bool bEnabled)
{
	LODSettings.bEnabled = bEnabled;
}

bool UStaticMeshComponent::IsLODEnabled() const
{
	return LODSettings.bEnabled;
}

void UStaticMeshComponent::SetLODScreenSizeScale(float InScale)
{
	LODSettings.ScreenSizeScale = (std::max)(InScale, 0.01f);
}

float UStaticMeshComponent::GetLODScreenSizeScale() const
{
	return LODSettings.ScreenSizeScale;
}

void UStaticMeshComponent::SetLODScreenSizeBias(float InBias)
{
	LODSettings.ScreenSizeBias = InBias;
}

float UStaticMeshComponent::GetLODScreenSizeBias() const
{
	return LODSettings.ScreenSizeBias;
}

const FStaticMeshComponentLODSettings& UStaticMeshComponent::GetLODSettings() const
{
	return LODSettings;
}

void UStaticMeshComponent::SetLODSettings(const FStaticMeshComponentLODSettings& InSettings)
{
	LODSettings = InSettings;
	LODSettings.ScreenSizeScale = (std::max)(LODSettings.ScreenSizeScale, 0.01f);
}

FRenderMesh* UStaticMeshComponent::GetRenderMesh() const
{
	return StaticMesh ? StaticMesh->GetRenderData() : nullptr;
}

FRenderMesh* UStaticMeshComponent::GetRenderMesh(const FRenderMeshSelectionContext& SelectionContext) const
{
	if (!StaticMesh)
	{
		return nullptr;
	}

	if (!LODSettings.bEnabled)
	{
		return StaticMesh->GetRenderData();
	}

	FStaticMeshLODSelectionContext LODSelectionContext;
	LODSelectionContext.ScreenSize = SelectionContext.ScreenSize;
	LODSelectionContext.ThresholdScale = LODSettings.ScreenSizeScale;
	LODSelectionContext.ThresholdBias = LODSettings.ScreenSizeBias;
	return StaticMesh->GetRenderDataForScreenSize(LODSelectionContext);
}

FRenderMesh* UStaticMeshComponent::GetRenderMesh(const float& Distance) const
{
	FRenderMeshSelectionContext SelectionContext;
	SelectionContext.Distance = Distance;
	return GetRenderMesh(SelectionContext);
}

void UStaticMeshComponent::DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const
{
	UPrimitiveComponent::DuplicateShallow(DuplicatedObject, Context);

	UStaticMeshComponent* DuplicatedStaticMeshComponent = static_cast<UStaticMeshComponent*>(DuplicatedObject);
	DuplicatedStaticMeshComponent->SetStaticMesh(StaticMesh);
	DuplicatedStaticMeshComponent->LODSettings = LODSettings;
	DuplicateMaterialsTo(DuplicatedStaticMeshComponent);
}

FBoxSphereBounds UStaticMeshComponent::GetLocalBounds() const
{
	if (StaticMesh)
	{
		return StaticMesh->LocalBounds;
	}
	return UPrimitiveComponent::GetLocalBounds();
}

bool UStaticMeshComponent::IntersectLocalRay(const FVector& LocalOrigin, const FVector& LocalDir, float& InOutDist) const
{
	return StaticMesh && StaticMesh->IntersectLocalRay(LocalOrigin, LocalDir, InOutDist);
}

FBoxSphereBounds UStaticMeshComponent::CalcBounds(const FMatrix& LocalToWorld) const
{
	return UPrimitiveComponent::CalcBounds(LocalToWorld);
}

void UStaticMeshComponent::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		UMeshComponent::Serialize(Ar);

		FString MeshFileName = "";
		if (StaticMesh)
		{
			MeshFileName = FPaths::ToRelativePath(StaticMesh->GetAssetPathFileName());
		}

		Ar.Serialize("ObjStaticMeshAsset", MeshFileName);
		Ar.Serialize("EnableLOD", LODSettings.bEnabled);
		Ar.Serialize("LODScreenSizeScale", LODSettings.ScreenSizeScale);
		Ar.Serialize("LODScreenSizeBias", LODSettings.ScreenSizeBias);
	}
	else
	{
		if (Ar.Contains("EnableLOD"))
		{
			Ar.Serialize("EnableLOD", LODSettings.bEnabled);
		}

		if (Ar.Contains("LODScreenSizeScale"))
		{
			Ar.Serialize("LODScreenSizeScale", LODSettings.ScreenSizeScale);
		}

		if (Ar.Contains("LODScreenSizeBias"))
		{
			Ar.Serialize("LODScreenSizeBias", LODSettings.ScreenSizeBias);
		}

		if (Ar.Contains("ObjStaticMeshAsset"))
		{
			FString MeshFileName;
			Ar.Serialize("ObjStaticMeshAsset", MeshFileName);

			if (!MeshFileName.empty())
			{
				UStaticMesh* LoadedMesh = FObjManager::LoadStaticMeshAsset(MeshFileName);
				SetStaticMesh(LoadedMesh);
			}
		}

		UMeshComponent::Serialize(Ar);
	}

	LODSettings.ScreenSizeScale = (std::max)(LODSettings.ScreenSizeScale, 0.01f);
}
