#include "Viewport/Services/EditorViewportRenderService.h"

#include "EditorEngine.h"
#include "Viewport/EditorViewportRegistry.h"
#include "Actor/Actor.h"
#include "Core/Engine.h"
#include "Gizmo/Gizmo.h"
#include "Math/Frustum.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Renderer.h"
#include "Level/Level.h"
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

	TArray<FOutlineRenderItem> BuildSelectionOutlineItems(AActor* Selected)
	{
		TArray<FOutlineRenderItem> OutlineItems;
		if (!Selected || Selected->IsPendingDestroy() || !Selected->IsVisible())
		{
			return OutlineItems;
		}

		if (Selected->GetComponentByClass<USkyComponent>() != nullptr)
		{
			return OutlineItems;
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
					Item.WorldMatrix = PrimitiveComponent->GetWorldTransform();
					Item.IndexStart = Section.StartIndex;
					Item.IndexCount = Section.IndexCount;
				}
				continue;
			}

			FOutlineRenderItem& Item = OutlineItems.emplace_back();
			Item.Mesh = RenderMesh;
			Item.Material = MeshComponent ? MeshComponent->GetMaterial(0).get() : nullptr;
			Item.WorldMatrix = PrimitiveComponent->GetWorldTransform();
		}

		return OutlineItems;
	}

	static EViewportCompositeMode ResolveViewportCompositeMode(const FShowFlags& ShowFlags)
	{
		if (ShowFlags.HasFlag(EEngineShowFlags::SF_DepthView))
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
		const bool bCanShowEditorSelection = bIsEditorWorld && SelectedActor && SelectedActor->GetWorld() == EntryWorld;

		FSceneRenderPacket ScenePacket;
		// 씬 패킷과 별도로, 그리드/기즈모 같은 추가 씬 커맨드는 별도 큐로 유지한다.
		TArray<FMeshBatch> AdditionalMeshBatches;
		AdditionalMeshBatches.reserve(Renderer->GetPrevCommandCount());
		const FMatrix ViewMatrix = Entry.LocalState.BuildViewMatrix();
		const FMatrix ProjectionMatrix = Entry.LocalState.BuildProjMatrix(AspectRatio);

		FFrustum Frustum;
		Frustum.ExtractFromVP(ViewMatrix * ProjectionMatrix);
		const FVector CameraPosition = ViewMatrix.GetInverse().GetTranslation();
		BuildSceneRenderPacket(Engine, EntryWorld, Frustum, Entry.LocalState.ShowFlags, ScenePacket);

		if (bCanShowEditorSelection && SelectedActor->GetComponentByClass<USkyComponent>() == nullptr)
		{
			Gizmo.BuildMeshBatches(SelectedActor, &Entry, AdditionalMeshBatches);
		}

		FMaterial* EntryGridMaterial = (CurrentEntryIndex < MAX_VIEWPORTS) ? GridMaterials[CurrentEntryIndex] : nullptr;
		if (Entry.LocalState.bShowGrid && GridMesh && EntryGridMaterial)
		{
			FVector GridAxisU = FVector::ForwardVector;
			FVector GridAxisV = FVector::RightVector;
			FVector ViewForward = FVector::ForwardVector;
			BuildGridVectors(ViewMatrix, Entry.LocalState, GridAxisU, GridAxisV, ViewForward);

			EntryGridMaterial->SetParameterData("GridSize", &Entry.LocalState.GridSize, 4);
			EntryGridMaterial->SetParameterData("LineThickness", &Entry.LocalState.LineThickness, 4);
			EntryGridMaterial->SetParameterData("GridAxisU", &GridAxisU, sizeof(FVector));
			EntryGridMaterial->SetParameterData("GridAxisV", &GridAxisV, sizeof(FVector));
			EntryGridMaterial->SetParameterData("ViewForward", &ViewForward, sizeof(FVector));

			FMeshBatch GridBatch;
			GridBatch.Mesh = GridMesh;
			GridBatch.Material = EntryGridMaterial;
			GridBatch.World = FMatrix::Identity;
			GridBatch.Domain = EMaterialDomain::Opaque;
			GridBatch.PassMask = static_cast<uint32>(EMeshPassMask::ForwardOpaque);
			AdditionalMeshBatches.push_back(GridBatch);
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
		ScenePass.SceneView.NearZ = Entry.LocalState.NearPlane;
		ScenePass.SceneView.FarZ = Entry.LocalState.FarPlane;
		ScenePass.SceneView.TotalTimeSeconds = Engine ? static_cast<float>(Engine->GetTimer().GetTotalTime()) : 0.0f;
		ScenePass.AdditionalMeshBatches = std::move(AdditionalMeshBatches);
		ScenePass.bForceWireframe = (Entry.LocalState.ViewMode == ERenderMode::Wireframe && WireFrameMaterial != nullptr);
		ScenePass.WireframeMaterial = WireFrameMaterial.get();
		ScenePass.OutlineRequest.bEnabled =
			bCanShowEditorSelection &&
			Entry.LocalState.ShowFlags.HasFlag(EEngineShowFlags::SF_Primitives);
		if (ScenePass.OutlineRequest.bEnabled)
		{
			ScenePass.OutlineRequest.Items = BuildSelectionOutlineItems(SelectedActor);
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
		Item.Mode = ResolveViewportCompositeMode(Entry.LocalState.ShowFlags);
		Item.SceneColorSRV = Entry.Viewport->GetSRV();
		Item.SceneDepthSRV = Entry.Viewport->GetDepthSRV();
		Item.VisualizationParams.NearZ = Entry.LocalState.NearPlane;
		Item.VisualizationParams.FarZ = Entry.LocalState.FarPlane;
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
