#pragma once
#include "CoreMinimal.h"
#include "Component/PrimitiveComponent.h"
#include "Component/MeshComponent.h"
#include "Serializer/Archive.h"

class UStaticMesh;

struct FStaticMeshComponentLODSettings
{
	bool bEnabled = true;
	TArray<float> Distances; // 컴포넌트별 per-LOD 시작 거리, 인덱스 0 = LOD1
};

class ENGINE_API UStaticMeshComponent : public UMeshComponent
{
public:
	DECLARE_RTTI(UStaticMeshComponent, UMeshComponent)

	void SetStaticMesh(UStaticMesh* InStaticMesh);
	void SetLODEnabled(bool bEnabled);
	bool IsLODEnabled() const;
	void SetLODDistance(int32 LODIndex, float Distance); // LODIndex 1-based
	float GetLODDistance(int32 LODIndex) const;
	int32 GetLODDistanceCount() const;
	int32 GetCurrentLODIndex() const;
	float GetLastLODSelectionDistance() const;
	const FStaticMeshComponentLODSettings& GetLODSettings() const;
	void SetLODSettings(const FStaticMeshComponentLODSettings& InSettings);
	FRenderMesh* GetRenderMesh() const override;
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	FRenderMesh* GetRenderMesh(const FRenderMeshSelectionContext& SelectionContext) const override;
	FRenderMesh* GetRenderMesh(const float& Distance) const override;
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	FBoxSphereBounds CalcBounds(const FMatrix& LocalToWorld) const override;
	FBoxSphereBounds GetLocalBounds() const override;
	virtual bool HasMeshIntersection() const override { return true; }
	virtual bool IntersectLocalRay(const FVector& LocalOrigin, const FVector& LocalDir, float& InOutDist) const override;

private:
	int32 GetAssetLodDistanceCount() const;
	void SyncLODDistancesWithAsset();
	void NormalizeLODDistances();
	float ConvertLegacyScreenSizeToDistance(float ScreenSize, int32 LODIndex) const;

	UStaticMesh* StaticMesh = nullptr;
	FStaticMeshComponentLODSettings LODSettings;
	mutable int32 LastResolvedLODIndex = 0;
	mutable float LastResolvedLODDistance = 0.0f;
};
