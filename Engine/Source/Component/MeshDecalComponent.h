#pragma once

#include "Component/DecalComponent.h"

class ENGINE_API UMeshDecalComponent : public UDecalComponent
{
public:
	DECLARE_RTTI(UMeshDecalComponent, UDecalComponent)

	void PostConstruct() override;
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;
	virtual bool IsPickable() const override { return false; }

	void SetSurfaceOffset(float InSurfaceOffset);
	float GetSurfaceOffset() const { return SurfaceOffset; }

	// Mesh decal path uses cluster data revision as geometry revision key.
	uint32 GetGeometryRevision() const { return GetClusterRevision(); }

private:
	float SurfaceOffset = 0.002f;
};
