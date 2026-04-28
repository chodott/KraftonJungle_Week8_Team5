#pragma once
#include "SceneComponent.h"
#include "Renderer/Features/Lighting/LightTypes.h"
class ENGINE_API ULightComponentBase : public USceneComponent
{
public:
	DECLARE_RTTI(ULightComponentBase, USceneComponent);

	FVector GetEmissionDirectionWS() const;
	FVector GetDirectionToLightWS() const;
	// GetMobility는 USceneComponent에서 상속

	bool IsCastingShadows() const
	{
		return bCastShadows;
	}
	bool IsCacheDirty() const
	{
		return bShadowCacheDirty;
	}
	void SetMobility(ELightMobility InMobility) override;  // USceneComponent의 virtual override
	void SetCastingShadows(bool bNewCastShadows);
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

	// 그림자 캐시 더티 — 라이트가 움직이거나 처음 등록될 때 true. 캐시 갱신 후 ResetShadowCacheDirty()로 false.
	bool IsShadowCacheDirty() const    { return bShadowCacheDirty; }
	void ResetShadowCacheDirty() const { bShadowCacheDirty = false; }

protected:
	void MarkTransformDirty() override;
	// Mobility는 USceneComponent로 이동
	bool bCastShadows = true;
	mutable bool bShadowCacheDirty = true;
};
