#pragma once
#include "Actor.h"
#include "Component/BillboardComponent.h"
#include "Component/SpotLightComponent.h"

class ULineBatchComponent;

class ENGINE_API ASpotLightActor : public AActor
{
public:
	DECLARE_RTTI(ASpotLightActor, AActor);
	
	void PostSpawnInitialize() override;
	void OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent) override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;
	USpotLightComponent* GetSpotLightComponent() const { return SpotLightComponent; }
	void SetEditorGizmoVisible(bool bVisible);
	FVector GetEmissionDirectionWS() const
	{
		return SpotLightComponent ? SpotLightComponent->GetEmissionDirectionWS() : FVector::ForwardVector;
	}
	FVector GetDirectionToLightWS() const
	{
		return SpotLightComponent ? SpotLightComponent->GetDirectionToLightWS() : FVector::BackwardVector;
	}
	
private:
	void UpdateConeGizmo();
	void UpdateBillboardTint();

	UBillboardComponent* IconBillboardComponent = nullptr;
	USpotLightComponent* SpotLightComponent = nullptr;
	ULineBatchComponent* ConeGizmoComponent = nullptr;
	bool bEditorGizmoVisible = false;
};
