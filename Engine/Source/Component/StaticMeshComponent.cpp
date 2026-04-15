#include "StaticMeshComponent.h"

#include <algorithm>
#include <filesystem>
#include <limits>

#include "Object/Class.h"
#include "Component/PrimitiveComponent.h"
#include "Component/MeshComponent.h"
#include "Asset/ObjManager.h"
#include "Core/Paths.h"
#include "Debug/EngineLog.h"
#include "Renderer/Mesh/MeshData.h"
#include "Renderer/Resources/Material/Material.h"

IMPLEMENT_RTTI(UStaticMeshComponent, UMeshComponent)

namespace
{
	constexpr float GLegacyProjectionScaleY = 1.7320508075688772f; // 60도 수직 FOV 기준 근사치
	constexpr float GMinLegacyScreenSize = 0.0001f;
}

int32 UStaticMeshComponent::GetAssetLodDistanceCount() const
{
	if (!StaticMesh)
	{
		return 0;
	}

	return (std::max)(static_cast<int32>(StaticMesh->GetLodCount()) - 1, 0);
}

void UStaticMeshComponent::SyncLODDistancesWithAsset()
{
	const int32 AssetLodCount = GetAssetLodDistanceCount();
	if (AssetLodCount <= 0 || !StaticMesh)
	{
		LODSettings.Distances.clear();
		return;
	}

	if (static_cast<int32>(LODSettings.Distances.size()) > AssetLodCount)
	{
		LODSettings.Distances.resize(static_cast<size_t>(AssetLodCount));
	}

	for (int32 LodIndex = static_cast<int32>(LODSettings.Distances.size()) + 1; LodIndex <= AssetLodCount; ++LodIndex)
	{
		LODSettings.Distances.push_back(StaticMesh->GetLodDistance(LodIndex));
	}

	NormalizeLODDistances();
}

void UStaticMeshComponent::NormalizeLODDistances()
{
	float PreviousDistance = 0.0f;
	for (float& Distance : LODSettings.Distances)
	{
		Distance = (std::max)(Distance, PreviousDistance);
		PreviousDistance = Distance;
	}
}

float UStaticMeshComponent::ConvertLegacyScreenSizeToDistance(float ScreenSize, int32 LODIndex) const
{
	if (!StaticMesh)
	{
		return (std::max)(ScreenSize, 0.0f);
	}

	const float SafeScreenSize = (std::max)(ScreenSize, GMinLegacyScreenSize);
	const float BoundsRadius = (std::max)(StaticMesh->LocalBounds.Radius, 1.0f);
	const float ApproxDistance = BoundsRadius * GLegacyProjectionScaleY / SafeScreenSize;
	const float AssetDistance = StaticMesh->GetLodDistance(LODIndex);
	return AssetDistance > 0.0f ? AssetDistance : ApproxDistance;
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
	StaticMesh = InStaticMesh;
	LODSettings.Distances.clear();
	LastResolvedLODIndex = 0;
	LastResolvedLODDistance = 0.0f;

	if (StaticMesh)
	{
		int32 NeededMaterialSlots = StaticMesh->GetNumSections();
		Materials.resize(NeededMaterialSlots, nullptr);
		const TArray<std::shared_ptr<FMaterial>>& DefaultMats = StaticMesh->GetDefaultMaterials();
		for (int32 i = 0; i < NeededMaterialSlots && i < DefaultMats.size(); ++i)
		{
			Materials[i] = DefaultMats[i]->CreateDynamicMaterial();
		}

		SyncLODDistancesWithAsset();
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

void UStaticMeshComponent::SetLODDistance(int32 LODIndex, float Distance)
{
	if (LODIndex <= 0)
	{
		return;
	}

	const int32 AssetLodCount = GetAssetLodDistanceCount();
	if (AssetLodCount > 0 && LODIndex > AssetLodCount)
	{
		return;
	}

	SyncLODDistancesWithAsset();

	const size_t Idx = static_cast<size_t>(LODIndex - 1);
	if (Idx >= LODSettings.Distances.size())
	{
		return;
	}

	float ClampedDistance = (std::max)(Distance, 0.0f);
	if (Idx > 0)
	{
		ClampedDistance = (std::max)(ClampedDistance, LODSettings.Distances[Idx - 1]);
	}
	if (Idx + 1 < LODSettings.Distances.size())
	{
		ClampedDistance = (std::min)(ClampedDistance, LODSettings.Distances[Idx + 1]);
	}

	LODSettings.Distances[Idx] = ClampedDistance;
	NormalizeLODDistances();
}

float UStaticMeshComponent::GetLODDistance(int32 LODIndex) const
{
	if (LODIndex <= 0)
	{
		return 0.0f;
	}

	const size_t Idx = static_cast<size_t>(LODIndex - 1);
	if (Idx >= LODSettings.Distances.size())
	{
		if (StaticMesh && LODIndex <= GetAssetLodDistanceCount())
		{
			return StaticMesh->GetLodDistance(LODIndex);
		}
		return 0.0f;
	}

	return LODSettings.Distances[Idx];
}

int32 UStaticMeshComponent::GetLODDistanceCount() const
{
	return (std::max)(static_cast<int32>(LODSettings.Distances.size()), GetAssetLodDistanceCount());
}

int32 UStaticMeshComponent::GetCurrentLODIndex() const
{
	return LastResolvedLODIndex;
}

float UStaticMeshComponent::GetLastLODSelectionDistance() const
{
	return LastResolvedLODDistance;
}

const FStaticMeshComponentLODSettings& UStaticMeshComponent::GetLODSettings() const
{
	return LODSettings;
}

void UStaticMeshComponent::SetLODSettings(const FStaticMeshComponentLODSettings& InSettings)
{
	LODSettings = InSettings;
	SyncLODDistancesWithAsset();
	NormalizeLODDistances();
}

FRenderMesh* UStaticMeshComponent::GetRenderMesh() const
{
	return StaticMesh ? StaticMesh->GetRenderData() : nullptr;
}

FRenderMesh* UStaticMeshComponent::GetRenderMesh(const FRenderMeshSelectionContext& SelectionContext) const
{
	LastResolvedLODDistance = (std::max)(SelectionContext.Distance, 0.0f);
	LastResolvedLODIndex = 0;

	if (!StaticMesh)
	{
		return nullptr;
	}

	if (!LODSettings.bEnabled)
	{
		return StaticMesh->GetRenderData();
	}

	FStaticMeshLODSelectionContext LODSelectionContext;
	LODSelectionContext.Distance = SelectionContext.Distance;
	LODSelectionContext.PerLODDistances = LODSettings.Distances;
	return StaticMesh->GetRenderDataForDistance(LODSelectionContext, &LastResolvedLODIndex);
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
		int32 DistanceCount = static_cast<int32>(LODSettings.Distances.size());
		Ar.Serialize("LODDistanceCount", DistanceCount);
		for (int32 i = 0; i < DistanceCount; ++i)
		{
			char Key[32];
			snprintf(Key, sizeof(Key), "LODDistance_%d", i);
			Ar.Serialize(Key, LODSettings.Distances[i]);
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
				SetStaticMesh(LoadedMesh);
			}
		}

		if (Ar.Contains("LODDistanceCount"))
		{
			int32 DistanceCount = 0;
			Ar.Serialize("LODDistanceCount", DistanceCount);
			for (int32 i = 0; i < DistanceCount && i < static_cast<int32>(LODSettings.Distances.size()); ++i)
			{
				char Key[32];
				snprintf(Key, sizeof(Key), "LODDistance_%d", i);
				if (Ar.Contains(Key))
				{
					Ar.Serialize(Key, LODSettings.Distances[i]);
				}
			}
			NormalizeLODDistances();
		}
		else if (Ar.Contains("LODScreenSizeCount"))
		{
			int32 ScreenSizeCount = 0;
			Ar.Serialize("LODScreenSizeCount", ScreenSizeCount);
			for (int32 i = 0; i < ScreenSizeCount && i < static_cast<int32>(LODSettings.Distances.size()); ++i)
			{
				char Key[32];
				snprintf(Key, sizeof(Key), "LODScreenSize_%d", i);
				if (Ar.Contains(Key))
				{
					float LegacyScreenSize = 0.0f;
					Ar.Serialize(Key, LegacyScreenSize);
					LODSettings.Distances[i] = ConvertLegacyScreenSizeToDistance(LegacyScreenSize, i + 1);
				}
			}
			NormalizeLODDistances();
		}

		UMeshComponent::Serialize(Ar);
	}
}
