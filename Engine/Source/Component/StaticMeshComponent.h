#pragma once
#include "CoreMinimal.h"
#include "Component/PrimitiveComponent.h"
#include "Component/MeshComponent.h"
#include "Serializer/Archive.h"

class UStaticMesh;

struct FStaticMeshComponentLODSettings
{
	bool bEnabled = true;
	TArray<float> ScreenSizes; // 컴포넌트별 per-LOD 임계값, 인덱스 0 = LOD1
};

class ENGINE_API UStaticMeshComponent : public UMeshComponent
{
public:
	DECLARE_RTTI(UStaticMeshComponent, UMeshComponent)

	void SetStaticMesh(UStaticMesh* InStaticMesh);
	void SetLODEnabled(bool bEnabled);
	bool IsLODEnabled() const;
	void SetLODScreenSize(int32 LODIndex, float ScreenSize); // LODIndex 1-based
	float GetLODScreenSize(int32 LODIndex) const;
	int32 GetLODScreenSizeCount() const;
	const FStaticMeshComponentLODSettings& GetLODSettings() const;
	void SetLODSettings(const FStaticMeshComponentLODSettings& InSettings);
	FRenderMesh* GetRenderMesh() const override;
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	FRenderMesh* GetRenderMesh(const FRenderMeshSelectionContext& SelectionContext) const override;
	FRenderMesh* GetRenderMesh(const float& Distance) const override;
	// 현재는 일단 .obj파싱 용도로 사용 - 추후 직렬화?
	// virtual void Serialize(FArchive& Ar) override;
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	FBoxSphereBounds CalcBounds(const FMatrix& LocalToWorld) const override;
	FBoxSphereBounds GetLocalBounds() const override;
	virtual bool HasMeshIntersection() const override { return true; }
	virtual bool IntersectLocalRay(const FVector& LocalOrigin, const FVector& LocalDir, float& InOutDist) const override;
	
private:
	int32 GetAssetLodScreenSizeCount() const;
	void SyncLODScreenSizesWithAsset();

	UStaticMesh* StaticMesh = nullptr;
	FStaticMeshComponentLODSettings LODSettings;
};
