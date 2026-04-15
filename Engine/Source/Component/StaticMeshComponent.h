#pragma once
#include "CoreMinimal.h"
#include "Component/PrimitiveComponent.h"
#include "Component/MeshComponent.h"
#include "Serializer/Archive.h"

class UStaticMesh;

struct FStaticMeshComponentLODSettings
{
	bool bEnabled = true;
	float ScreenSizeScale = 1.0f;
	float ScreenSizeBias = 0.0f;
};

class ENGINE_API UStaticMeshComponent : public UMeshComponent
{
public:
	DECLARE_RTTI(UStaticMeshComponent, UMeshComponent)

	void SetStaticMesh(UStaticMesh* InStaticMesh);
	void SetLODEnabled(bool bEnabled);
	bool IsLODEnabled() const;
	void SetLODScreenSizeScale(float InScale);
	float GetLODScreenSizeScale() const;
	void SetLODScreenSizeBias(float InBias);
	float GetLODScreenSizeBias() const;
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
	UStaticMesh* StaticMesh = nullptr;
	FStaticMeshComponentLODSettings LODSettings;
};
