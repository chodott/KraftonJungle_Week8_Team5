#pragma once
#include "Math/Vector.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Viewport/Viewport.h"
#include "imgui.h"
#include <functional>
#include <memory>
class FEditorEngine;
class AActor;
class UActorComponent;
class USceneComponent;
class UBillboardComponent;
class UMeshDecalComponent;
class UStaticMeshComponent;
class UStaticMesh;
class FMaterial;
using FPropertyChangedCallback = std::function<void(const FVector&, const FVector&, const FVector&)>;

class FPropertyWindow
{
public:
	~FPropertyWindow();
	void Render(FEditorEngine* Engine);
	void SetTarget(const FVector& Location, const FVector& Rotation, const FVector& Scale,
		const char* ActorName = nullptr);

	bool    IsModified()    const { return bModified; }
	FVector GetLocation()   const { return EditLocation; }
	FVector GetRotation()   const { return EditRotation; }
	FVector GetScale()      const { return EditScale; }

	void SetOnChanged(FPropertyChangedCallback Callback) { OnChanged = Callback; }

	FPropertyChangedCallback OnChanged;
private:
	void DrawTransformSection();
	void DrawComponentSection(AActor* SelectedActor);
	void DrawSceneComponentNode(USceneComponent* Component, int32 Depth = 0);
	void DrawAttachedNonSceneComponentNodes(USceneComponent* SceneComponent, int32 Depth = 0);
	void DrawNonSceneComponentEntry(UActorComponent* Component);
	void DrawDetailsSection(UActorComponent* Component, FEditorEngine* Engine);
	void DrawSceneComponentDetails(USceneComponent* SceneComponent);
	void DrawMovementComponentDetails(class UMovementComponent* MovementComponent);
	void DrawStaticMeshComponentDetails(class UStaticMeshComponent* MeshComponent, FEditorEngine* Engine);
	void DrawRotatingMovementComponentDetails(class URotatingMovementComponent* RotatingMovementComponent);
	void DrawProjectileMovementComponentDetails(class UProjectileMovementComponent* ProjectileMovementComponent, FEditorEngine* Engine);
	void DrawSpringArmComponentDetails(class USpringArmComponent* SpringArmComponent);
	void DrawTextComponentDetails(class UTextRenderComponent* TextComponent);
	void DrawSubUVComponentDetails(class USubUVComponent* SubUVComponent);
	void DrawHeightFogComponentDetails(class UHeightFogComponent* HeightFogComponent);
	void DrawLocalHeightFogComponentDetails(class ULocalHeightFogComponent* LocalHeightFogComponent);
	void DrawBillboardComponentDetials(class UBillboardComponent* BillboardComponent, FEditorEngine* Engine);
	void DrawDecalComponentDetails(class UDecalComponent* DecalComponent, FEditorEngine* Engine);
	void DrawMeshDecalComponentDetails(class UMeshDecalComponent* MeshDecalComponent, FEditorEngine* Engine);
	void DrawFireBallComponentDetails(class UFireBallComponent* FireBallComponent);
	void DrawLightComponentDetails(class ULightComponent* LightComponent);
	void DrawPointLightComponentDetails(class UPointLightComponent* PointLightComponent, bool bShowHeader = true);
	void DrawSpotLightComponentDetails(class USpotLightComponent* SpotLightComponent);
	void DrawMaterialPreviewSection(class UStaticMeshComponent* MeshComponent, FEditorEngine* Engine);
	bool RenderMaterialPreview(
		class UStaticMeshComponent* MeshComponent,
		class FMaterial* PreviewMaterial,
		FEditorEngine* Engine,
		const ImVec2& PreviewSize);
	bool EnsureMaterialPreviewScene(FEditorEngine* Engine);
	bool DrawVector3Control(const char* Label, const FVector& Value, FVector& OutValue, float Speed, const char* Format);
	bool DrawAddComponentButton(AActor* SelectedActor);
	bool AddComponentToActor(AActor* SelectedActor, class UClass* ComponentClass, const char* BaseName);
	bool IsComponentOwnedByActor(AActor* SelectedActor, UActorComponent* Component) const;
	USceneComponent* GetSelectedSceneComponent(AActor* SelectedActor) const;

	FVector EditLocation = { 0.0f, 0.0f, 0.0f };
	FVector EditRotation = { 0.0f, 0.0f, 0.0f };
	FVector EditScale = { 1.0f, 1.0f, 1.0f };
	char    ActorNameBuf[128] = "None";
	bool    bModified = false;
	UActorComponent* SelectedComponent = nullptr;
	UStaticMeshComponent* PreviewedMeshComponent = nullptr;
	class UStaticMesh* PreviewSphereMesh = nullptr;
	int32 PreviewMaterialSectionIndex = 0;
	float PreviewOrbitYaw = 45.0f;
	float PreviewOrbitPitch = -20.0f;
	float PreviewOrbitDistance = 3.5f;
	std::unique_ptr<FViewport> MaterialPreviewViewport;
	std::unique_ptr<FSceneTargetManager> MaterialPreviewTargetManager;
};
