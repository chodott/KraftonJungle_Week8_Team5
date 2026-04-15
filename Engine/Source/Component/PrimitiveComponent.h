#pragma once
#include "Component/SceneComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Math/Frustum.h"
#include <memory>
#include <algorithm>
#include <cmath>

class ULevel;

struct FRenderMesh;
class FArchive;
class FMaterial;
class Archive;
struct FBoxSphereBounds;

struct FBoxSphereBounds
{
	FVector Center;
	float Radius = 0.f;
	FVector BoxExtent;
};

class ENGINE_API UPrimitiveComponent : public USceneComponent
{
public:
	DECLARE_RTTI(UPrimitiveComponent, USceneComponent)

	// virtual FBoxSphereBounds GetWorldBounds() const { return Bounds; };
	virtual FBoxSphereBounds GetWorldBounds() const { return CalcBounds(GetBoundsWorldTransform()); }
	virtual void UpdateBounds();
	virtual FBoxSphereBounds GetLocalBounds() const;
	virtual FBoxSphereBounds CalcBounds(const FMatrix& LocalToWorld) const;
	virtual FVector GetRenderWorldScale() const;
	virtual FMatrix GetRenderWorldTransform() const;
	virtual FMatrix GetBoundsWorldTransform() const;

	bool ShouldDrawDebugBounds() const { return bDrawDebugBounds; }
	void SetDrawDebugBounds(bool bEnable) { bDrawDebugBounds = bEnable; }
	void SetIgnoreParentScaleInRender(bool bEnable) { bIgnoreParentScaleInRender = bEnable; }
	bool IsIgnoringParentScaleInRender() const { return bIgnoreParentScaleInRender; }
	void SetEditorVisualization(bool bEnable) { bEditorVisualization = bEnable; }
	bool IsEditorVisualization() const { return bEditorVisualization; }
	void SetHiddenInGame(bool bInHidden) { bHiddenInGame = bInHidden; }
	bool IsHiddenInGame() const { return bHiddenInGame; }

	virtual FRenderMesh* GetRenderMesh() const { return nullptr; }

	virtual bool IsPickable() const { return true; }
	virtual bool UseSpherePicking() const { return false; }
	virtual bool HasMeshIntersection() const { return false; }
	virtual bool IntersectLocalRay(const FVector& LocalOrigin, const FVector& LocalDir, float& InOutDist) const { return false; }
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	void PostDuplicate(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;
	void Serialize(FArchive& Ar) override;

protected:
	virtual void MarkTransformDirty() override;

	FBoxSphereBounds Bounds;
	bool bDrawDebugBounds = true;
	bool bIgnoreParentScaleInRender = false;
	bool bEditorVisualization = false;
	bool bHiddenInGame = false;
};
