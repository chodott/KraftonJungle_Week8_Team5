#include "EditorViewportClient.h"

#include "EditorEngine.h"
#include "EditorViewportRegistry.h"
#include "UI/EditorUI.h"
#include "Core/Paths.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsCore/RenderStateManager.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "imgui.h"
#include "Viewport.h"

FEditorViewportClient::FEditorViewportClient(
	FEditorEngine& InEditorEngine,
	FEditorUI& InEditorUI,
	FEditorViewportRegistry& InViewportRegistry,
	FWindowsWindow* InMainWindow)
	: EditorUI(InEditorUI)
	, MainWindow(InMainWindow)
	, EditorEngine(InEditorEngine)
	, ViewportRegistry(InViewportRegistry)
{
}

void FEditorViewportClient::Attach(FEngine* Engine, FRenderer* Renderer)
{
	FEditorEngine* EditorEngine = static_cast<FEditorEngine*>(Engine);
	if (!EditorEngine || !Renderer)
	{
		return;
	}

	// ?먮뵒??UI? 酉고룷???뚮뜑留곸뿉 ?꾩슂??怨듭슜 由ъ냼?ㅻ? ???쒖젏??以鍮꾪븳??
	EditorUI.Initialize(EditorEngine);
	EditorUI.InitializeRendererResources(Renderer);
	WireFrameMaterial = FMaterialManager::Get().FindByName(WireframeMaterialName);
	CreateGridResource(Renderer);
	CreateWorldAxisResource(Renderer);
}

void FEditorViewportClient::CreateGridResource(FRenderer* Renderer)
{
	ID3D11Device* Device = Renderer->GetDevice();
	if (Device)
	{
		// ?먮뵒??洹몃━?쒕뒗 ?붾뱶 異뺢낵 遺꾨━???꾩슜 硫붿떆/癒명떚由ъ뼹???ъ슜?쒕떎.
		constexpr int32 GridVertexCount = 6;

		GridMesh = std::make_unique<FDynamicMesh>();
		GridMesh->Topology = EMeshTopology::EMT_TriangleList;
		for (int32 i = 0; i < GridVertexCount; ++i)
		{
			FVertex Vertex;
			GridMesh->Vertices.push_back(Vertex);
			GridMesh->Indices.push_back(i);
		}
		GridMesh->CreateVertexAndIndexBuffer(Device);

		std::wstring ShaderDirW = FPaths::ShaderDir();
		std::wstring VSPath = ShaderDirW + L"EditorWorldOverlay/GridVertexShader.hlsl";
		std::wstring PSPath = ShaderDirW + L"EditorWorldOverlay/GridPixelShader.hlsl";
		auto VS = FShaderMap::Get().GetOrCreateVertexShader(Device, VSPath.c_str(), EVertexLayoutType::MeshVertex);
		auto PS = FShaderMap::Get().GetOrCreatePixelShader(Device, PSPath.c_str());

		GridMaterial = std::make_shared<FMaterial>();
		GridMaterial->SetOriginName("M_EditorGrid");
		GridMaterial->SetVertexShader(VS);
		GridMaterial->SetPixelShader(PS);

		FRasterizerStateOption RasterizerOption;
		RasterizerOption.FillMode = D3D11_FILL_SOLID;
		RasterizerOption.CullMode = D3D11_CULL_NONE;
		RasterizerOption.DepthBias = -10; // ?먮뒗 -1 ~ -100 ?ъ씠 ?쒕떇
		auto RS = Renderer->GetRenderStateManager()->GetOrCreateRasterizerState(RasterizerOption);
		GridMaterial->SetRasterizerOption(RasterizerOption);
		GridMaterial->SetRasterizerState(RS);

		FDepthStencilStateOption DepthStencilOption;
		DepthStencilOption.DepthEnable = true;
		DepthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthStencilOption.DepthFunc = D3D11_COMPARISON_LESS;
		auto DSS = Renderer->GetRenderStateManager()->GetOrCreateDepthStencilState(DepthStencilOption);
		GridMaterial->SetDepthStencilOption(DepthStencilOption);
		GridMaterial->SetDepthStencilState(DSS);

		FBlendStateOption BlendOption;
		BlendOption.BlendEnable = true;
		BlendOption.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		BlendOption.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendOption.BlendOp = D3D11_BLEND_OP_ADD;
		BlendOption.SrcBlendAlpha = D3D11_BLEND_ONE;
		BlendOption.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		BlendOption.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		auto BS = Renderer->GetRenderStateManager()->GetOrCreateBlendState(BlendOption);
		GridMaterial->SetBlendOption(BlendOption);
		GridMaterial->SetBlendState(BS);

		int32 SlotIndex = GridMaterial->CreateConstantBuffer(Device, 64);
		if (SlotIndex >= 0)
		{
			GridMaterial->RegisterParameter("GridSize", SlotIndex, 0, 4);
			GridMaterial->RegisterParameter("LineThickness", SlotIndex, 4, 4);
			GridMaterial->RegisterParameter("GridAxisU", SlotIndex, 16, 12);
			GridMaterial->RegisterParameter("GridAxisV", SlotIndex, 32, 12);
			GridMaterial->RegisterParameter("ViewForward", SlotIndex, 48, 12);

			float DefaultGridSize = 10.0f;
			float DefaultLineThickness = 1.0f;
			const FVector DefaultGridAxisU = FVector::ForwardVector;
			const FVector DefaultGridAxisV = FVector::RightVector;
			const FVector DefaultViewForward = FVector::ForwardVector;
			GridMaterial->SetParameterData("GridSize", &DefaultGridSize, 4);
			GridMaterial->SetParameterData("LineThickness", &DefaultLineThickness, 4);
			GridMaterial->SetParameterData("GridAxisU", &DefaultGridAxisU, sizeof(FVector));
			GridMaterial->SetParameterData("GridAxisV", &DefaultGridAxisV, sizeof(FVector));
			GridMaterial->SetParameterData("ViewForward", &DefaultViewForward, sizeof(FVector));
			for (int32 i = 0; i < MAX_VIEWPORTS; ++i)
			{
				// ?ㅼ젣 洹몃━??諛⑺뼢怨?酉?諛⑺뼢? 酉고룷?몃퀎濡??щ씪吏????덉쑝誘濡??숈쟻 癒명떚由ъ뼹??遺꾨━?쒕떎.
				GridMaterials[i] = GridMaterial->CreateDynamicMaterial();
			}
		}
	}
}

void FEditorViewportClient::CreateWorldAxisResource(FRenderer* Renderer)
{
	ID3D11Device* Device = Renderer->GetDevice();
	if (Device)
	{
		// ?붾뱶 異뺤? 洹몃━?쒖? ?낅┰???꾩슜 硫붿떆/癒명떚由ъ뼹濡??뚮뜑?쒕떎.
		constexpr int32 AxisVertexCount = 36;

		WorldAxisMesh = std::make_unique<FDynamicMesh>();
		WorldAxisMesh->Topology = EMeshTopology::EMT_TriangleList;
		for (int32 i = 0; i < AxisVertexCount; ++i)
		{
			FVertex Vertex;
			WorldAxisMesh->Vertices.push_back(Vertex);
			WorldAxisMesh->Indices.push_back(i);
		}
		WorldAxisMesh->CreateVertexAndIndexBuffer(Device);

		std::wstring ShaderDirW = FPaths::ShaderDir();
		std::wstring VSPath = ShaderDirW + L"EditorScreenOverlay/AxisVertexShader.hlsl";
		std::wstring PSPath = ShaderDirW + L"EditorScreenOverlay/AxisPixelShader.hlsl";
		auto VS = FShaderMap::Get().GetOrCreateVertexShader(Device, VSPath.c_str(), EVertexLayoutType::MeshVertex);
		auto PS = FShaderMap::Get().GetOrCreatePixelShader(Device, PSPath.c_str());

		WorldAxisMaterial = std::make_shared<FMaterial>();
		WorldAxisMaterial->SetOriginName("M_EditorWorldAxis");
		WorldAxisMaterial->SetVertexShader(VS);
		WorldAxisMaterial->SetPixelShader(PS);

		FRasterizerStateOption RasterizerOption;
		RasterizerOption.FillMode = D3D11_FILL_SOLID;
		RasterizerOption.CullMode = D3D11_CULL_NONE;
		auto RS = Renderer->GetRenderStateManager()->GetOrCreateRasterizerState(RasterizerOption);
		WorldAxisMaterial->SetRasterizerOption(RasterizerOption);
		WorldAxisMaterial->SetRasterizerState(RS);

		FDepthStencilStateOption DepthStencilOption;
		DepthStencilOption.DepthEnable = true;
		DepthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		DepthStencilOption.DepthFunc = D3D11_COMPARISON_LESS;
		auto DSS = Renderer->GetRenderStateManager()->GetOrCreateDepthStencilState(DepthStencilOption);
		WorldAxisMaterial->SetDepthStencilOption(DepthStencilOption);
		WorldAxisMaterial->SetDepthStencilState(DSS);

		FBlendStateOption BlendOption;
		BlendOption.BlendEnable = true;
		BlendOption.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		BlendOption.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		BlendOption.BlendOp = D3D11_BLEND_OP_ADD;
		BlendOption.SrcBlendAlpha = D3D11_BLEND_ONE;
		BlendOption.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		BlendOption.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		auto BS = Renderer->GetRenderStateManager()->GetOrCreateBlendState(BlendOption);
		WorldAxisMaterial->SetBlendOption(BlendOption);
		WorldAxisMaterial->SetBlendState(BS);

		int32 SlotIndex = WorldAxisMaterial->CreateConstantBuffer(Device, 64);
		if (SlotIndex >= 0)
		{
			WorldAxisMaterial->RegisterParameter("GridSize", SlotIndex, 0, 4);
			WorldAxisMaterial->RegisterParameter("LineThickness", SlotIndex, 4, 4);
			WorldAxisMaterial->RegisterParameter("GridAxisU", SlotIndex, 16, 12);
			WorldAxisMaterial->RegisterParameter("GridAxisV", SlotIndex, 32, 12);
			WorldAxisMaterial->RegisterParameter("ViewForward", SlotIndex, 48, 12);

			float DefaultGridSize = 10.0f;
			float DefaultLineThickness = 1.0f;
			const FVector DefaultGridAxisU = FVector::ForwardVector;
			const FVector DefaultGridAxisV = FVector::RightVector;
			const FVector DefaultViewForward = FVector::ForwardVector;
			WorldAxisMaterial->SetParameterData("GridSize", &DefaultGridSize, 4);
			WorldAxisMaterial->SetParameterData("LineThickness", &DefaultLineThickness, 4);
			WorldAxisMaterial->SetParameterData("GridAxisU", &DefaultGridAxisU, sizeof(FVector));
			WorldAxisMaterial->SetParameterData("GridAxisV", &DefaultGridAxisV, sizeof(FVector));
			WorldAxisMaterial->SetParameterData("ViewForward", &DefaultViewForward, sizeof(FVector));
			for (int32 i = 0; i < MAX_VIEWPORTS; ++i)
			{
				WorldAxisMaterials[i] = WorldAxisMaterial->CreateDynamicMaterial();
			}
		}
	}
}

void FEditorViewportClient::Detach(FEngine* Engine, FRenderer* Renderer)
{
	// ?쒕옒洹?以묒씤 湲곗쫰紐⑥? ?먮뵒???꾩슜 ?뚮뜑 ?먯썝??紐⑤몢 ?댁젣?쒕떎.
	Gizmo.EndDrag();
	EditorUI.ShutdownRendererResources(Renderer);

	GridMesh.reset();
	GridMaterial.reset();
	WorldAxisMesh.reset();
	WorldAxisMaterial.reset();
	for (int32 i = 0; i < MAX_VIEWPORTS; ++i)
	{
		GridMaterials[i].reset();
		WorldAxisMaterials[i].reset();
	}
}

void FEditorViewportClient::Tick(FEngine* Engine, float DeltaTime)
{
	IViewportClient::Tick(Engine, DeltaTime);
	FEditorEngine* EditorEngine = static_cast<FEditorEngine*>(Engine);
	// 移대찓???대퉬寃뚯씠?섍낵 酉고룷???낅젰 ?곹깭???꾩슜 ?쒕퉬?ㅺ? ?대떦?쒕떎.
	InputService.TickCameraNavigation(Engine, EditorEngine, ViewportRegistry, Gizmo);
}

void FEditorViewportClient::HandleMessage(FEngine* Engine, HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	FEditorEngine* EditorEngine = static_cast<FEditorEngine*>(Engine);
	// ?낅젰 ?쒕퉬?ㅺ? ?쇳궧, 湲곗쫰紐? ?좏깮 媛깆떊源뚯? ??踰덉뿉 泥섎━?쒕떎.
	InputService.HandleMessage(
		Engine,
		EditorEngine,
		Hwnd,
		Msg,
		WParam,
		LParam,
		ViewportRegistry,
		Picker,
		Gizmo,
		[this]()
		{
			EditorUI.SyncSelectedActorProperty();
		});
}

void FEditorViewportClient::HandleFileDoubleClick(const FString& FilePath)
{
	AssetInteractionService.HandleFileDoubleClick(EditorUI, ViewportRegistry, FilePath);
}

void FEditorViewportClient::HandleFileDropOnViewport(const FString& FilePath)
{
	AssetInteractionService.HandleFileDropOnViewport(
		EditorUI,
		Picker,
		ViewportRegistry,
		InputService.GetScreenMouseX(),
		InputService.GetScreenMouseY(),
		FilePath);
}

void FEditorViewportClient::BuildSceneRenderPacket(
	FEngine* Engine,
	UWorld* World,
	const FFrustum& Frustum,
	const FShowFlags& Flags,
	FSceneRenderPacket& OutPacket)
{
	if (!Engine || !World)
	{
		return;
	}
	// ?ㅼ젣 ?섏쭛 濡쒖쭅? 怨듯넻 ViewportClient??ScenePacketBuilder 寃쎈줈瑜??ъ궗?⑺븳??
	IViewportClient::BuildSceneRenderPacket(Engine, World, Frustum, Flags, OutPacket);
}

void FEditorViewportClient::Render(FEngine* Engine, FRenderer* Renderer)
{
	if (!Renderer)
	{
		return;
	}

	// ?뚮뜑 ?꾨쭏??Slate媛 怨꾩궛??酉고룷???ш컖?뺤쓣 ?덉??ㅽ듃由??뷀듃由ъ뿉 諛섏쁺?쒕떎.
	SyncViewportRectsFromDock();
	FEditorEngine* EditorEngine = static_cast<FEditorEngine*>(Engine);
	FMaterial* GridMaterialPtrs[MAX_VIEWPORTS] = {};
	FMaterial* WorldAxisMaterialPtrs[MAX_VIEWPORTS] = {};
	for (int32 i = 0; i < MAX_VIEWPORTS; ++i)
	{
		GridMaterialPtrs[i] = GridMaterials[i].get();
		WorldAxisMaterialPtrs[i] = WorldAxisMaterials[i].get();
	}

	// ?먮뵒???꾨젅??議곕┰怨??ㅼ젣 ?붿껌 ?앹꽦? RenderService媛 ?대떦?쒕떎.
	RenderService.RenderAll(
		Engine,
		Renderer,
		EditorEngine,
		ViewportRegistry,
		EditorUI,
		Gizmo,
		WireFrameMaterial,
		GridMesh.get(),
		GridMaterialPtrs,
		WorldAxisMesh.get(),
		WorldAxisMaterialPtrs,
		[this](FEngine* InEngine, UWorld* World, const FFrustum& Frustum, const FShowFlags& Flags, FSceneRenderPacket& OutPacket)
		{
			BuildSceneRenderPacket(InEngine, World, Frustum, Flags, OutPacket);
		});
}

void FEditorViewportClient::SyncViewportRectsFromDock()
{
	// 以묒븰 dock rect媛 ?덉쑝硫?洹멸쾬???곌퀬, ?놁쑝硫?ImGui 硫붿씤 酉고룷???묒뾽 ?곸뿭??fallback?쇰줈 ?ъ슜?쒕떎.
	FRect Central;
	if (!EditorUI.GetCentralDockRect(Central) || !Central.IsValid())
	{
		if (!ImGui::GetCurrentContext())
		{
			return;
		}
		ImGuiViewport* VP = ImGui::GetMainViewport();
		if (!VP || VP->WorkSize.x <= 0 || VP->WorkSize.y <= 0)
		{
			return;
		}
		Central.X      = static_cast<int32>(VP->WorkPos.x - VP->Pos.x);
		Central.Y      = static_cast<int32>(VP->WorkPos.y - VP->Pos.y);
		Central.Width  = static_cast<int32>(VP->WorkSize.x);
		Central.Height = static_cast<int32>(VP->WorkSize.y);
	}
	
	FSlateApplication* Slate = EditorEngine.GetSlateApplication();
	if (Slate)
	{
		// Slate ?덉씠?꾩썐 寃곌낵瑜?諛뷀깢?쇰줈 ?쒖꽦 酉고룷?몄? 媛?酉고룷???곸뿭??媛깆떊?쒕떎.
		Slate->SetViewportAreaRect(Central);

		for (FViewportEntry& Entry : ViewportRegistry.GetEntries())
		{
			Entry.bActive = Slate->IsViewportActive(Entry.Id);
		}
	}
}
