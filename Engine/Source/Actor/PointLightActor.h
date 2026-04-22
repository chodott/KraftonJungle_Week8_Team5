#pragma once
#include "Actor.h"
#include "Component/BillboardComponent.h"
#include "Component/PointLightComponent.h"

class ULineBatchComponent;

class ENGINE_API APointLightActor : public AActor
{
public:
	DECLARE_RTTI(APointLightActor, AActor);
	
	void PostSpawnInitialize() override;
	void OnOwnedComponentPropertyChanged(UActorComponent* ChangedComponent) override;
	void Serialize(FArchive& Ar) override;
	void FixupDuplicatedReferences(UObject* DuplicatedObject, const FDuplicateContext& Context) const override;
	UPointLightComponent* GetPointLightComponent() const { return PointLightComponent; }
	void SetEditorGizmoVisible(bool bVisible);
	
private:
	void UpdateRadiusGizmo();
	void UpdateBillboardTint();

	UBillboardComponent* IconBillboardComponent = nullptr;
	UPointLightComponent* PointLightComponent = nullptr;
	ULineBatchComponent* RadiusGizmoComponent = nullptr;
	bool bEditorGizmoVisible = false;
};
