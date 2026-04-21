#include "Renderer/Renderer.h"
#include "Actor/Actor.h"
#include "Component/DecalComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/UUIDBillboardComponent.h"
#include "Core/Paths.h"
#include "Debug/DebugDrawManager.h"
#include "Renderer/Features/Billboard/BillboardRenderFeature.h"
#include "Renderer/Features/Debug/DebugLineRenderFeature.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Decal/DecalTextureCache.h"
#include "Renderer/Features/Decal/VolumeDecalRenderFeature.h"
#include "Renderer/Features/FireBall/FireBallRenderFeature.h"
#include "Renderer/Features/Fog/FogRenderFeature.h"
#include "Renderer/Features/Outline/OutlineRenderFeature.h"
#include "Renderer/Features/PostProcess/FXAARenderFeature.h"
#include "Renderer/Features/SubUV/SubUVRenderFeature.h"
#include "Renderer/Features/Text/TextRenderFeature.h"
#include "Renderer/Features/Lighting/LightRenderFeature.h"
#include "Renderer/Frame/EditorFrameRenderer.h"
#include "Renderer/Frame/GameFrameRenderer.h"
#include "Renderer/Frame/RendererResourceBootstrap.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Renderer/Frame/Viewport/ViewportCompositor.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"
#include "Renderer/Resources/Shader/ShaderResource.h"
#include "Renderer/Resources/Shader/ShaderType.h"
#include "Renderer/Scene/Builders/DebugSceneBuilder.h"
#include "Renderer/Scene/MeshPassProcessor.h"
#include "Renderer/Scene/Passes/PassContext.h"
#include "Renderer/Scene/SceneRenderer.h"
#include "Renderer/UI/Screen/ScreenUIRenderer.h"
#include "World/World.h"
#include <algorithm>
#include <cassert>
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
	struct FToneMappingConstants
	{
		float Exposure = 1.0f;
		float ShoulderStrength = 0.0f;
		float LinearWhite = 11.2f;
		float Pad0 = 0.0f;
	};

	ID3D11Texture2D* ResolveTextureFromRenderTarget(ID3D11RenderTargetView* RenderTargetView)
	{
		if (!RenderTargetView)
		{
			return nullptr;
		}

		ID3D11Resource* Resource = nullptr;
		RenderTargetView->GetResource(&Resource);
		if (!Resource)
		{
			return nullptr;
		}

		ID3D11Texture2D* Texture = nullptr;
		const HRESULT    Hr      = Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture));
		Resource->Release();
		return SUCCEEDED(Hr) ? Texture : nullptr;
	}
}

FRenderer::FRenderer(HWND InHwnd, int32 InWidth, int32 InHeight)
	: SceneRenderer(std::make_unique<FSceneRenderer>()), ViewportCompositor(std::make_unique<FViewportCompositor>()),
	  ScreenUIRenderer(std::make_unique<FScreenUIRenderer>()),
	  SceneTargetManager(std::make_unique<FSceneTargetManager>()),
	  DecalTextureCache(std::make_unique<FDecalTextureCache>())
{
	Initialize(InHwnd, InWidth, InHeight);
}

FRenderer::~FRenderer()
{
	Release();
}

bool FRenderer::Initialize(HWND InHwnd, int32 Width, int32 Height)
{
	if (!RenderDevice.Initialize(InHwnd, Width, Height))
	{
		return false;
	}
	ID3D11Device*        Device        = RenderDevice.GetDevice();
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!Device || !DeviceContext)
	{
		return false;
	}

	RenderStateManager = std::make_unique<FRenderStateManager>(Device, DeviceContext);
	RenderStateManager->PrepareCommonStates();

	if (!CreateSamplers())
	{
		return false;
	}
	if (!ViewportCompositor || !ViewportCompositor->Initialize(Device))
	{
		return false;
	}

	if (!CreateConstantBuffers())
	{
		return false;
	}
	SetConstantBuffers();

	if (!FRendererResourceBootstrap::Initialize(*this))
	{
		return false;
	}

	if (!ShaderHotReloadService.Initialize(FPaths::ShaderDir().wstring()))
	{
		OutputDebugStringA("[ShaderHotReload] watcher init failed.\n");
	}

	return true;
}

void FRenderer::BeginFrame()
{
	constexpr float ClearColor[4] = { 0.01f, 0.01f, 0.01f, 1.0f };
	RenderDevice.BeginFrame(ClearColor);
	if (SceneRenderer)
	{
		SceneRenderer->BeginFrame();
	}
}

void FRenderer::EndFrame()
{
	RenderDevice.EndFrame();
}

void FRenderer::Release()
{
	ShaderHotReloadService.Shutdown();

	if (SceneTargetManager)
	{
		SceneTargetManager->Release();
	}
	if (ViewportCompositor)
	{
		ViewportCompositor->Release();
	}

	if (ToneMappingConstantBuffer)
	{
		ToneMappingConstantBuffer->Release();
		ToneMappingConstantBuffer = nullptr;
	}
	if (FullscreenRasterizerState)
	{
		FullscreenRasterizerState->Release();
		FullscreenRasterizerState = nullptr;
	}
	if (FullscreenNoDepthState)
	{
		FullscreenNoDepthState->Release();
		FullscreenNoDepthState = nullptr;
	}
	if (FullscreenPointSampler)
	{
		FullscreenPointSampler->Release();
		FullscreenPointSampler = nullptr;
	}
	FinalImageVertexShader.reset();
	FinalImageBlitPixelShader.reset();
	ToneMappingPixelShader.reset();
	FRendererResourceBootstrap::Release(*this);
	RenderDevice.Release();
}

void FRenderer::TickShaderHotReload(float DeltaTime)
{
	ShaderHotReloadService.Tick(*this, DeltaTime);
}

bool FRenderer::IsOccluded()
{
	return RenderDevice.IsOccluded();
}

void FRenderer::OnResize(int32 W, int32 H)
{
	if (W == 0 || H == 0)
	{
		return;
	}
	if (SceneTargetManager)
	{
		SceneTargetManager->Release();
	}
	RenderDevice.OnResize(W, H);
}

bool FRenderer::RenderScreenUIPass(const FScreenUIPassInputs& PassInputs,
                                   const FFrameContext&       Frame,
                                   ID3D11RenderTargetView*    RenderTargetView,
                                   ID3D11DepthStencilView*    DepthStencilView)
{
	return ScreenUIRenderer && ScreenUIRenderer->Render(*this, Frame, PassInputs, RenderTargetView, DepthStencilView);
}

bool FRenderer::ComposeViewports(const FViewportCompositePassInputs& Inputs,
                                 const FFrameContext&                Frame,
                                 const FViewContext&                 View,
                                 ID3D11RenderTargetView*             RenderTargetView,
                                 ID3D11DepthStencilView*             DepthStencilView)
{
	if (!RenderTargetView)
	{
		return false;
	}

	return ViewportCompositor &&
			ViewportCompositor->Compose(*this, Frame, View, RenderTargetView, DepthStencilView, Inputs);
}

bool FRenderer::RenderGameFrame(const FGameFrameRequest& Request)
{
	return FGameFrameRenderer::Render(*this, Request);
}

bool FRenderer::RenderEditorFrame(const FEditorFrameRequest& Request)
{
	return FEditorFrameRenderer::Render(*this, Request);
}

bool FRenderer::CreateTextureFromSTB(
	ID3D11Device*              Device,
	const char*                FilePath,
	ID3D11ShaderResourceView** OutSRV,
	ETextureColorSpace         ColorSpace)
{
	if (FilePath == nullptr)
	{
		return false;
	}

	return CreateTextureFromSTB(Device, FPaths::ToPath(FilePath), OutSRV, ColorSpace);
}

bool FRenderer::CreateTextureFromSTB(
	ID3D11Device*                Device,
	const std::filesystem::path& FilePath,
	ID3D11ShaderResourceView**   OutSRV,
	ETextureColorSpace           ColorSpace)
{
	if (Device == nullptr || OutSRV == nullptr || FilePath.empty())
	{
		return false;
	}

	*OutSRV = nullptr;

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

	int            W    = 0;
	int            H    = 0;
	int            C    = 0;
	unsigned char* Data = stbi_load_from_memory(FileBytes.data(), static_cast<int>(FileBytes.size()), &W, &H, &C, 4);
	if (!Data)
	{
		return false;
	}

	const DXGI_FORMAT TextureFormat = (ColorSpace == ETextureColorSpace::ColorSRGB)
		? DXGI_FORMAT_R8G8B8A8_TYPELESS
		: DXGI_FORMAT_R8G8B8A8_UNORM;
	const DXGI_FORMAT SRVFormat = (ColorSpace == ETextureColorSpace::ColorSRGB)
		? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
		: DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D11_TEXTURE2D_DESC Desc = {};
	Desc.Width                = W;
	Desc.Height               = H;
	Desc.MipLevels            = 1;
	Desc.ArraySize            = 1;
	Desc.Format               = TextureFormat;
	Desc.SampleDesc.Count     = 1;
	Desc.Usage                = D3D11_USAGE_DEFAULT;
	Desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA InitData = { Data, static_cast<UINT>(W * 4), 0 };
	ID3D11Texture2D*       Tex      = nullptr;
	HRESULT                Hr       = Device->CreateTexture2D(&Desc, &InitData, &Tex);
	stbi_image_free(Data);
	if (FAILED(Hr))
	{
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = SRVFormat;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	Hr = Device->CreateShaderResourceView(Tex, &SRVDesc, OutSRV);
	Tex->Release();
	return SUCCEEDED(Hr);
}

void FRenderer::ConfigureMaterialPasses(FMaterial& Material, bool bTexturedMaterial)
{
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return;
	}

	const std::wstring   ShaderDir = FPaths::ShaderDir();
	FMaterialPassShaders DepthPass;
	FMaterialPassShaders GBufferPass;
	FMaterialPassShaders OutlineMaskPass;
	if (bTexturedMaterial)
	{
		DepthPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"SceneGeometry/DepthOnlyTextureVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"SceneGeometry/TextureVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.PS =
				FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"SceneGeometry/GBufferTexturePixelShader.hlsl").c_str());
		OutlineMaskPass.VS = GBufferPass.VS;
		OutlineMaskPass.PS = FShaderMap::Get().GetOrCreatePixelShader(
			Device,
			(ShaderDir + L"SelectionHighlight/OutlineMaskTexturePixelShader.hlsl").c_str());
	}
	else
	{
		DepthPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
			Device,
			(ShaderDir + L"SceneGeometry/DepthOnlyVertexShader.hlsl").c_str(),
			EVertexLayoutType::MeshVertex);
		GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(Device,
		                                                           (ShaderDir + L"SceneGeometry/VertexShader.hlsl").c_str(),
		                                                           EVertexLayoutType::MeshVertex);
		GBufferPass.PS =
				FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"SceneGeometry/GBufferColorPixelShader.hlsl").c_str());
		OutlineMaskPass.VS = GBufferPass.VS;
		OutlineMaskPass.PS =
				FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"SelectionHighlight/OutlineMaskPixelShader.hlsl").c_str());
	}

	Material.SetPassShaders(EMaterialPassType::DepthOnly, DepthPass);
	Material.SetPassShaders(EMaterialPassType::GBuffer, GBufferPass);
	Material.SetPassShaders(EMaterialPassType::OutlineMask, OutlineMaskPass);
}

bool FRenderer::ApplyShaderReload(const FShaderReloadTransaction& Transaction, std::string& OutError)
{
	ID3D11Device* Device = GetDevice();
	ID3D11DeviceContext* Context = GetDeviceContext();
	if (!Device || !Context)
	{
		OutError = "Invalid D3D device/context.";
		return false;
	}

	Context->ClearState();
	Context->Flush();

	if (!FShaderRegistry::Get().ApplyTransaction(Device, Transaction, OutError))
	{
		return false;
	}

	if (Transaction.bRequiresFullFallback)
	{
		if (DefaultMaterial)
		{
			ConfigureMaterialPasses(*DefaultMaterial, false);
		}

		if (DefaultTextureMaterial)
		{
			ConfigureMaterialPasses(*DefaultTextureMaterial, true);
		}
	}

	SetConstantBuffers();
	return true;
}

size_t FRenderer::GetPrevCommandCount() const
{
	return SceneRenderer ? SceneRenderer->GetPrevCommandCount() : 0;
}

ISceneTextFeature* FRenderer::GetSceneTextFeature() const
{
	return TextFeature.get();
}

ISceneSubUVFeature* FRenderer::GetSceneSubUVFeature() const
{
	return SubUVFeature.get();
}

ISceneBillboardFeature* FRenderer::GetSceneBillboardFeature() const
{
	return BillboardFeature.get();
}

FFogRenderFeature* FRenderer::GetFogFeature() const
{
	return FogFeature.get();
}

FOutlineRenderFeature* FRenderer::GetOutlineFeature() const
{
	return OutlineFeature.get();
}

FDebugLineRenderFeature* FRenderer::GetDebugLineFeature() const
{
	return DebugLineFeature.get();
}

FDecalRenderFeature* FRenderer::GetDecalFeature() const
{
	return DecalFeature.get();
}

FVolumeDecalRenderFeature* FRenderer::GetVolumeDecalFeature() const
{
	return VolumeDecalFeature.get();
}

FFireBallRenderFeature* FRenderer::GetFireBallFeature() const
{
	return FireBallFeature.get();
}

FFXAARenderFeature* FRenderer::GetFXAAFeature() const
{
	return FXAAFeature.get();
}

FLightRenderFeature* FRenderer::GetLightFeature() const
{
	return LightFeature.get();
}

FBillboardRenderer& FRenderer::GetBillboardRenderer()
{
	return BillboardFeature->GetRenderer();
}

const FDecalFrameStats& FRenderer::GetDecalFrameStats() const
{
	static constexpr FDecalFrameStats EmptyStats = {};
	return DecalFeature ? DecalFeature->GetFrameStats() : EmptyStats;
}

FMeshPassFrameStats FRenderer::GetMeshPassFrameStats() const
{
	static constexpr FMeshPassFrameStats EmptyStats = {};
	return SceneRenderer ? SceneRenderer->GetMeshPassFrameStats() : EmptyStats;
}

FDecalStats FRenderer::GetDecalStats() const
{
	FDecalStats Stats;
	Stats.Common.Mode = GetDecalProjectionMode();

	if (Stats.Common.Mode == EDecalProjectionMode::VolumeDraw)
	{
		if (VolumeDecalFeature)
		{
			Stats.Volume                        = VolumeDecalFeature->GetStats();
			Stats.Common.BuildTimeMs            = VolumeDecalFeature->GetBuildTimeMs();
			Stats.Common.CullIntersectionTimeMs = VolumeDecalFeature->GetCullIntersectionTimeMs();
			Stats.Common.ShadingPassTimeMs      = VolumeDecalFeature->GetShadingPassTimeMs();
			Stats.Common.TotalDecalTimeMs       = VolumeDecalFeature->GetTotalTimeMs();
		}
		return Stats;
	}

	if (DecalFeature)
	{
		Stats.ClusteredLookup               = DecalFeature->GetClusteredStats();
		const FDecalFrameStats& FrameStats  = DecalFeature->GetFrameStats();
		Stats.Common.BuildTimeMs            = FrameStats.VisibleBuildTimeMs;
		Stats.Common.CullIntersectionTimeMs = FrameStats.ClusterBuildTimeMs;
		Stats.Common.ShadingPassTimeMs      = FrameStats.ShadingPassTimeMs;
		Stats.Common.TotalDecalTimeMs       = FrameStats.TotalDecalTimeMs;
	}

	return Stats;
}

FFogStats FRenderer::GetFogStats() const
{
	FFogStats Stats;
	if (FogFeature)
	{
		Stats = FogFeature->GetStats();
	}
	return Stats;
}

FLightStats FRenderer::GetLightStats() const
{
	if (LightFeature)
	{
		return LightFeature->GetStats();
	}
	return FLightStats{};
}

FGPUFrameStats FRenderer::GetGPUStats() const
{
	FGPUFrameStats            Stats;
	const FDecalStats         DecalStats      = GetDecalStats();
	const FFogStats           FogStats        = GetFogStats();
	const FMeshPassFrameStats MeshPassStats   = GetMeshPassFrameStats();
	const FDecalFrameStats&   DecalFrameStats = GetDecalFrameStats();
	const bool                bHasDecalPass   =
			(DecalStats.Common.Mode == EDecalProjectionMode::VolumeDraw)
				? (DecalStats.Volume.DecalDrawCalls > 0u)
				: (DecalStats.ClusteredLookup.UploadedDecalCount > 0u && DecalStats.ClusteredLookup.DecalCellRegistrations > 0u);

	Stats.GeometryDrawCalls = MeshPassStats.TotalDrawCalls;
	Stats.DecalDrawCalls    = (DecalStats.Common.Mode == EDecalProjectionMode::VolumeDraw)
		                       ? DecalStats.Volume.DecalDrawCalls
		                       : (bHasDecalPass ? 1u : 0u);
	Stats.FogDrawCalls          = FogStats.Common.DrawCallCount;
	Stats.GeometryTimeMs        = MeshPassStats.TotalTimeMs;
	Stats.PixelShadingTimeMs    = DecalStats.Common.ShadingPassTimeMs + FogStats.Common.ShadingPassTimeMs;
	Stats.MemoryBandwidthTimeMs = DecalFrameStats.UploadDecalBufferTimeMs
			+ DecalFrameStats.UploadClusterHeaderBufferTimeMs
			+ DecalFrameStats.UploadClusterIndexBufferTimeMs
			+ FogStats.Common.StructuredBufferUploadTimeMs;
	Stats.OverdrawFillrateTimeMs = Stats.PixelShadingTimeMs;

	Stats.UploadBytes = static_cast<uint64>(DecalFrameStats.UploadedDecalCount) * sizeof(FDecalGPUData)
			+ static_cast<uint64>(DecalFrameStats.UploadedClusterHeaderCount) * sizeof(FDecalClusterHeaderGPU)
			+ static_cast<uint64>(DecalFrameStats.UploadedClusterIndexCount) * sizeof(uint32)
			+ FogStats.Common.TotalUploadBytes;
	Stats.CopyBytes = FogStats.Common.SceneColorCopyBytes;

	Stats.FullscreenPassCount = 0;
	if (DecalStats.Common.Mode == EDecalProjectionMode::ClusteredLookup && bHasDecalPass)
	{
		++Stats.FullscreenPassCount;
	}
	Stats.FullscreenPassCount += FogStats.Common.FullscreenPassCount;

	Stats.PassCount = 4; // clear, upload, depth prepass, forward opaque
	if (bHasDecalPass)
	{
		++Stats.PassCount;
	}
	if (FogStats.Common.TotalFogVolumes > 0u)
	{
		++Stats.PassCount;
	}
	Stats.PassCount += 1; // fireball
	Stats.PassCount += 1; // forward transparent
	Stats.PassCount += 1; // editor grid
	Stats.PassCount += 2; // outline
	Stats.PassCount += 1; // fxaa
	Stats.PassCount += 2; // editor overlay passes

	Stats.DrawCallCount             = Stats.GeometryDrawCalls + Stats.DecalDrawCalls + Stats.FogDrawCalls;
	Stats.EstimatedFullscreenPixels = static_cast<uint64>(
				FogStats.Common.FullscreenPassCount +
				((DecalStats.Common.Mode == EDecalProjectionMode::ClusteredLookup && bHasDecalPass) ? 1u : 0u))
			* static_cast<uint64>(GetBackBufferViewport().Width)
			* static_cast<uint64>(GetBackBufferViewport().Height);

	return Stats;
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
	DeviceContext->CSSetConstantBuffers(0, 2, CBs);
}

void FRenderer::UpdateFrameConstantBuffer(const FFrameContext& Frame, const FViewContext& View)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FFrameConstantBuffer CBData;
	CBData.View           = View.View.GetTransposed();
	CBData.Projection     = View.Projection.GetTransposed();
	CBData.CameraPosition = FVector4(View.CameraPosition, 1.0f);
	CBData.Time           = Frame.TotalTimeSeconds;
	CBData.DeltaTime      = Frame.DeltaTimeSeconds;
	D3D11_MAPPED_SUBRESOURCE Mapped;
	if (SUCCEEDED(DeviceContext->Map(FrameConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(FrameConstantBuffer, 0);
	}
}

void FRenderer::UpdateObjectConstantBuffer(const FMatrix& World)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FObjectConstantBuffer CBData;
	CBData.World                = World.GetTransposed();
	// HLSL cbuffer 기본 column-major 해석으로 인해 CPU에서 한 번 더 전치되어 들어가므로
	// inverse-transpose를 의도할 때는 여기서 inverse만 채워야 셰이더에서 (inverse)^T가 된다.
	CBData.WorldInvTranspose    = World.GetInverse();
	CBData.LocalLightListOffset = 0;
	CBData.LocalLightListCount  = 0;
	CBData.ObjectFlags          = 0;
	CBData.Pad0                 = 0;

	D3D11_MAPPED_SUBRESOURCE Mapped;
	if (SUCCEEDED(DeviceContext->Map(ObjectConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(ObjectConstantBuffer, 0);
	}
}

void FRenderer::UpdateObjectConstantBuffer(const FMeshBatch& Batch)
{
	ID3D11DeviceContext* DeviceContext = RenderDevice.GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FObjectConstantBuffer CBData;
	CBData.World                = Batch.World.GetTransposed();
	// HLSL cbuffer 기본 column-major 해석으로 인해 CPU에서 한 번 더 전치되어 들어가므로
	// inverse-transpose를 의도할 때는 여기서 inverse만 채워야 셰이더에서 (inverse)^T가 된다.
	CBData.WorldInvTranspose    = Batch.World.GetInverse();
	CBData.LocalLightListOffset = Batch.LocalLightListOffset;
	CBData.LocalLightListCount  = Batch.LocalLightListCount;
	CBData.ObjectFlags          = 0;
	CBData.Pad0                 = 0;

	D3D11_MAPPED_SUBRESOURCE Mapped;
	if (SUCCEEDED(DeviceContext->Map(ObjectConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		memcpy(Mapped.pData, &CBData, sizeof(CBData));
		DeviceContext->Unmap(ObjectConstantBuffer, 0);
	}
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

void FRenderer::PreparePassDomain(EPassDomain Domain, const FSceneRenderTargets& Targets)
{
	if (!RenderStateManager)
	{
		return;
	}

	switch (Domain)
	{
	case EPassDomain::Compute:
		RenderStateManager->ClearAllGraphicsState();
		RenderStateManager->ClearAllComputeState();
		if (Targets.GetSceneColorShaderResource())
		{
			RenderStateManager->UnbindResourceEverywhere(Targets.GetSceneColorTexture());
		}
		if (Targets.GetSceneColorWriteTexture())
		{
			RenderStateManager->UnbindResourceEverywhere(Targets.GetSceneColorWriteTexture());
		}
		break;

	case EPassDomain::Copy:
		RenderStateManager->ClearAllGraphicsState();
		RenderStateManager->ClearAllComputeState();
		break;

	case EPassDomain::Graphics:
	default:
		RenderStateManager->ClearAllComputeState();
		break;
	}
}

bool FRenderer::EnsureFinalImageResources()
{
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return false;
	}

	const std::wstring ShaderDir = FPaths::ShaderDir().wstring();

	if (!FinalImageVertexShader)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Vertex;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "vs_5_0";
		Recipe.LayoutType = EVertexLayoutType::FullscreenNone;
		FinalImageVertexShader = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
	}

	if (!FinalImageBlitPixelShader)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Pixel;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/BlitPixelShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "ps_5_0";
		FinalImageBlitPixelShader = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	if (!ToneMappingPixelShader)
	{
		FShaderRecipe Recipe = {};
		Recipe.Stage = EShaderStage::Pixel;
		Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/ToneMappingPixelShader.hlsl";
		Recipe.EntryPoint = "main";
		Recipe.Target = "ps_5_0";
		ToneMappingPixelShader = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
	}

	if (!ToneMappingConstantBuffer)
	{
		D3D11_BUFFER_DESC Desc = {};
		Desc.ByteWidth = sizeof(FToneMappingConstants);
		Desc.Usage = D3D11_USAGE_DYNAMIC;
		Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(Device->CreateBuffer(&Desc, nullptr, &ToneMappingConstantBuffer)))
		{
			return false;
		}
	}

	if (!FullscreenRasterizerState)
	{
		D3D11_RASTERIZER_DESC Desc = {};
		Desc.FillMode = D3D11_FILL_SOLID;
		Desc.CullMode = D3D11_CULL_NONE;
		Desc.DepthClipEnable = TRUE;
		if (FAILED(Device->CreateRasterizerState(&Desc, &FullscreenRasterizerState)))
		{
			return false;
		}
	}

	if (!FullscreenNoDepthState)
	{
		D3D11_DEPTH_STENCIL_DESC Desc = {};
		Desc.DepthEnable = FALSE;
		Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		Desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(Device->CreateDepthStencilState(&Desc, &FullscreenNoDepthState)))
		{
			return false;
		}
	}

	if (!FullscreenPointSampler)
	{
		D3D11_SAMPLER_DESC Desc = {};
		Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		Desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		Desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		Desc.MinLOD = 0.0f;
		Desc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(Device->CreateSamplerState(&Desc, &FullscreenPointSampler)))
		{
			return false;
		}
	}

	return FinalImageVertexShader != nullptr
		&& FinalImageBlitPixelShader != nullptr
		&& ToneMappingPixelShader != nullptr
		&& ToneMappingConstantBuffer != nullptr
		&& FullscreenRasterizerState != nullptr
		&& FullscreenNoDepthState != nullptr
		&& FullscreenPointSampler != nullptr;
}

bool FRenderer::ResolveSceneColorTargets(
	FSceneRenderTargets& Targets,
	const FFrameContext& Frame,
	const FViewContext& View,
	bool bApplyFXAA)
{
	ID3D11DeviceContext* DeviceContext = GetDeviceContext();
	if (!DeviceContext || !Targets.SceneColorRead || !Targets.SceneColorWrite || !EnsureFinalImageResources())
	{
		return false;
	}

	PreparePassDomain(EPassDomain::Copy, Targets);

	if (RenderStateManager)
	{
		if (Targets.SceneColorRead->Texture)
		{
			RenderStateManager->UnbindResourceEverywhere(Targets.SceneColorRead->Texture);
		}
		if (Targets.SceneColorWrite->Texture)
		{
			RenderStateManager->UnbindResourceEverywhere(Targets.SceneColorWrite->Texture);
		}
	}

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (FAILED(DeviceContext->Map(ToneMappingConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		return false;
	}

	*reinterpret_cast<FToneMappingConstants*>(Mapped.pData) = FToneMappingConstants{};
	DeviceContext->Unmap(ToneMappingConstantBuffer, 0);

	FFullscreenPassPipelineState PipelineState;
	PipelineState.DepthStencilState = FullscreenNoDepthState;
	PipelineState.RasterizerState = FullscreenRasterizerState;

	const FFullscreenPassConstantBufferBinding ToneMappingBuffers[] =
	{
		{ 0, ToneMappingConstantBuffer },
	};
	const FFullscreenPassShaderResourceBinding ToneMappingResources[] =
	{
		{ 0, Targets.SceneColorRead->SRV },
	};
	const FFullscreenPassSamplerBinding ToneMappingSamplers[] =
	{
		{ 0, FullscreenPointSampler },
	};
	const FFullscreenPassBindings ToneMappingBindings
	{
		ToneMappingBuffers,
		static_cast<uint32>(sizeof(ToneMappingBuffers) / sizeof(ToneMappingBuffers[0])),
		ToneMappingResources,
		static_cast<uint32>(sizeof(ToneMappingResources) / sizeof(ToneMappingResources[0])),
		ToneMappingSamplers,
		static_cast<uint32>(sizeof(ToneMappingSamplers) / sizeof(ToneMappingSamplers[0]))
	};

	if (!ExecuteFullscreenPass(
		*this,
		Frame,
		View,
		Targets.SceneColorWrite->RTV,
		nullptr,
		View.Viewport,
		{ FinalImageVertexShader, ToneMappingPixelShader },
		PipelineState,
		ToneMappingBindings,
		[](ID3D11DeviceContext& Context)
		{
			Context.Draw(3, 0);
		}))
	{
		return false;
	}

	Targets.SwapSceneColor();

	if (bApplyFXAA && FXAAFeature)
	{
		PreparePassDomain(EPassDomain::Copy, Targets);
		if (!FXAAFeature->Render(*this, Frame, View, Targets))
		{
			return false;
		}
	}

	if (!Targets.NeedsSceneColorResolve())
	{
		return true;
	}

	PreparePassDomain(EPassDomain::Copy, Targets);

	const FFullscreenPassShaderResourceBinding BlitResources[] =
	{
		{ 0, Targets.SceneColorRead->SRV },
	};
	const FFullscreenPassSamplerBinding BlitSamplers[] =
	{
		{ 0, FullscreenPointSampler },
	};
	const FFullscreenPassBindings BlitBindings
	{
		nullptr,
		0u,
		BlitResources,
		static_cast<uint32>(sizeof(BlitResources) / sizeof(BlitResources[0])),
		BlitSamplers,
		static_cast<uint32>(sizeof(BlitSamplers) / sizeof(BlitSamplers[0]))
	};

	return ExecuteFullscreenPass(
		*this,
		Frame,
		View,
		Targets.FinalSceneColor->RTV,
		nullptr,
		View.Viewport,
		{ FinalImageVertexShader, FinalImageBlitPixelShader },
		PipelineState,
		BlitBindings,
		[](ID3D11DeviceContext& Context)
		{
			Context.Draw(3, 0);
		});
}

bool FRenderer::CreateConstantBuffers()
{
	ID3D11Device* Device = RenderDevice.GetDevice();
	if (!Device)
	{
		return false;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.Usage             = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
	Desc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;

	Desc.ByteWidth = sizeof(FFrameConstantBuffer);
	if (FAILED(Device->CreateBuffer(&Desc, nullptr, &FrameConstantBuffer)))
	{
		return false;
	}

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
	SamplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SamplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_WRAP;
	SamplerDesc.ComparisonFunc     = D3D11_COMPARISON_NEVER;
	SamplerDesc.MinLOD             = 0;
	SamplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;

	HRESULT Hr = Device->CreateSamplerState(&SamplerDesc, &NormalSampler);
	if (FAILED(Hr))
	{
		return false;
	}

	return true;
}
