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

int32 UStaticMeshComponent::GetAssetLodScreenSizeCount() const
{
	if (!StaticMesh)
	{
		return 0;
	}

	return (std::max)(static_cast<int32>(StaticMesh->GetLodCount()) - 1, 0);
}

void UStaticMeshComponent::SyncLODScreenSizesWithAsset()
{
	const int32 AssetLodCount = GetAssetLodScreenSizeCount();
	if (AssetLodCount <= 0 || !StaticMesh)
	{
		LODSettings.ScreenSizes.clear();
		return;
	}

	if (static_cast<int32>(LODSettings.ScreenSizes.size()) > AssetLodCount)
	{
		LODSettings.ScreenSizes.resize(static_cast<size_t>(AssetLodCount));
	}

	for (int32 LodIndex = static_cast<int32>(LODSettings.ScreenSizes.size()) + 1; LodIndex <= AssetLodCount; ++LodIndex)
	{
		LODSettings.ScreenSizes.push_back(StaticMesh->GetLodScreenSize(LodIndex));
	}
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
	StaticMesh = InStaticMesh;
	LODSettings.ScreenSizes.clear();

	if (StaticMesh)
	{
		int32 NeededMaterialSlots = StaticMesh->GetNumSections();
		Materials.resize(NeededMaterialSlots, nullptr);
		const TArray<std::shared_ptr<FMaterial>>& DefaultMats = StaticMesh->GetDefaultMaterials();
		for (int32 i = 0; i < NeededMaterialSlots && i < DefaultMats.size(); ++i)
		{
			Materials[i] = DefaultMats[i]->CreateDynamicMaterial();
		}

		SyncLODScreenSizesWithAsset();

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

void UStaticMeshComponent::SetLODScreenSize(int32 LODIndex, float ScreenSize)
{
	if (LODIndex <= 0)
	{
		return;
	}

	const int32 AssetLodCount = GetAssetLodScreenSizeCount();
	if (AssetLodCount > 0 && LODIndex > AssetLodCount)
	{
		return;
	}

	SyncLODScreenSizesWithAsset();

	const size_t Idx = static_cast<size_t>(LODIndex - 1);
	if (Idx >= LODSettings.ScreenSizes.size())
	{
		return;
	}
	LODSettings.ScreenSizes[Idx] = (std::max)(ScreenSize, 0.0f);
}

float UStaticMeshComponent::GetLODScreenSize(int32 LODIndex) const
{
	if (LODIndex <= 0)
	{
		return 0.0f;
	}
	const size_t Idx = static_cast<size_t>(LODIndex - 1);
	if (Idx >= LODSettings.ScreenSizes.size())
	{
		if (StaticMesh && LODIndex <= GetAssetLodScreenSizeCount())
		{
			return StaticMesh->GetLodScreenSize(LODIndex);
		}
		return 0.0f;
	}
	return LODSettings.ScreenSizes[Idx];
}

int32 UStaticMeshComponent::GetLODScreenSizeCount() const
{
	return (std::max)(static_cast<int32>(LODSettings.ScreenSizes.size()), GetAssetLodScreenSizeCount());
}

const FStaticMeshComponentLODSettings& UStaticMeshComponent::GetLODSettings() const
{
	return LODSettings;
}

void UStaticMeshComponent::SetLODSettings(const FStaticMeshComponentLODSettings& InSettings)
{
	LODSettings = InSettings;
	SyncLODScreenSizesWithAsset();
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
	LODSelectionContext.PerLODThresholds = LODSettings.ScreenSizes;
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
	DuplicatedStaticMeshComponent->SetLODSettings(LODSettings);
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
		int32 ScreenSizeCount = static_cast<int32>(LODSettings.ScreenSizes.size());
		Ar.Serialize("LODScreenSizeCount", ScreenSizeCount);
		for (int32 i = 0; i < ScreenSizeCount; ++i)
		{
			char Key[32];
			snprintf(Key, sizeof(Key), "LODScreenSize_%d", i);
			Ar.Serialize(Key, LODSettings.ScreenSizes[i]);
		}
	}
	else
	{
		if (Ar.Contains("EnableLOD"))
		{
			Ar.Serialize("EnableLOD", LODSettings.bEnabled);
		}

		if (Ar.Contains("ObjStaticMeshAsset"))
		{
			FString MeshFileName;
			Ar.Serialize("ObjStaticMeshAsset", MeshFileName);

			if (!MeshFileName.empty())
			{
				UStaticMesh* LoadedMesh = FObjManager::LoadStaticMeshAsset(MeshFileName);
				SetStaticMesh(LoadedMesh); // ScreenSizes가 에셋 기본값으로 초기화됨
			}
		}

		// SetStaticMesh 이후에 저장된 오버라이드값 적용
		if (Ar.Contains("LODScreenSizeCount"))
		{
			int32 ScreenSizeCount = 0;
			Ar.Serialize("LODScreenSizeCount", ScreenSizeCount);
			for (int32 i = 0; i < ScreenSizeCount && i < static_cast<int32>(LODSettings.ScreenSizes.size()); ++i)
			{
				char Key[32];
				snprintf(Key, sizeof(Key), "LODScreenSize_%d", i);
				if (Ar.Contains(Key))
				{
					Ar.Serialize(Key, LODSettings.ScreenSizes[i]);
				}
			}
		}

		UMeshComponent::Serialize(Ar);
	}
}
