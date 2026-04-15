#pragma once
#include <d3d11.h>

#include "Renderer/Mesh/RenderMesh.h"
#include "CoreMinimal.h"
#include "Level/MeshBVH.h"
#include <memory>

struct FStaticMesh;

struct FStaticMeshLODSelectionContext
{
	float Distance = 0.0f;
	TArray<float> PerLODDistances; // 컴포넌트별 per-LOD 시작 거리 (비어있으면 에셋 기본값 사용)
};

struct FStaticMeshLOD
{
	std::unique_ptr<FStaticMesh> Mesh = nullptr;
	float Distance = 0.0f;
	uint32 VertexCount = 0;
};

struct ENGINE_API FStaticMesh : public FRenderMesh
{
	virtual bool UpdateVertexAndIndexBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) override;
	virtual bool CreateVertexAndIndexBuffer(ID3D11Device* Device) override;
};

struct ENGINE_API FDynamicMesh : public FRenderMesh
{
	virtual bool UpdateVertexAndIndexBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) override;
	virtual bool CreateVertexAndIndexBuffer(ID3D11Device* Device) override;

private:
	uint32 MaxVertexCapacity = 0;
	uint32 MaxIndexCapacity = 0;
};

class ENGINE_API UStaticMesh : public UObject
{
public:
	DECLARE_RTTI(UStaticMesh, UObject)
	virtual ~UStaticMesh()
	{
		if (StaticMeshAsset)
		{
			delete StaticMeshAsset;
			StaticMeshAsset = nullptr;
		}
	}

	FBoxSphereBounds LocalBounds;
	const FString& GetAssetPathFileName() const;

	void SetStaticMeshAsset(FStaticMesh* InStaticMesh);
	FStaticMesh* GetRenderData() const { return StaticMeshAsset; }
	int32 GetNumSections() const { return StaticMeshAsset ? StaticMeshAsset->GetNumSection() : 0; }

	const TArray<std::shared_ptr<FMaterial>>& GetDefaultMaterials() const { return DefaultMaterials; }
	void AddDefaultMaterial(const std::shared_ptr<FMaterial>& InMaterial) { DefaultMaterials.push_back(InMaterial); }
	bool IntersectLocalRay(const FVector& RayOrigin, const FVector& RayDirection, float& OutDistance) const;
	void BuildAccelerationStructureIfNeeded() const;
	void VisitMeshBVHNodes(const FBVHNodeVisitor& Visitor) const;

	FStaticMesh* GetRenderData(int32 LODIndex) const;
	int32 GetLODIndexForDistance(const FStaticMeshLODSelectionContext& SelectionContext) const;
	FStaticMesh* GetRenderDataForDistance(const FStaticMeshLODSelectionContext& SelectionContext, int32* OutSelectedLODIndex = nullptr) const;
	void AddLod(std::unique_ptr<FStaticMesh> InMesh, float InDistance);
	void ClearLods();
	uint32 GetLodCount() const;
	float GetLodDistance(int32 LODIndex) const;

private:
	FStaticMesh* StaticMeshAsset = nullptr;
	TArray<std::shared_ptr<FMaterial>> DefaultMaterials;
	mutable std::unique_ptr<FMeshBVH> TriangleBVH;
	TArray<FStaticMeshLOD> LODs;
};
