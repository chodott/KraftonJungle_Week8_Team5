#pragma once

#include "Component/PrimitiveComponent.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Renderer/Mesh/MeshData.h"

class ULineBatchComponent : public UPrimitiveComponent
{
	DECLARE_RTTI(ULineBatchComponent, UPrimitiveComponent)

public:
	void PostConstruct() override;
	void DrawLine(FVector InStart, FVector InEnd, FVector4 color);
	void DrawWireCube(FVector InCenter, FQuat InRotation, FVector InScale, FVector4 InColor);
	void DrawWireSphere(FVector InCenter, float InRadius, FVector4 InColor);
	void DrawWireCone(
		FVector InOrigin,
		FVector InDirection,
		float InLength,
		float InAngleDegrees,
		FVector4 InColor,
		int32 InSegments = 24,
		int32 InSpokes = 8,
		bool bDrawSphericalCap = false);
	void Clear();

	virtual FRenderMesh* GetRenderMesh() const override { return LineMesh.get(); }
	virtual FRenderMesh* GetRenderMesh(const FRenderMeshSelectionContext& SelectionContext) const override
	{
		(void)SelectionContext;
		return LineMesh.get();
	}
	virtual FRenderMesh* GetRenderMesh(const float& Distance) const override
	{
		(void)Distance;
		return LineMesh.get();
	}
	virtual FBoxSphereBounds GetLocalBounds() const override;
	virtual bool IsPickable() const override { return false; }
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

private:
	std::shared_ptr<FDynamicMesh> LineMesh;
};
