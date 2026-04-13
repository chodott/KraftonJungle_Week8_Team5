#include "Renderer.h"
#include "ShaderType.h"
#include "Shader.h"
#include "ShaderMap.h"
#include "ShaderResource.h"
#include "Material.h"
#include "MaterialManager.h"
#include "Actor/Actor.h"
#include "Core/Paths.h"
#include "RenderMesh.h"
#include "Component/StaticMeshComponent.h"
#include "Component/DecalComponent.h"
#include "Debug/DebugDrawManager.h"
#include "Renderer/FramePasses.h"
#include "Renderer/FramePipeline.h"
#include "Renderer/Feature/FogRenderFeature.h"
#include "World/World.h"
#include <cassert>
#include <algorithm>
#include <fstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "Asset/ObjManager.h"
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

	FogFeature = std::make_unique<FFogRenderFeature>();
	OutlineFeature = std::make_unique<FOutlineRenderFeature>();
	DebugLineFeature = std::make_unique<FDebugLineRenderFeature>();
	FireBallFeature = std::make_unique<FFireBallRenderFeature>();
	if (!FireBallFeature)
	{
		return false;
	}

	std::filesystem::path FolderIconPath = FPaths::AssetDir() / FString("Textures/FolderIcon.png");
	std::filesystem::path FileIconPath = FPaths::AssetDir() / FString("Textures/FileIcon.png");
	CreateTextureFromSTB(Device, FolderIconPath, &FolderIconSRV);
	CreateTextureFromSTB(Device, FileIconPath, &FileIconSRV);
	if (!CreateSolidColorTextureSRV(Device, 0xFFFFFFFFu, &DecalFallbackBaseColorSRV))
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
	if (!AcquireGameSceneTargets(Targets))
	{
		return false;
	}

	const FFrameContext Frame = BuildFrameContext(Request.SceneView.TotalTimeSeconds);
	const FViewContext View = BuildViewContext(Request.SceneView, RenderDevice.GetViewport());

	FSceneViewData SceneViewData;
	SceneRenderer.BuildSceneViewData(*this, Request.ScenePacket, Frame, View, Request.AdditionalMeshBatches, SceneViewData);
	ResolveDecalBaseColorTextureArray(SceneViewData);
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
		if (!WrapExternalSceneTargets(
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
		ResolveDecalBaseColorTextureArray(SceneViewData);
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

bool FRenderer::EnsureGameSceneTargets(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		return false;
	}

	if (GameSceneColorRTV && GameSceneDepthDSV
		&& GameSceneTargetCacheWidth == Width
		&& GameSceneTargetCacheHeight == Height)
	{
		return EnsureSupplementalTargets(Width, Height);
	}

	ReleaseSceneTargets();
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return false;
	}

	if (!CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &GameSceneColorTexture, &GameSceneColorRTV, &GameSceneColorSRV))
	{
		ReleaseSceneTargets();
		return false;
	}

	if (!CreateDepthTarget(Device, Width, Height, &GameSceneDepthTexture, &GameSceneDepthDSV, &GameSceneDepthSRV))
	{
		ReleaseSceneTargets();
		return false;
	}

	GameSceneTargetCacheWidth = Width;
	GameSceneTargetCacheHeight = Height;
	return EnsureSupplementalTargets(Width, Height);
}

bool FRenderer::EnsureSupplementalTargets(uint32 Width, uint32 Height)
{
	if (Width == 0 || Height == 0)
	{
		return false;
	}

	if (GBufferARTV && GBufferBRTV && GBufferCRTV && SceneColorScratchRTV && OutlineMaskRTV
		&& SupplementalTargetCacheWidth == Width
		&& SupplementalTargetCacheHeight == Height)
	{
		return true;
	}

	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return false;
	}

	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferASRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferARTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferATexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskTexture));

	if (!CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &GBufferATexture, &GBufferARTV, &GBufferASRV)
		|| !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, &GBufferBTexture, &GBufferBRTV, &GBufferBSRV)
		|| !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &GBufferCTexture, &GBufferCRTV, &GBufferCSRV)
		|| !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &SceneColorScratchTexture, &SceneColorScratchRTV, &SceneColorScratchSRV)
		|| !CreateColorRenderTarget(Device, Width, Height, DXGI_FORMAT_R8G8B8A8_UNORM, &OutlineMaskTexture, &OutlineMaskRTV, &OutlineMaskSRV))
	{
		ReleaseSceneTargets();
		return false;
	}

	SupplementalTargetCacheWidth = Width;
	SupplementalTargetCacheHeight = Height;
	return true;
}

bool FRenderer::AcquireGameSceneTargets(FSceneRenderTargets& OutTargets)
{
	const D3D11_VIEWPORT& Viewport = RenderDevice.GetViewport();
	if (!EnsureGameSceneTargets(static_cast<uint32>(Viewport.Width), static_cast<uint32>(Viewport.Height)))
	{
		return false;
	}

	OutTargets = {};
	OutTargets.Width = static_cast<uint32>(Viewport.Width);
	OutTargets.Height = static_cast<uint32>(Viewport.Height);
	OutTargets.SceneColorTexture = GameSceneColorTexture;
	OutTargets.SceneColorRTV = GameSceneColorRTV;
	OutTargets.SceneColorSRV = GameSceneColorSRV;
	OutTargets.SceneColorScratchTexture = SceneColorScratchTexture;
	OutTargets.SceneColorScratchRTV = SceneColorScratchRTV;
	OutTargets.SceneColorScratchSRV = SceneColorScratchSRV;
	OutTargets.SceneDepthTexture = GameSceneDepthTexture;
	OutTargets.SceneDepthDSV = GameSceneDepthDSV;
	OutTargets.SceneDepthSRV = GameSceneDepthSRV;
	OutTargets.GBufferATexture = GBufferATexture;
	OutTargets.GBufferARTV = GBufferARTV;
	OutTargets.GBufferASRV = GBufferASRV;
	OutTargets.GBufferBTexture = GBufferBTexture;
	OutTargets.GBufferBRTV = GBufferBRTV;
	OutTargets.GBufferBSRV = GBufferBSRV;
	OutTargets.GBufferCTexture = GBufferCTexture;
	OutTargets.GBufferCRTV = GBufferCRTV;
	OutTargets.GBufferCSRV = GBufferCSRV;
	OutTargets.OutlineMaskTexture = OutlineMaskTexture;
	OutTargets.OutlineMaskRTV = OutlineMaskRTV;
	OutTargets.OutlineMaskSRV = OutlineMaskSRV;
	return true;
}

bool FRenderer::WrapExternalSceneTargets(
	ID3D11RenderTargetView* RenderTargetView,
	ID3D11ShaderResourceView* RenderTargetShaderResourceView,
	ID3D11DepthStencilView* DepthStencilView,
	ID3D11ShaderResourceView* DepthShaderResourceView,
	const D3D11_VIEWPORT& Viewport,
	FSceneRenderTargets& OutTargets)
{
	const uint32 Width = static_cast<uint32>(Viewport.Width);
	const uint32 Height = static_cast<uint32>(Viewport.Height);
	if (!RenderTargetView || !RenderTargetShaderResourceView || !DepthStencilView || !DepthShaderResourceView || !EnsureSupplementalTargets(Width, Height))
	{
		return false;
	}

	OutTargets = {};
	OutTargets.Width = Width;
	OutTargets.Height = Height;
	OutTargets.SceneColorRTV = RenderTargetView;
	OutTargets.SceneColorSRV = RenderTargetShaderResourceView;
	OutTargets.SceneColorScratchTexture = SceneColorScratchTexture;
	OutTargets.SceneColorScratchRTV = SceneColorScratchRTV;
	OutTargets.SceneColorScratchSRV = SceneColorScratchSRV;
	OutTargets.SceneDepthDSV = DepthStencilView;
	OutTargets.SceneDepthSRV = DepthShaderResourceView;
	OutTargets.GBufferATexture = GBufferATexture;
	OutTargets.GBufferARTV = GBufferARTV;
	OutTargets.GBufferASRV = GBufferASRV;
	OutTargets.GBufferBTexture = GBufferBTexture;
	OutTargets.GBufferBRTV = GBufferBRTV;
	OutTargets.GBufferBSRV = GBufferBSRV;
	OutTargets.GBufferCTexture = GBufferCTexture;
	OutTargets.GBufferCRTV = GBufferCRTV;
	OutTargets.GBufferCSRV = GBufferCSRV;
	OutTargets.OutlineMaskTexture = OutlineMaskTexture;
	OutTargets.OutlineMaskRTV = OutlineMaskRTV;
	OutTargets.OutlineMaskSRV = OutlineMaskSRV;
	return true;
}

void FRenderer::ReleaseSceneTargets()
{
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneColorSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneColorRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneColorTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneDepthSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneDepthDSV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GameSceneDepthTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferASRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferARTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferATexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferBTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(GBufferCTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(SceneColorScratchTexture));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskSRV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskRTV));
	ReleaseCOM(reinterpret_cast<IUnknown*&>(OutlineMaskTexture));
	GameSceneTargetCacheWidth = 0;
	GameSceneTargetCacheHeight = 0;
	SupplementalTargetCacheWidth = 0;
	SupplementalTargetCacheHeight = 0;
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

bool FRenderer::CreateSolidColorTextureSRV(ID3D11Device* Device, uint32 PackedRGBA, ID3D11ShaderResourceView** OutSRV)
{
	if (Device == nullptr || OutSRV == nullptr)
	{
		return false;
	}

	*OutSRV = nullptr;

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	const D3D11_SUBRESOURCE_DATA InitData = { &PackedRGBA, sizeof(PackedRGBA), 0 };

	ID3D11Texture2D* Texture = nullptr;
	if (FAILED(Device->CreateTexture2D(&Desc, &InitData, &Texture)) || !Texture)
	{
		return false;
	}

	const HRESULT Hr = Device->CreateShaderResourceView(Texture, nullptr, OutSRV);
	Texture->Release();
	return SUCCEEDED(Hr);
}

ID3D11ShaderResourceView* FRenderer::GetOrLoadDecalBaseColorTexture(const std::wstring& TexturePath)
{
	if (TexturePath.empty())
	{
		return DecalFallbackBaseColorSRV;
	}

	const std::wstring NormalizedPath = std::filesystem::path(TexturePath).lexically_normal().wstring();
	auto Found = DecalBaseColorTextureCache.find(NormalizedPath);
	if (Found != DecalBaseColorTextureCache.end())
	{
		return Found->second;
	}

	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return DecalFallbackBaseColorSRV;
	}

	ID3D11ShaderResourceView* LoadedSRV = nullptr;
	if (!CreateTextureFromSTB(Device, std::filesystem::path(NormalizedPath), &LoadedSRV) || !LoadedSRV)
	{
		return DecalFallbackBaseColorSRV;
	}

	DecalBaseColorTextureCache.emplace(NormalizedPath, LoadedSRV);
	return LoadedSRV;
}

ID3D11ShaderResourceView* FRenderer::ResolveDecalBaseColorTexture(
	const FSceneRenderPacket& ScenePacket,
	std::wstring& OutResolvedTexturePath)
{
	OutResolvedTexturePath.clear();

	for (const FSceneDecalPrimitive& Primitive : ScenePacket.DecalPrimitives)
	{
		const UDecalComponent* DecalComponent = Primitive.Component;
		if (!DecalComponent || !DecalComponent->IsEnabled())
		{
			continue;
		}

		const std::wstring& TexturePath = DecalComponent->GetTexturePath();
		if (TexturePath.empty())
		{
			continue;
		}

		ID3D11ShaderResourceView* TextureSRV = GetOrLoadDecalBaseColorTexture(TexturePath);
		if (TextureSRV && TextureSRV != DecalFallbackBaseColorSRV)
		{
			OutResolvedTexturePath = std::filesystem::path(TexturePath).lexically_normal().wstring();
			return TextureSRV;
		}
	}

	return DecalFallbackBaseColorSRV;
}

void FRenderer::ResolveDecalBaseColorTextureArray(FSceneViewData& InOutSceneViewData)
{
	auto& DecalItems = InOutSceneViewData.PostProcessInputs.DecalItems;

	// --- Step 1: path dedup, assign TextureIndex per item ---
	// Slot 0 is always the fallback (white).
	TArray<std::wstring> SlicePaths;
	SlicePaths.push_back(L""); // slot 0 = fallback
	TMap<std::wstring, uint32> PathToSlice;
	PathToSlice.emplace(L"", 0u);

	for (FDecalRenderItem& Item : DecalItems)
	{
		Item.TextureIndex = 0;
		if (Item.TexturePath.empty())
		{
			continue;
		}

		const std::wstring NormalizedPath =
			std::filesystem::path(Item.TexturePath).lexically_normal().wstring();

		auto Found = PathToSlice.find(NormalizedPath);
		if (Found != PathToSlice.end())
		{
			Item.TextureIndex = Found->second;
			continue;
		}

		if (SlicePaths.size() >= DECAL_MAX_TEXTURE_SLICES)
		{
			UE_LOG("[Decal] Texture array overflow (%u slices). Using fallback for %ls",
				static_cast<uint32>(DECAL_MAX_TEXTURE_SLICES), NormalizedPath.c_str());
			PathToSlice.emplace(NormalizedPath, 0u);
			Item.TextureIndex = 0;
			continue;
		}

		const uint32 NewIndex = static_cast<uint32>(SlicePaths.size());
		SlicePaths.push_back(NormalizedPath);
		PathToSlice.emplace(NormalizedPath, NewIndex);
		Item.TextureIndex = NewIndex;
	}

	// --- Step 2: early-out if path set is unchanged ---
	if (SlicePaths == DecalBaseColorTextureArrayPaths && DecalBaseColorTextureArraySRV)
	{
		InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = DecalBaseColorTextureArraySRV;
		return;
	}

	// --- Step 3: release stale resources ---
	if (DecalBaseColorTextureArraySRV)
	{
		DecalBaseColorTextureArraySRV->Release();
		DecalBaseColorTextureArraySRV = nullptr;
	}
	if (DecalBaseColorTextureArrayResource)
	{
		DecalBaseColorTextureArrayResource->Release();
		DecalBaseColorTextureArrayResource = nullptr;
	}
	DecalBaseColorTextureArrayPaths = SlicePaths;

	// --- Step 4: load raw pixels for each slice ---
	struct FSlicePixels
	{
		std::vector<unsigned char> Pixels;
		uint32 W = 0;
		uint32 H = 0;
	};

	const uint32 ArraySize = static_cast<uint32>(SlicePaths.size());
	std::vector<FSlicePixels> Slices(ArraySize);

	// Slot 0: 1x1 white fallback (will be resized to canonical size later)
	Slices[0].W = 1; Slices[0].H = 1;
	Slices[0].Pixels = { 255, 255, 255, 255 };

	uint32 CanonicalW = 0;
	uint32 CanonicalH = 0;

	for (uint32 i = 1; i < ArraySize; ++i)
	{
		const std::wstring& Path = SlicePaths[i];
		std::ifstream File(Path, std::ios::binary | std::ios::ate);
		if (!File.is_open())
		{
			UE_LOG("[Decal] Cannot open texture for array: %ls. Using fallback.", Path.c_str());
			continue; // pixels remain empty — will be replaced with canonical fallback below
		}

		const std::streamsize FileSize = File.tellg();
		File.seekg(0, std::ios::beg);
		std::vector<unsigned char> FileBytes(static_cast<size_t>(FileSize));
		if (!File.read(reinterpret_cast<char*>(FileBytes.data()), FileSize))
		{
			UE_LOG("[Decal] Cannot read texture for array: %ls. Using fallback.", Path.c_str());
			continue;
		}

		int W = 0, H = 0, C = 0;
		unsigned char* Data = stbi_load_from_memory(
			FileBytes.data(), static_cast<int>(FileBytes.size()), &W, &H, &C, 4);
		if (!Data)
		{
			UE_LOG("[Decal] STB decode failed for: %ls. Using fallback.", Path.c_str());
			continue;
		}

		if (CanonicalW == 0)
		{
			CanonicalW = static_cast<uint32>(W);
			CanonicalH = static_cast<uint32>(H);
		}

		if (static_cast<uint32>(W) == CanonicalW && static_cast<uint32>(H) == CanonicalH)
		{
			Slices[i].W = CanonicalW;
			Slices[i].H = CanonicalH;
			Slices[i].Pixels.assign(Data, Data + W * H * 4);
		}
		else
		{
			UE_LOG("[Decal] Size mismatch for %ls (%dx%d vs canonical %dx%d). Using fallback.",
				Path.c_str(), W, H, CanonicalW, CanonicalH);
		}
		stbi_image_free(Data);
	}

	// Resolve canonical size (fallback to 1x1 if no real textures)
	if (CanonicalW == 0) { CanonicalW = 1; CanonicalH = 1; }

	// Fill all empty/mismatched slices with canonical-sized white pixels
	const std::vector<unsigned char> WhitePixels(CanonicalW * CanonicalH * 4, 255u);
	for (uint32 i = 0; i < ArraySize; ++i)
	{
		if (Slices[i].W != CanonicalW || Slices[i].H != CanonicalH)
		{
			Slices[i].W = CanonicalW;
			Slices[i].H = CanonicalH;
			Slices[i].Pixels = WhitePixels;
		}
	}

	// --- Step 5: create ID3D11Texture2D array ---
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return;
	}

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width = CanonicalW;
	Desc.Height = CanonicalH;
	Desc.MipLevels = 1;
	Desc.ArraySize = ArraySize;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	std::vector<D3D11_SUBRESOURCE_DATA> InitData(ArraySize);
	for (uint32 i = 0; i < ArraySize; ++i)
	{
		InitData[i].pSysMem = Slices[i].Pixels.data();
		InitData[i].SysMemPitch = CanonicalW * 4;
		InitData[i].SysMemSlicePitch = 0;
	}

	if (FAILED(Device->CreateTexture2D(&Desc, InitData.data(), &DecalBaseColorTextureArrayResource))
		|| !DecalBaseColorTextureArrayResource)
	{
		UE_LOG("[Decal] Failed to create Texture2DArray (%u slices, %ux%u).", ArraySize, CanonicalW, CanonicalH);
		InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = nullptr;
		return;
	}

	// --- Step 6: create SRV ---
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.MostDetailedMip = 0;
	SRVDesc.Texture2DArray.MipLevels = 1;
	SRVDesc.Texture2DArray.FirstArraySlice = 0;
	SRVDesc.Texture2DArray.ArraySize = ArraySize;

	if (FAILED(Device->CreateShaderResourceView(
		DecalBaseColorTextureArrayResource, &SRVDesc, &DecalBaseColorTextureArraySRV))
		|| !DecalBaseColorTextureArraySRV)
	{
		UE_LOG("[Decal] Failed to create Texture2DArray SRV.");
		InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = nullptr;
		return;
	}

	InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = DecalBaseColorTextureArraySRV;
}

void FRenderer::Release()
{
	ReleaseSceneTargets();
	ViewportCompositor.Release();
	if (FogFeature) FogFeature->Release();
	if (OutlineFeature) OutlineFeature->Release();
	if (DebugLineFeature) DebugLineFeature->Release();
	if (TextFeature) TextFeature->Release();
	if (SubUVFeature) SubUVFeature->Release();
	if (BillboardFeature) BillboardFeature->Release();
	if (DecalFeature) DecalFeature->Release();
	if (FireBallFeature) FireBallFeature->Release();
	OutlineFeature.reset();
	DebugLineFeature.reset();
	FogFeature.reset();
	TextFeature.reset();
	SubUVFeature.reset();
	BillboardFeature.reset();
	DecalFeature.reset();
	FireBallFeature.reset();
	ShaderManager.Release(); FShaderMap::Get().Clear(); FMaterialManager::Get().Clear();
	if (NormalSampler) { NormalSampler->Release(); NormalSampler = nullptr; }
	DefaultMaterial.reset();
	DefaultTextureMaterial.reset();
	for (auto& Entry : DecalBaseColorTextureCache)
	{
		if (Entry.second)
		{
			Entry.second->Release();
		}
	}
	DecalBaseColorTextureCache.clear();
	if (DecalBaseColorTextureArraySRV) { DecalBaseColorTextureArraySRV->Release(); DecalBaseColorTextureArraySRV = nullptr; }
	if (DecalBaseColorTextureArrayResource) { DecalBaseColorTextureArrayResource->Release(); DecalBaseColorTextureArrayResource = nullptr; }
	DecalBaseColorTextureArrayPaths.clear();
	if (DecalFallbackBaseColorSRV) { DecalFallbackBaseColorSRV->Release(); DecalFallbackBaseColorSRV = nullptr; }
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
	ReleaseSceneTargets();
	RenderDevice.OnResize(W, H);
}
