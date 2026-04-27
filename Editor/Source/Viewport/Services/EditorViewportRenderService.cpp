#include "Viewport/Services/EditorViewportRenderService.h"

#include "EditorEngine.h"
#include "Viewport/EditorViewportRegistry.h"
#include "Actor/Actor.h"
#include "Camera/Camera.h"
#include "Core/Engine.h"
#include "Gizmo/Gizmo.h"
#include "Math/Frustum.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Renderer.h"
#include "Level/Level.h"
#include "Level/PrimitiveVisibilityUtils.h"
#include "UI/EditorUI.h"
#include "Viewport/Viewport.h"
#include "World/World.h"
#include "World/WorldContext.h"
#include "Component/PrimitiveComponent.h"
#include "Component/MeshComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/SkyComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextComponent.h"
#include "Component/CameraComponent.h"
#include "Slate/Widget/Painter.h"

namespace
{
	void BuildGridVectors(const FMatrix& ViewMatrix, const FViewportLocalState& LocalState, FVector& OutGridAxisU, FVector& OutGridAxisV, FVector& OutViewForward)
	{
		const FMatrix ViewInverse = ViewMatrix.GetInverse();
		OutViewForward = ViewInverse.GetForwardVector().GetSafeNormal();

		if (LocalState.ProjectionType == EViewportType::Perspective)
		{
			OutGridAxisU = FVector::ForwardVector;
			OutGridAxisV = FVector::RightVector;
			return;
		}

		OutGridAxisU = ViewInverse.GetRightVector().GetSafeNormal();
		OutGridAxisV = ViewInverse.GetUpVector().GetSafeNormal();
	}

	TArray<FOutlineRenderItem> BuildSelectionOutlineItems(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags)
	{
		TArray<FOutlineRenderItem> OutlineItems;
		if (SelectedActors.empty())
		{
			return OutlineItems;
		}

		for (AActor* Selected : SelectedActors)
		{
			if (!Selected || Selected->IsPendingDestroy() || !Selected->IsVisible())
			{
				continue;
			}

			if (Selected->GetComponentByClass<USkyComponent>() != nullptr)
			{
				continue;
			}

			for (UActorComponent* Component : Selected->GetComponents())
			{
				if (!Component || !Component->IsA(UPrimitiveComponent::StaticClass()))
				{
					continue;
				}

				if (Component->IsA(UTextRenderComponent::StaticClass()) ||
					Component->IsA(USubUVComponent::StaticClass()) ||
					Component->IsA(UBillboardComponent::StaticClass()))
				{
					continue;
				}

				UPrimitiveComponent* PrimitiveComponent = static_cast<UPrimitiveComponent*>(Component);
				if (PrimitiveComponent->IsEditorVisualization())
				{
					continue;
				}

				if (IsArrowVisualizationPrimitive(PrimitiveComponent)
					|| IsHiddenByArrowVisualizationShowFlags(PrimitiveComponent, ShowFlags))
				{
					continue;
				}

				FRenderMesh* RenderMesh = PrimitiveComponent->GetRenderMesh();
				if (!RenderMesh)
				{
					continue;
				}

				UMeshComponent* MeshComponent =
					Component->IsA(UMeshComponent::StaticClass())
					? static_cast<UMeshComponent*>(Component)
					: nullptr;
				const int32 SectionCount = RenderMesh->GetNumSection();

				if (SectionCount > 0)
				{
					for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
					{
						const FMeshSection& Section = RenderMesh->Sections[SectionIndex];
						FOutlineRenderItem& Item = OutlineItems.emplace_back();
						Item.Mesh = RenderMesh;
						Item.Material = MeshComponent ? MeshComponent->GetMaterial(Section.MaterialIndex).get() : nullptr;
						Item.WorldMatrix = PrimitiveComponent->GetRenderWorldTransform();
						Item.IndexStart = Section.StartIndex;
						Item.IndexCount = Section.IndexCount;
					}
					continue;
				}

				FOutlineRenderItem& Item = OutlineItems.emplace_back();
				Item.Mesh = RenderMesh;
				Item.Material = MeshComponent ? MeshComponent->GetMaterial(0).get() : nullptr;
				Item.WorldMatrix = PrimitiveComponent->GetRenderWorldTransform();
			}
		}

		return OutlineItems;
	}

	static EViewportCompositeMode ResolveViewportCompositeMode(ERenderMode RenderMode)
	{
		if (RenderMode == ERenderMode::SceneDepth)
		{
			return EViewportCompositeMode::DepthView;
		}

		return EViewportCompositeMode::SceneColor;
	}
}

void FEditorViewportRenderService::RenderAll(
	FEngine* Engine,
	FRenderer* Renderer,
	FEditorEngine* EditorEngine,
	FEditorViewportRegistry& ViewportRegistry,
	FEditorUI& EditorUI,
	FGizmo& Gizmo,
	const std::shared_ptr<FMaterial>& WireFrameMaterial,
	FRenderMesh* GridMesh,
	FMaterial* GridMaterials[MAX_VIEWPORTS],
	FRenderMesh* WorldAxisMesh,
	FMaterial* WorldAxisMaterials[MAX_VIEWPORTS],
	const FBuildSceneRenderPacket& BuildSceneRenderPacket) const
{
	if (!Engine || !Renderer || !EditorEngine)
	{
		return;
	}

	ID3D11Device* Device = Renderer->GetDevice();
	if (!Device || !Renderer->GetDeviceContext())
	{
		return;
	}

	FEditorFrameRequest FrameRequest;
	const TArray<FViewportEntry>& Entries = ViewportRegistry.GetEntries();
	AActor* SelectedActor = EditorEngine->GetSelectedActor();
	const TArray<AActor*> SelectedActors = EditorEngine->GetSelectedActors();

	int32 EntryIndex = 0;
	for (const FViewportEntry& Entry : Entries)
	{
		const int32 CurrentEntryIndex = EntryIndex++;
		if (!Entry.bActive || !Entry.Viewport)
		{
			continue;
		}

		Entry.Viewport->EnsureResources(Device);

		ID3D11RenderTargetView* RTV = Entry.Viewport->GetRTV();
		ID3D11DepthStencilView* DSV = Entry.Viewport->GetDSV();
		if (!RTV || !DSV)
		{
			continue;
		}

		const FRect& Rect = Entry.Viewport->GetRect();
		D3D11_VIEWPORT Viewport = {};
		Viewport.TopLeftX = 0.0f;
		Viewport.TopLeftY = 0.0f;
		Viewport.Width = static_cast<float>(Rect.Width);
		Viewport.Height = static_cast<float>(Rect.Height);
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;

		const float AspectRatio = static_cast<float>(Rect.Width) / static_cast<float>(Rect.Height);
		FWorldContext* EntryWorldContext = Entry.WorldContext;
		UWorld* EntryWorld = EntryWorldContext ? EntryWorldContext->World : nullptr;
		if (!EntryWorld)
		{
			continue;
		}
		const bool bIsEditorWorld = EntryWorldContext && EntryWorldContext->WorldType == EWorldType::Editor;
		const bool bCanShowEditorSelection = bIsEditorWorld && !SelectedActors.empty();

		FSceneRenderPacket ScenePacket;
		// 씬 패킷과 별도로, 그리드/기즈모 같은 추가 씬 커맨드는 별도 큐로 유지한다.
		TArray<FMeshBatch> AdditionalMeshBatches;
		AdditionalMeshBatches.reserve(Renderer->GetPrevCommandCount());
		FMatrix ViewMatrix = Entry.LocalState.BuildViewMatrix();
		FMatrix ProjectionMatrix = Entry.LocalState.BuildProjMatrix(AspectRatio);
		float EffectiveNearPlane = Entry.LocalState.NearPlane;
		float EffectiveFarPlane = Entry.LocalState.FarPlane;
		ERenderMode EffectiveRenderMode = Entry.LocalState.ViewMode;
		const bool bIsPIEPossessedView =
			EntryWorldContext &&
			EntryWorldContext->WorldType == EWorldType::PIE &&
			EditorEngine->IsPIEActive() &&
			EditorEngine->IsPIEInputCaptured();
		if (EntryWorldContext &&
			EntryWorldContext->WorldType == EWorldType::PIE &&
			EditorEngine->IsPIEActive() &&
			EditorEngine->IsPIEInputCaptured())
		{
			if (UCameraComponent* ActiveCameraComponent = EntryWorld->GetActiveCameraComponent())
			{
				if (FCamera* ActiveCamera = ActiveCameraComponent->GetCamera())
				{
					ActiveCamera->SetAspectRatio(AspectRatio);
				}
				ViewMatrix = ActiveCameraComponent->GetViewMatrix();
				ProjectionMatrix = ActiveCameraComponent->GetProjectionMatrix();
				EffectiveNearPlane = ActiveCameraComponent->GetNearPlane();
				EffectiveFarPlane = ActiveCameraComponent->GetFarPlane();
			}
		}
		if (bIsPIEPossessedView && (EffectiveRenderMode == ERenderMode::LightCullingHeatmap || EffectiveRenderMode == ERenderMode::CSMCascadeDebug))
		{
			// PIE 소유 시점에서는 LightCulling Heatmap 디버그 뷰를 강제 비활성화한다.
			// (게임 화면에 디버그 시각화가 섞여 보이는 것을 방지)
			EffectiveRenderMode = ERenderMode::Lit_Phong;
		}
		FFrustum Frustum;
		Frustum.ExtractFromVP(ViewMatrix * ProjectionMatrix);
		const FVector CameraPosition = ViewMatrix.GetInverse().GetTranslation();
		BuildSceneRenderPacket(Engine, EntryWorld, Frustum, Entry.LocalState.ShowFlags, ScenePacket);

		if (bCanShowEditorSelection && SelectedActor && SelectedActor->GetWorld() == EntryWorld &&
			SelectedActor->GetComponentByClass<USkyComponent>() == nullptr)
		{
			Gizmo.BuildMeshBatches(SelectedActor, &Entry, AdditionalMeshBatches);
		}

		FVector GridAxisU = FVector::ForwardVector;
		FVector GridAxisV = FVector::RightVector;
		FVector ViewForward = FVector::ForwardVector;
		BuildGridVectors(ViewMatrix, Entry.LocalState, GridAxisU, GridAxisV, ViewForward);

		FMaterial* EntryGridMaterial = (CurrentEntryIndex < MAX_VIEWPORTS) ? GridMaterials[CurrentEntryIndex] : nullptr;
		if (Entry.LocalState.bShowGrid && GridMesh && EntryGridMaterial)
		{
			EntryGridMaterial->SetParameterData("GridSize", &Entry.LocalState.GridSize, 4);
			EntryGridMaterial->SetParameterData("LineThickness", &Entry.LocalState.LineThickness, 4);
			EntryGridMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector));
			EntryGridMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector));
			EntryGridMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector));

			FMeshBatch GridBatch;
			GridBatch.Mesh = GridMesh;
			GridBatch.Material = EntryGridMaterial;
			GridBatch.World = FMatrix::Identity;
			GridBatch.Domain = EMaterialDomain::EditorGrid;
			GridBatch.PassMask = static_cast<uint32>(EMeshPassMask::EditorGrid);
			GridBatch.bDisableDepthWrite = true;
			GridBatch.bDisableDepthTest = false;
			GridBatch.bDisableCulling = true;
			AdditionalMeshBatches.push_back(GridBatch);
		}

		FMaterial* EntryWorldAxisMaterial = (CurrentEntryIndex < MAX_VIEWPORTS) ? WorldAxisMaterials[CurrentEntryIndex] : nullptr;
		if (Entry.LocalState.ShowFlags.HasFlag(EEngineShowFlags::SF_WorldAxis) && WorldAxisMesh && EntryWorldAxisMaterial)
		{
			EntryWorldAxisMaterial->SetParameterData("GridSize", &Entry.LocalState.GridSize, 4);
			EntryWorldAxisMaterial->SetParameterData("LineThickness", &Entry.LocalState.LineThickness, 4);
			EntryWorldAxisMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector));
			EntryWorldAxisMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector));
			EntryWorldAxisMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector));

			FMeshBatch WorldAxisBatch;
			WorldAxisBatch.Mesh = WorldAxisMesh;
			WorldAxisBatch.Material = EntryWorldAxisMaterial;
			WorldAxisBatch.World = FMatrix::Identity;
			WorldAxisBatch.Domain = EMaterialDomain::EditorPrimitive;
			WorldAxisBatch.PassMask = static_cast<uint32>(EMeshPassMask::EditorPrimitive);
			WorldAxisBatch.bDisableDepthWrite = true;
			WorldAxisBatch.bDisableDepthTest = false;
			WorldAxisBatch.bDisableCulling = true;
			AdditionalMeshBatches.push_back(WorldAxisBatch);
		}

		FViewportScenePassRequest ScenePass;
		ScenePass.RenderTargetView = RTV;
		ScenePass.RenderTargetShaderResourceView = Entry.Viewport->GetSRV();
		ScenePass.DepthStencilView = DSV;
		ScenePass.DepthShaderResourceView = Entry.Viewport->GetDepthSRV();
		ScenePass.Viewport = Viewport;
		ScenePass.ScenePacket = std::move(ScenePacket);
		ScenePass.SceneView.ViewMatrix = ViewMatrix;
		ScenePass.SceneView.ProjectionMatrix = ProjectionMatrix;
		ScenePass.SceneView.CameraPosition = CameraPosition;
		ScenePass.SceneView.NearZ = EffectiveNearPlane;
		ScenePass.SceneView.FarZ = EffectiveFarPlane;
		ScenePass.SceneView.TotalTimeSeconds = Engine ? static_cast<float>(Engine->GetTimer().GetTotalTime()) : 0.0f;
		ScenePass.AdditionalMeshBatches = std::move(AdditionalMeshBatches);
		ScenePass.RenderMode = EffectiveRenderMode;
		ScenePass.bForceWireframe = (EffectiveRenderMode == ERenderMode::Wireframe && WireFrameMaterial != nullptr);
		ScenePass.WireframeMaterial = WireFrameMaterial.get();
		ScenePass.OutlineRequest.bEnabled =
			bCanShowEditorSelection &&
			Entry.LocalState.ShowFlags.HasFlag(EEngineShowFlags::SF_Primitives);
		if (ScenePass.OutlineRequest.bEnabled)
		{
			TArray<AActor*> SelectedInWorld;
			SelectedInWorld.reserve(SelectedActors.size());
			for (AActor* Actor : SelectedActors)
			{
				if (Actor && Actor->GetWorld() == EntryWorld)
				{
					SelectedInWorld.push_back(Actor);
				}
			}
			ScenePass.OutlineRequest.Items = BuildSelectionOutlineItems(SelectedInWorld, Entry.LocalState.ShowFlags);
		}
		ScenePass.DebugInputs.DrawManager = &Engine->GetDebugDrawManager();
		ScenePass.DebugInputs.World = EntryWorld;
		ScenePass.DebugInputs.ShowFlags = Entry.LocalState.ShowFlags;
		ScenePass.DebugInputs.BoundsActor = bCanShowEditorSelection ? SelectedActor : nullptr;

		FrameRequest.ScenePasses.push_back(std::move(ScenePass));
	}

	FrameRequest.CompositeItems.reserve(Entries.size());
	for (const FViewportEntry& Entry : Entries)
	{
		if (!Entry.Viewport)
		{
			continue;
		}

		const FRect& Rect = Entry.Viewport->GetRect();
		FViewportCompositeItem Item;
		Item.Mode = ResolveViewportCompositeMode(Entry.LocalState.ViewMode);
		Item.SceneColorSRV = Entry.Viewport->GetSRV();
		Item.SceneDepthSRV = Entry.Viewport->GetDepthSRV();
		float VisualizationNearPlane = Entry.LocalState.NearPlane;
		float VisualizationFarPlane = Entry.LocalState.FarPlane;
		const bool bIsPIEPossessedView =
			Entry.WorldContext &&
			Entry.WorldContext->WorldType == EWorldType::PIE &&
			EditorEngine->IsPIEActive() &&
			EditorEngine->IsPIEInputCaptured();
		if (bIsPIEPossessedView)
		{
			if (UWorld* EntryWorld = Entry.WorldContext->World)
			{
				if (UCameraComponent* ActiveCameraComponent = EntryWorld->GetActiveCameraComponent())
				{
					VisualizationNearPlane = ActiveCameraComponent->GetNearPlane();
					VisualizationFarPlane = ActiveCameraComponent->GetFarPlane();
				}
			}
		}
		Item.VisualizationParams.NearZ = VisualizationNearPlane;
		Item.VisualizationParams.FarZ = VisualizationFarPlane;
		Item.VisualizationParams.bOrthographic = (Entry.LocalState.ProjectionType == EViewportType::Perspective) ? 0u : 1u;
		Item.Rect.X = Rect.X;
		Item.Rect.Y = Rect.Y;
		Item.Rect.Width = Rect.Width;
		Item.Rect.Height = Rect.Height;
		Item.bVisible = Entry.bActive;
		FrameRequest.CompositeItems.push_back(Item);
	}

	if (FSlateApplication* Slate = EditorEngine->GetSlateApplication())
	{
		FSlatePaintContext PaintContext;

		RECT rc{};
		::GetClientRect(Renderer->GetHwnd(), &rc);
		PaintContext.SetScreenSize(rc.right - rc.left, rc.bottom - rc.top);
		Slate->BuildDrawList(PaintContext);

		FrameRequest.ScreenDrawList = PaintContext.ConsumeDrawList();
	}

	Renderer->RenderEditorFrame(FrameRequest);
	EditorEngine->ClearDebugDrawForFrame();
	EditorUI.Render();
}
