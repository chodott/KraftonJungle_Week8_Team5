#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderType.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Resources/Shader/ShaderResource.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Actor/Actor.h"
#include "Core/Paths.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Component/StaticMeshComponent.h"
#include "Component/DecalComponent.h"
#include "Debug/DebugDrawManager.h"
#include "Renderer/UI/FramePasses.h"
#include "Renderer/UI/FramePipeline.h"
#include "Renderer/Common/SceneTargetManager.h"
#include "Renderer/Features/Decal/DecalTextureCache.h"
#include "Renderer/Features/Fog/FogRenderFeature.h"
#include "Renderer/Features/Outline/OutlineRenderFeature.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"
#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/Features/PostProcess/FXAARenderFeature.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "Renderer/Features/SubUV/SubUVRenderFeature.h"
#include "Renderer/Features/Text/TextRenderFeature.h"
#include "Renderer/Features/Billboard/BillboardRenderFeature.h"
#include "World/World.h"
#include <cassert>
#include <algorithm>
#include <fstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "Asset/ObjManager.h"
#include "Core/ConsoleVariableManager.h"
#include "Core/Engine.h"
#include "Debug/EngineLog.h"
#include "ThirdParty/stb_image.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
	static constexpr uint32 DECAL_MAX_TEXTURE_SLICES = 16;

	void BuildDebugLinePassInputs(const FDebugPrimitiveList& Primitives, FDebugLinePassInputs& OutPassInputs)
	{
		OutPassInputs.Clear();

		for (const FDebugCube& Cube : Primitives.Cubes)
		{
			FDebugLineRenderFeature::AppendCube(OutPassInputs, Cube.Center, Cube.Extent, Cube.Color);
		}

		for (const FDebugLine& Line : Primitives.Lines)
		{
			FDebugLineRenderFeature::AppendLine(OutPassInputs, Line.Start, Line.End, Line.Color);
		}
	}

	void AppendActorMeshBVHDebug(
		AActor* BoundsActor,
		UWorld* World,
		FDebugPrimitiveList& OutPrimitives)
	{
		if (!BoundsActor || !World)
		{
			return;
		}

		UStaticMeshComponent* MeshComp = nullptr;
		for (UActorComponent* Comp : BoundsActor->GetComponents())
		{
			if (Comp && Comp->IsA(UStaticMeshComponent::StaticClass()))
			{
				MeshComp = static_cast<UStaticMeshComponent*>(Comp);
				break;
			}
		}

		if (!MeshComp)
		{
			return;
		}

		if (ULevel* Scene = World->GetScene())
		{
			Scene->VisitBVHNodesForPrimitive(MeshComp, [&OutPrimitives](const FAABB& Bounds, int32 Depth, bool bIsLeaf)
				{
					(void)Depth;
					const FVector Center = (Bounds.PMin + Bounds.PMax) * 0.5f;
					const FVector Extent = (Bounds.PMax - Bounds.PMin) * 0.5f;
					const FVector4 Color = bIsLeaf
						? FVector4(1.0f, 1.0f, 0.0f, 1.0f)
						: FVector4(0.0f, 1.0f, 0.0f, 1.0f);
					OutPrimitives.Cubes.push_back({ Center, Extent, Color });
				});
		}

		UStaticMesh* StaticMesh = MeshComp->GetStaticMesh();
		if (!StaticMesh)
		{
			return;
		}

		const FMatrix& LocalToWorld = MeshComp->GetWorldTransform();
		StaticMesh->VisitMeshBVHNodes([&OutPrimitives, &LocalToWorld](const FAABB& LocalBounds, int32 Depth, bool bIsLeaf)
			{
				(void)Depth;
				const FVector& PMin = LocalBounds.PMin;
				const FVector& PMax = LocalBounds.PMax;
				const FVector Corners[8] = {
					{ PMin.X, PMin.Y, PMin.Z }, { PMax.X, PMin.Y, PMin.Z },
					{ PMin.X, PMax.Y, PMin.Z }, { PMax.X, PMax.Y, PMin.Z },
					{ PMin.X, PMin.Y, PMax.Z }, { PMax.X, PMin.Y, PMax.Z },
					{ PMin.X, PMax.Y, PMax.Z }, { PMax.X, PMax.Y, PMax.Z },
				};

				FVector WorldMin = LocalToWorld.TransformPosition(Corners[0]);
				FVector WorldMax = WorldMin;
				for (int32 Index = 1; Index < 8; ++Index)
				{
					const FVector W = LocalToWorld.TransformPosition(Corners[Index]);
					WorldMin.X = (W.X < WorldMin.X) ? W.X : WorldMin.X;
					WorldMin.Y = (W.Y < WorldMin.Y) ? W.Y : WorldMin.Y;
					WorldMin.Z = (W.Z < WorldMin.Z) ? W.Z : WorldMin.Z;
					WorldMax.X = (W.X > WorldMax.X) ? W.X : WorldMax.X;
					WorldMax.Y = (W.Y > WorldMax.Y) ? W.Y : WorldMax.Y;
					WorldMax.Z = (W.Z > WorldMax.Z) ? W.Z : WorldMax.Z;
				}

				const FVector Center = (WorldMin + WorldMax) * 0.5f;
				const FVector Extent = (WorldMax - WorldMin) * 0.5f;
				const FVector4 Color = bIsLeaf
					? FVector4(0.0f, 0.5f, 1.0f, 1.0f)
					: FVector4(0.0f, 1.0f, 1.0f, 1.0f);
				OutPrimitives.Cubes.push_back({ Center, Extent, Color });
			});
	}

	void BuildDebugLinePassInputs(const FDebugSceneBuildInputs& Inputs, FDebugLinePassInputs& OutPassInputs)
	{
		OutPassInputs.Clear();
		if (!Inputs.DrawManager || !Inputs.World)
		{
			return;
		}

		FDebugPrimitiveList Primitives;
		Inputs.DrawManager->BuildPrimitiveList(Inputs.ShowFlags, Inputs.World, Primitives);
		if (Inputs.ShowFlags.HasFlag(EEngineShowFlags::SF_DebugDraw))
		{
			AppendActorMeshBVHDebug(Inputs.BoundsActor, Inputs.World, Primitives);
		}

		BuildDebugLinePassInputs(Primitives, OutPassInputs);
	}

	bool CreateColorRenderTarget(
		ID3D11Device* Device,
		uint32 Width,
		uint32 Height,
		DXGI_FORMAT Format,
		ID3D11Texture2D** OutTexture,
		ID3D11RenderTargetView** OutRTV,
		ID3D11ShaderResourceView** OutSRV)
	{
		if (!Device || !OutTexture || !OutRTV || !OutSRV)
		{
			return false;
		}

		*OutTexture = nullptr;
		*OutRTV = nullptr;
		*OutSRV = nullptr;

		D3D11_TEXTURE2D_DESC Desc = {};
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = Format;
		Desc.SampleDesc.Count = 1;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(Device->CreateTexture2D(&Desc, nullptr, OutTexture)) || !*OutTexture)
		{
			return false;
		}

		if (FAILED(Device->CreateRenderTargetView(*OutTexture, nullptr, OutRTV)) || !*OutRTV)
		{
			(*OutTexture)->Release();
			*OutTexture = nullptr;
			return false;
		}

		if (FAILED(Device->CreateShaderResourceView(*OutTexture, nullptr, OutSRV)) || !*OutSRV)
		{
			(*OutRTV)->Release();
			(*OutTexture)->Release();
			*OutRTV = nullptr;
			*OutTexture = nullptr;
			return false;
		}

		return true;
	}

	bool CreateDepthTarget(
		ID3D11Device* Device,
		uint32 Width,
		uint32 Height,
		ID3D11Texture2D** OutTexture,
		ID3D11DepthStencilView** OutDSV,
		ID3D11ShaderResourceView** OutSRV)
	{
		if (!Device || !OutTexture || !OutDSV || !OutSRV)
		{
			return false;
		}

		*OutTexture = nullptr;
		*OutDSV = nullptr;
		*OutSRV = nullptr;

		D3D11_TEXTURE2D_DESC DepthDesc = {};
		DepthDesc.Width = Width;
		DepthDesc.Height = Height;
		DepthDesc.MipLevels = 1;
		DepthDesc.ArraySize = 1;
		DepthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		DepthDesc.SampleDesc.Count = 1;
		DepthDesc.Usage = D3D11_USAGE_DEFAULT;

		if (FAILED(Device->CreateTexture2D(&DepthDesc, nullptr, OutTexture)) || !*OutTexture)
		{
			return false;
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(Device->CreateDepthStencilView(*OutTexture, &DSVDesc, OutDSV)) || !*OutDSV)
		{
			(*OutTexture)->Release();
			*OutTexture = nullptr;
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MipLevels = 1;
		if (FAILED(Device->CreateShaderResourceView(*OutTexture, &SRVDesc, OutSRV)) || !*OutSRV)
		{
			(*OutDSV)->Release();
			(*OutTexture)->Release();
			*OutDSV = nullptr;
			*OutTexture = nullptr;
			return false;
		}

		return true;
	}

	void ReleaseCOM(IUnknown*& Resource)
	{
		if (Resource)
		{
			Resource->Release();
			Resource = nullptr;
		}
	}
}


FBillboardRenderer& FRenderer::GetBillboardRenderer()
{
	return BillboardFeature->GetRenderer();
}

FRenderer::FRenderer(HWND InHwnd, int32 InWidth, int32 InHeight)
{
	Initialize(InHwnd, InWidth, InHeight);
}

FRenderer::~FRenderer()
{
	Release();
}

bool FRenderer::Initialize(HWND InHwnd, int32 Width, int32 Height)
{
	if (!RenderDevice.Initialize(InHwnd, Width, Height)) return false;
	ID3D11Device* Device = RenderDevice.GetDevice();
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!Device || !DeviceContext) return false;

	RenderStateManager = std::make_unique<FRenderStateManager>(Device, DeviceContext);
	RenderStateManager->PrepareCommonStates();

	if (!CreateSamplers()) return false;
	if (!ViewportCompositor.Initialize(Device)) return false;

	if (!CreateConstantBuffers()) return false;
	SetConstantBuffers();

	std::wstring ShaderDirW = FPaths::ShaderDir();
	std::wstring VSPath = ShaderDirW + L"VertexShader.hlsl";
	std::wstring PSPath = ShaderDirW + L"PixelShader.hlsl";

	if (!ShaderManager.LoadVertexShader(Device, VSPath.c_str())) return false;
	if (!ShaderManager.LoadPixelShader(Device, PSPath.c_str())) return false;

	{
		auto VS = FShaderMap::Get().GetOrCreateVertexShader(Device, VSPath.c_str());
		std::wstring ColorPSPath = ShaderDirW + L"ColorPixelShader.hlsl";
		auto PS = FShaderMap::Get().GetOrCreatePixelShader(Device, ColorPSPath.c_str());
		DefaultMaterial = std::make_shared<FMaterial>();
		DefaultMaterial->SetOriginName("M_Default");
		DefaultMaterial->SetVertexShader(VS);
		DefaultMaterial->SetPixelShader(PS);

		FRasterizerStateOption rasterizerOption;
		rasterizerOption.FillMode = D3D11_FILL_SOLID;
		rasterizerOption.CullMode = D3D11_CULL_BACK;
		auto RS = RenderStateManager->GetOrCreateRasterizerState(rasterizerOption);
		DefaultMaterial->SetRasterizerOption(rasterizerOption);
		DefaultMaterial->SetRasterizerState(RS);

		FDepthStencilStateOption depthStencilOption;
		depthStencilOption.DepthEnable = true;
		depthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		auto DSS = RenderStateManager->GetOrCreateDepthStencilState(depthStencilOption);
		DefaultMaterial->SetDepthStencilOption(depthStencilOption);
		DefaultMaterial->SetDepthStencilState(DSS);

		int32 SlotIndex = DefaultMaterial->CreateConstantBuffer(Device, 16);
		if (SlotIndex >= 0)
		{
			DefaultMaterial->RegisterParameter("BaseColor", SlotIndex, 0, 16);
			float White[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			DefaultMaterial->GetConstantBuffer(SlotIndex)->SetData(White, sizeof(White));
		}

		ConfigureMaterialPasses(*DefaultMaterial, false);
		FMaterialManager::Get().Register("M_Default", DefaultMaterial);
	}

	{
		std::wstring TextureVSPath = ShaderDirW + L"TextureVertexShader.hlsl";
		auto VS = FShaderMap::Get().GetOrCreateVertexShader(Device, TextureVSPath.c_str());
		std::wstring TexturePSPath = ShaderDirW + L"TexturePixelShader.hlsl";
		auto PS = FShaderMap::Get().GetOrCreatePixelShader(Device, TexturePSPath.c_str());
		DefaultTextureMaterial = std::make_shared<FMaterial>();
		DefaultTextureMaterial->SetOriginName("M_Default_Texture");
		DefaultTextureMaterial->SetVertexShader(VS);
		DefaultTextureMaterial->SetPixelShader(PS);

		FRasterizerStateOption rasterizerOption;
		rasterizerOption.FillMode = D3D11_FILL_SOLID;
		rasterizerOption.CullMode = D3D11_CULL_BACK;
		auto RS = RenderStateManager->GetOrCreateRasterizerState(rasterizerOption);
		DefaultTextureMaterial->SetRasterizerOption(rasterizerOption);
		DefaultTextureMaterial->SetRasterizerState(RS);

		FDepthStencilStateOption depthStencilOption;
		depthStencilOption.DepthEnable = true;
		depthStencilOption.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		auto DSS = RenderStateManager->GetOrCreateDepthStencilState(depthStencilOption);
		DefaultTextureMaterial->SetDepthStencilOption(depthStencilOption);
		DefaultTextureMaterial->SetDepthStencilState(DSS);

		int32 SlotIndex = DefaultTextureMaterial->CreateConstantBuffer(Device, 32);
		if (SlotIndex >= 0)
		{
			DefaultTextureMaterial->RegisterParameter("BaseColor", SlotIndex, 0, 16);
			float White[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			DefaultTextureMaterial->GetConstantBuffer(SlotIndex)->SetData(White, sizeof(White));

			DefaultTextureMaterial->RegisterParameter("UVScrollSpeed", SlotIndex, 16, 16);
			float DefaultScroll[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			DefaultTextureMaterial->GetConstantBuffer(SlotIndex)->SetData(DefaultScroll, sizeof(DefaultScroll), 16);
		}

		ConfigureMaterialPasses(*DefaultTextureMaterial, true);
		FMaterialManager::Get().Register("M_Default_Texture", DefaultTextureMaterial);
	}

	TextFeature = std::make_unique<FTextRenderFeature>();
	if (!TextFeature || !TextFeature->Initialize(*this))
	{
		return false;
	}

	std::filesystem::path SubUVTexturePath = FPaths::ContentDir() / FString("Textures/SubUVDino.png");
	SubUVFeature = std::make_unique<FSubUVRenderFeature>();
	if (!SubUVFeature || !SubUVFeature->Initialize(*this, SubUVTexturePath.wstring()))
	{
		MessageBox(0, L"SubUVRenderer Initialize Failed.", 0, 0);
		return false;
	}

	BillboardFeature = std::make_unique<FBillboardRenderFeature>();
	if (!BillboardFeature || !BillboardFeature->Initialize(*this))
	{
		return false;
	}

	DecalFeature = std::make_unique<FDecalRenderFeature>();
	if (!DecalFeature)
	{
		return false;
	}

	VolumeDecalFeature = std::make_unique<FVolumeDecalRenderFeature>();
	if (!VolumeDecalFeature)
	{
		return false;
	}

	FogFeature = std::make_unique<FFogRenderFeature>();
	OutlineFeature = std::make_unique<FOutlineRenderFeature>();
	DebugLineFeature = std::make_unique<FDebugLineRenderFeature>();
	FireBallFeature = std::make_unique<FFireBallRenderFeature>();
	if (!FireBallFeature)
	{
		return false;
	}

	FXAAFeature = std::make_unique<FFXAARenderFeature>();
	if (!FXAAFeature)
	{
		return false;
	}

	std::filesystem::path FolderIconPath = FPaths::AssetDir() / FString("Textures/FolderIcon.png");
	std::filesystem::path FileIconPath = FPaths::AssetDir() / FString("Textures/FileIcon.png");
	CreateTextureFromSTB(Device, FolderIconPath, &FolderIconSRV);
	CreateTextureFromSTB(Device, FileIconPath, &FileIconSRV);
	if (!DecalTextureCache.InitializeFallbackTexture(Device))
	{
		return false;
	}

	return true;
}

void FRenderer::SetConstantBuffers()
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	ID3D11Buffer* CBs[2] = { FrameConstantBuffer, ObjectConstantBuffer };
	DeviceContext->VSSetConstantBuffers(0, 2, CBs);
	DeviceContext->PSSetConstantBuffers(0, 2, CBs);
}

void FRenderer::BeginFrame()
{
	constexpr float ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
	RenderDevice.BeginFrame(ClearColor);
	SceneRenderer.BeginFrame();
}

void FRenderer::EndFrame()
{
	RenderDevice.EndFrame();
}

FFrameContext FRenderer::BuildFrameContext(float TotalTimeSeconds) const
{
	FFrameContext Frame;
	Frame.TotalTimeSeconds = TotalTimeSeconds;
	Frame.DeltaTimeSeconds = GEngine ? GEngine->GetDeltaTime() : 0.0f;
	return Frame;
}

FViewContext FRenderer::BuildViewContext(const FSceneViewRenderRequest& SceneView, const D3D11_VIEWPORT& Viewport) const
{
	FViewContext View;
	View.View = SceneView.ViewMatrix;
	View.Projection = SceneView.ProjectionMatrix;
	View.ViewProjection = SceneView.ViewMatrix * SceneView.ProjectionMatrix;
	View.InverseView = SceneView.ViewMatrix.GetInverse();
	View.InverseProjection = SceneView.ProjectionMatrix.GetInverse();
	View.InverseViewProjection = View.ViewProjection.GetInverse();
	View.CameraPosition = SceneView.CameraPosition;
	View.NearZ = SceneView.NearZ;
	View.FarZ = SceneView.FarZ;
	View.Viewport = Viewport;
	return View;
}

bool FRenderer::RenderGameFrame(const FGameFrameRequest& Request)
{
	FSceneRenderTargets Targets;
	if (!SceneTargetManager.AcquireGameSceneTargets(GetDevice(), RenderDevice.GetViewport(), Targets))
	{
		return false;
	}

	const FFrameContext Frame = BuildFrameContext(Request.SceneView.TotalTimeSeconds);
	const FViewContext View = BuildViewContext(Request.SceneView, RenderDevice.GetViewport());

	FSceneViewData SceneViewData;
	SceneRenderer.BuildSceneViewData(*this, Request.ScenePacket, Frame, View, Request.AdditionalMeshBatches, SceneViewData);
	DecalTextureCache.ResolveTextureArray(GetDevice(), SceneViewData);
	BuildDebugLinePassInputs(Request.DebugInputs, SceneViewData.DebugInputs.LinePass);

	if (!SceneRenderer.RenderSceneView(
		*this,
		Targets,
		SceneViewData,
		Request.ClearColor,
		Request.bForceWireframe,
		Request.WireframeMaterial))
	{
		return false;
	}

	FViewportCompositeItem FullscreenItem;
	FullscreenItem.Mode = Request.CompositeMode;
	FullscreenItem.SceneColorSRV = Targets.SceneColorSRV;
	FullscreenItem.SceneDepthSRV = Targets.SceneDepthSRV;
	FullscreenItem.VisualizationParams.NearZ = View.NearZ;
	FullscreenItem.VisualizationParams.FarZ = View.FarZ;
	FullscreenItem.VisualizationParams.bOrthographic = 0u;
	FullscreenItem.Rect.X = 0;
	FullscreenItem.Rect.Y = 0;
	FullscreenItem.Rect.Width = static_cast<int32>(RenderDevice.GetViewport().Width);
	FullscreenItem.Rect.Height = static_cast<int32>(RenderDevice.GetViewport().Height);
	FullscreenItem.bVisible = true;

	TArray<FViewportCompositeItem> CompositeItems;
	CompositeItems.push_back(FullscreenItem);
	const FViewportCompositePassInputs CompositeInputs{ &CompositeItems };

	FViewContext FramePassView;
	FramePassView.Viewport = RenderDevice.GetViewport();
	FFramePassContext FramePassContext{ *this, Frame, FramePassView, RenderDevice.GetRenderTargetView(), nullptr, &CompositeInputs, nullptr };
	FFrameRenderPipeline FramePipeline;
	FramePipeline.AddPass(std::make_unique<FViewportCompositePass>());
	return FramePipeline.Execute(FramePassContext);
}

bool FRenderer::RenderScreenUIPass(
	const FScreenUIPassInputs& PassInputs,
	const FFrameContext& Frame,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView)
{
	return ScreenUIRenderer.Render(
		*this,
		Frame,
		PassInputs,
		RenderTargetView,
		DepthStencilView);
}

bool FRenderer::ComposeViewports(
	const FViewportCompositePassInputs& Inputs,
	const FFrameContext& Frame,
	const FViewContext& View,
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11DepthStencilView* DepthStencilView)
{
	if (!RenderTargetView)
	{
		return false;
	}

	return ViewportCompositor.Compose(
		*this,
		Frame,
		View,
		RenderTargetView,
		DepthStencilView,
		Inputs);
}

bool FRenderer::RenderEditorFrame(const FEditorFrameRequest& Request)
{
	for (const FViewportScenePassRequest& ScenePass : Request.ScenePasses)
	{
		if (!ScenePass.IsValid())
		{
			continue;
		}

		const FFrameContext Frame = BuildFrameContext(ScenePass.SceneView.TotalTimeSeconds);
		const FViewContext View = BuildViewContext(ScenePass.SceneView, ScenePass.Viewport);

		FSceneRenderTargets Targets;
		if (!SceneTargetManager.WrapExternalSceneTargets(
			GetDevice(),
			ScenePass.RenderTargetView,
			ScenePass.RenderTargetShaderResourceView,
			ScenePass.DepthStencilView,
			ScenePass.DepthShaderResourceView,
			ScenePass.Viewport,
			Targets))
		{
			continue;
		}

		FSceneViewData SceneViewData;
		SceneRenderer.BuildSceneViewData(*this, ScenePass.ScenePacket, Frame, View, ScenePass.AdditionalMeshBatches, SceneViewData);
		DecalTextureCache.ResolveTextureArray(GetDevice(), SceneViewData);
		SceneViewData.PostProcessInputs.OutlineItems = ScenePass.OutlineRequest.Items;
		SceneViewData.PostProcessInputs.bOutlineEnabled = ScenePass.OutlineRequest.bEnabled;
		BuildDebugLinePassInputs(ScenePass.DebugInputs, SceneViewData.DebugInputs.LinePass);

		if (!SceneRenderer.RenderSceneView(
			*this,
			Targets,
			SceneViewData,
			ScenePass.ClearColor,
			ScenePass.bForceWireframe,
			ScenePass.WireframeMaterial))
		{
			continue;
		}
	}

	const float FrameTimeSeconds = !Request.ScenePasses.empty()
		? Request.ScenePasses.front().SceneView.TotalTimeSeconds
		: 0.0f;
	const FFrameContext Frame = BuildFrameContext(FrameTimeSeconds);
	FViewContext FramePassView;
	FramePassView.Viewport = RenderDevice.GetViewport();
	const FViewportCompositePassInputs CompositeInputs{ &Request.CompositeItems };
	FScreenUIPassInputs ScreenUIInputs;
	if (!ScreenUIRenderer.BuildPassInputs(*this, Request.ScreenDrawList, RenderDevice.GetViewport(), ScreenUIInputs))
	{
		return false;
	}

	FFramePassContext FramePassContext{ *this, Frame, FramePassView, RenderDevice.GetRenderTargetView(), nullptr, &CompositeInputs, &ScreenUIInputs };
	FFrameRenderPipeline FramePipeline;
	FramePipeline.AddPass(std::make_unique<FViewportCompositePass>());
	FramePipeline.AddPass(std::make_unique<FScreenUIPass>());
	return FramePipeline.Execute(FramePassContext);
}

void FRenderer::ClearDepthBuffer(ID3D11DepthStencilView* DepthStencilView)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext || !DepthStencilView)
	{
		return;
	}

	DeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

const FDecalFrameStats& FRenderer::GetDecalFrameStats() const
{
	static const FDecalFrameStats EmptyStats = {};
	return DecalFeature ? DecalFeature->GetFrameStats() : EmptyStats;
}

FDecalStats FRenderer::GetDecalStats() const
{
	FDecalStats Stats;
	Stats.Common.Mode = GetDecalProjectionMode();

	if (Stats.Common.Mode == EDecalProjectionMode::VolumeDraw)
	{
		if (VolumeDecalFeature)
		{
			Stats.Volume = VolumeDecalFeature->GetStats();
			Stats.Common.TotalDecals = VolumeDecalFeature->GetTotalDecalCount();
			Stats.Common.ActiveDecals = VolumeDecalFeature->GetTotalDecalCount();
			Stats.Common.VisibleDecals = Stats.Volume.DecalDrawCalls;
			Stats.Common.RejectedDecals = Stats.Common.ActiveDecals > Stats.Common.VisibleDecals
				? (Stats.Common.ActiveDecals - Stats.Common.VisibleDecals)
				: 0;
			Stats.Common.FadeInOutDecals = VolumeDecalFeature->GetFadeInOutCount();
			Stats.Common.BuildTimeMs = 0.0;
			Stats.Common.CullIntersectionTimeMs = 0.0;
			Stats.Common.ShadingPassTimeMs = VolumeDecalFeature->GetShadingPassTimeMs();
			Stats.Common.TotalDecalTimeMs = VolumeDecalFeature->GetTotalTimeMs();
		}
		return Stats;
	}

	if (DecalFeature)
	{
		const FDecalFrameStats& FrameStats = DecalFeature->GetFrameStats();
		Stats.ClusteredLookup = DecalFeature->GetClusteredStats();
		Stats.Common.TotalDecals = FrameStats.InputItemCount;
		Stats.Common.ActiveDecals = FrameStats.InputItemCount;
		Stats.Common.VisibleDecals = FrameStats.VisibleItemCount;
		Stats.Common.RejectedDecals = FrameStats.InputItemCount > FrameStats.VisibleItemCount
			? (FrameStats.InputItemCount - FrameStats.VisibleItemCount)
			: 0;
		Stats.Common.BuildTimeMs = FrameStats.VisibleBuildTimeMs;
		Stats.Common.CullIntersectionTimeMs = FrameStats.ClusterBuildTimeMs;
		Stats.Common.ShadingPassTimeMs = FrameStats.ShadingPassTimeMs;
		Stats.Common.TotalDecalTimeMs = FrameStats.TotalDecalTimeMs;
		Stats.Common.FadeInOutDecals = FrameStats.FadeInOutCount;
	}

	return Stats;
}

size_t FRenderer::GetPrevCommandCount() const
{
	return SceneRenderer.GetPrevCommandCount();
}

bool FRenderer::CreateConstantBuffers()
{
	ID3D11Device* Device = RenderDevice.GetDevice();
	if (!Device)
	{
		return false;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	Desc.ByteWidth = sizeof(FFrameConstantBuffer);
	if (FAILED(Device->CreateBuffer(&Desc, nullptr, &FrameConstantBuffer))) return false;

	Desc.ByteWidth = sizeof(FObjectConstantBuffer);
	return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, &ObjectConstantBuffer));
}

bool FRenderer::CreateSamplers()
{
	ID3D11Device* Device = RenderDevice.GetDevice();
	if (!Device)
	{
		return false;
	}

	D3D11_SAMPLER_DESC SamplerDesc = {};
	SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD = 0;
	SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	HRESULT Hr = Device->CreateSamplerState(&SamplerDesc, &NormalSampler);
	if (FAILED(Hr))
	{
		return false;
	}

	return true;
}

void FRenderer::UpdateFrameConstantBuffer(const FFrameContext& Frame, const FViewContext& View)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FFrameConstantBuffer CBData;
	CBData.View = View.View.GetTransposed();
	CBData.Projection = View.Projection.GetTransposed();
	CBData.Time = Frame.TotalTimeSeconds;
	CBData.DeltaTime = Frame.DeltaTimeSeconds;
	D3D11_MAPPED_SUBRESOURCE Mapped;
	if (SUCCEEDED(DeviceContext->Map(FrameConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(FrameConstantBuffer, 0);
	}
}

void FRenderer::UpdateObjectConstantBuffer(const FMatrix& WorldMatrix)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FObjectConstantBuffer CBData;
	CBData.World = WorldMatrix.GetTransposed();
	D3D11_MAPPED_SUBRESOURCE Mapped;
	if (SUCCEEDED(DeviceContext->Map(ObjectConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(ObjectConstantBuffer, 0);
	}
}

void FRenderer::ConfigureMaterialPasses(FMaterial& Material, bool bTexturedMaterial)
{
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return;
	}

	const std::wstring ShaderDir = FPaths::ShaderDir();
	FMaterialPassShaders DepthPass;
	FMaterialPassShaders GBufferPass;
	FMaterialPassShaders OutlineMaskPass;
	if (bTexturedMaterial)
	{
		DepthPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"DepthOnlyTextureVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"TextureVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.PS = FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"GBufferTexturePixelShader.hlsl").c_str());
		OutlineMaskPass.VS = GBufferPass.VS;
		OutlineMaskPass.PS = FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"OutlineMaskTexturePixelShader.hlsl").c_str());
	}
	else
	{
		DepthPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"DepthOnlyVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"VertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.PS = FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"GBufferColorPixelShader.hlsl").c_str());
		OutlineMaskPass.VS = GBufferPass.VS;
		OutlineMaskPass.PS = FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"OutlineMaskPixelShader.hlsl").c_str());
	}

	Material.SetPassShaders(EMaterialPassType::DepthOnly, DepthPass);
	Material.SetPassShaders(EMaterialPassType::GBuffer, GBufferPass);
	Material.SetPassShaders(EMaterialPassType::OutlineMask, OutlineMaskPass);
}

bool FRenderer::CreateTextureFromSTB(ID3D11Device* Device, const char* FilePath, ID3D11ShaderResourceView** OutSRV)
{
	if (FilePath == nullptr)
	{
		return false;
	}

	return CreateTextureFromSTB(Device, FPaths::ToPath(FilePath), OutSRV);
}

bool FRenderer::CreateTextureFromSTB(ID3D11Device* Device, const std::filesystem::path& FilePath, ID3D11ShaderResourceView** OutSRV)
{
	if (Device == nullptr || OutSRV == nullptr || FilePath.empty())
	{
		return false;
	}

	std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
	if (!File.is_open())
	{
		return false;
	}

	const std::streamsize FileSize = File.tellg();
	if (FileSize <= 0)
	{
		return false;
	}

	File.seekg(0, std::ios::beg);
	std::vector<unsigned char> FileBytes(static_cast<size_t>(FileSize));
	if (!File.read(reinterpret_cast<char*>(FileBytes.data()), FileSize))
	{
		return false;
	}

	int W = 0;
	int H = 0;
	int C = 0;
	unsigned char* Data = stbi_load_from_memory(FileBytes.data(), static_cast<int>(FileBytes.size()), &W, &H, &C, 4);
	if (!Data) return false;

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = W; Desc.Height = H; Desc.MipLevels = 1; Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT; Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA InitData = { Data, static_cast<UINT>(W * 4), 0 };
	ID3D11Texture2D* Tex = nullptr;
	HRESULT hr = Device->CreateTexture2D(&Desc, &InitData, &Tex);
	stbi_image_free(Data);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(Tex, nullptr, OutSRV);
	Tex->Release();
	return SUCCEEDED(hr);
}

void FRenderer::Release()
{
	SceneTargetManager.Release();
	ViewportCompositor.Release();
	if (FogFeature) FogFeature->Release();
	if (OutlineFeature) OutlineFeature->Release();
	if (DebugLineFeature) DebugLineFeature->Release();
	if (TextFeature) TextFeature->Release();
	if (SubUVFeature) SubUVFeature->Release();
	if (BillboardFeature) BillboardFeature->Release();
	if (DecalFeature) DecalFeature->Release();
	if (VolumeDecalFeature) VolumeDecalFeature->Release();
	if (FireBallFeature) FireBallFeature->Release();
	if (FXAAFeature) FXAAFeature->Release();
	OutlineFeature.reset();
	DebugLineFeature.reset();
	FogFeature.reset();
	TextFeature.reset();
	SubUVFeature.reset();
	BillboardFeature.reset();
	DecalFeature.reset();
	VolumeDecalFeature.reset();
	FireBallFeature.reset();
	FXAAFeature.reset();
	ShaderManager.Release(); FShaderMap::Get().Clear(); FMaterialManager::Get().Clear();
	if (NormalSampler) { NormalSampler->Release(); NormalSampler = nullptr; }
	DefaultMaterial.reset();
	DefaultTextureMaterial.reset();
	DecalTextureCache.Release();
	if (FolderIconSRV) { FolderIconSRV->Release(); FolderIconSRV = nullptr; }
	if (FileIconSRV) { FileIconSRV->Release(); FileIconSRV = nullptr; }
	if (FrameConstantBuffer) { FrameConstantBuffer->Release(); FrameConstantBuffer = nullptr; }
	if (ObjectConstantBuffer) { ObjectConstantBuffer->Release(); ObjectConstantBuffer = nullptr; }
	RenderDevice.Release();
}

bool FRenderer::IsOccluded()
{
	return RenderDevice.IsOccluded();
}

void FRenderer::OnResize(int32 W, int32 H)
{
	if (W == 0 || H == 0) return;
	SceneTargetManager.Release();
	RenderDevice.OnResize(W, H);
}
