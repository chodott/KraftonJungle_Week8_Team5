#pragma once
#include "SceneComponent.h"
#include "Renderer/Features/Lighting/LightTypes.h"
class ENGINE_API ULightComponentBase : public USceneComponent
{
public:
	DECLARE_RTTI(ULightComponentBase, USceneComponent);

	FVector GetEmissionDirectionWS() const;
	FVector GetDirectionToLightWS() const;

	bool IsCastingShadows() const
	{
		return bCastShadows;
	}

	void SetCastingShadows(bool bNewCastShadows);
	void Serialize(FArchive& Ar) override;
	void DuplicateShallow(UObject* DuplicatedObject, FDuplicateContext& Context) const override;

protected:
	void MarkTransformDirty() override;
	ELightMobility Mobility = ELightMobility::Movable;
	bool bCastShadows = true;
};
