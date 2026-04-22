#pragma once
#include "Actor.h"
#include "Component/BillboardComponent.h"
#include "Component/DirectionalLightComponent.h"

class UStaticMeshComponent;

class ENGINE_API ADirectionalLightActor : public AActor
{
public:
	DECLARE_RTTI(ADirectionalLightActor, AActor)
	
	void PostSpawnInitialize() override;
	void OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent) override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;
	UDirectionalLightComponent* GetDirectionalLightComponent() const { return DirectionalLightComponent; }
	FVector GetEmissionDirectionWS() const
	{
		return DirectionalLightComponent ? DirectionalLightComponent->GetEmissionDirectionWS() : FVector::ForwardVector;
	}
	FVector GetDirectionToLightWS() const
	{
		return DirectionalLightComponent ? DirectionalLightComponent->GetDirectionToLightWS() : FVector::BackwardVector;
	}
	
private:
	void UpdateBillboardTint();

	UBillboardComponent * IconBillboardComponent = nullptr;
	UDirectionalLightComponent* DirectionalLightComponent = nullptr;
	UStaticMeshComponent* ArrowComponent = nullptr;
};
