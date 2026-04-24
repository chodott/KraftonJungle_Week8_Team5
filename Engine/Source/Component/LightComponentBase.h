#pragma once
#include "SceneComponent.h"

class ENGINE_API ULightComponentBase : public USceneComponent
{
public:
	DECLARE_RTTI(ULightComponentBase, USceneComponent);

	FVector GetEmissionDirectionWS() const;
	FVector GetDirectionToLightWS() const;
	bool IsCastingShadows() const { return bCastShadows; }
	void SetCastingShadows(bool bNewCastShadows);

protected:
	void MarkTransformDirty() override;

	bool bCastShadows = true;
};
