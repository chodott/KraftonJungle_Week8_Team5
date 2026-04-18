#pragma once
#include "Actor.h"
#include "Component/AmbientLightComponent.h"
#include "Component/BillboardComponent.h"

class ENGINE_API AAmbientLightActor : public AActor
{
public:
	DECLARE_RTTI(AAmbientLightActor, AActor);
	
	void PostSpawnInitialize() override;
	void OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent) override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;
	UAmbientLightComponent* GetAmbientLightComponent() const { return AmbientLightComponent; }
	
private:
	void UpdateBillboardTint();

	UBillboardComponent* IconBillboardComponent = nullptr;
	UAmbientLightComponent* AmbientLightComponent = nullptr;
};
