#pragma once
#include "SceneComponent.h"

class ENGINE_API ULightComponentBase : public USceneComponent
{
public:
	DECLARE_RTTI(ULightComponentBase, USceneComponent);

	FVector GetEmissionDirectionWS() const;
	FVector GetDirectionToLightWS() const;

protected:
	void MarkTransformDirty() override;
};
