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
#include "Renderer/Frame/EditorFrameRenderer.h"
#include "Renderer/Frame/GameFrameRenderer.h"
#include "Renderer/Frame/RendererResourceBootstrap.h"
#include "Renderer/Frame/SceneTargetManager.h"
#include "Renderer/Frame/Viewport/ViewportCompositor.h"
#include "Renderer/Mesh/RenderMesh.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Resources/Shader/ShaderResource.h"
#include "Renderer/Resources/Shader/ShaderType.h"
#include "Renderer/Scene/Builders/DebugSceneBuilder.h"
#include "Renderer/Scene/MeshPassProcessor.h"
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

FBillboardRenderer &FRenderer::GetBillboardRenderer()
{
    return BillboardFeature->GetRenderer();
}

ISceneTextFeature *FRenderer::GetSceneTextFeature() const
{
    return TextFeature.get();
}

ISceneSubUVFeature *FRenderer::GetSceneSubUVFeature() const
{
    return SubUVFeature.get();
}

ISceneBillboardFeature *FRenderer::GetSceneBillboardFeature() const
{
    return BillboardFeature.get();
}

FFogRenderFeature *FRenderer::GetFogFeature() const
{
    return FogFeature.get();
}

FOutlineRenderFeature *FRenderer::GetOutlineFeature() const
{
    return OutlineFeature.get();
}

FDebugLineRenderFeature *FRenderer::GetDebugLineFeature() const
{
    return DebugLineFeature.get();
}

FDecalRenderFeature *FRenderer::GetDecalFeature() const
{
    return DecalFeature.get();
}

FVolumeDecalRenderFeature *FRenderer::GetVolumeDecalFeature() const
{
    return VolumeDecalFeature.get();
}

FFireBallRenderFeature *FRenderer::GetFireBallFeature() const
{
    return FireBallFeature.get();
}

FFXAARenderFeature *FRenderer::GetFXAAFeature() const
{
    return FXAAFeature.get();
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
        return false;
    ID3D11Device *Device = RenderDevice.GetDevice();
    ID3D11DeviceContext *DeviceContext = RenderDevice.GetDeviceContext();
    if (!Device || !DeviceContext)
        return false;

    RenderStateManager = std::make_unique<FRenderStateManager>(Device, DeviceContext);
    RenderStateManager->PrepareCommonStates();

    if (!CreateSamplers())
        return false;
    if (!ViewportCompositor || !ViewportCompositor->Initialize(Device))
        return false;

    if (!CreateConstantBuffers())
        return false;
    SetConstantBuffers();

    if (!FRendererResourceBootstrap::Initialize(*this))
    {
        return false;
    }

    return true;
}

void FRenderer::SetConstantBuffers()
{
    ID3D11DeviceContext *DeviceContext = RenderDevice.GetDeviceContext();
    if (!DeviceContext)
    {
        return;
    }

    ID3D11Buffer *CBs[2] = {FrameConstantBuffer, ObjectConstantBuffer};
    DeviceContext->VSSetConstantBuffers(0, 2, CBs);
    DeviceContext->PSSetConstantBuffers(0, 2, CBs);
}

void FRenderer::BeginFrame()
{
    constexpr float ClearColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};
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

bool FRenderer::RenderGameFrame(const FGameFrameRequest &Request)
{
    return FGameFrameRenderer::Render(*this, Request);
}

bool FRenderer::RenderScreenUIPass(const FScreenUIPassInputs &PassInputs, const FFrameContext &Frame,
                                   ID3D11RenderTargetView *RenderTargetView, ID3D11DepthStencilView *DepthStencilView)
{
    return ScreenUIRenderer && ScreenUIRenderer->Render(*this, Frame, PassInputs, RenderTargetView, DepthStencilView);
}

bool FRenderer::ComposeViewports(const FViewportCompositePassInputs &Inputs, const FFrameContext &Frame,
                                 const FViewContext &View, ID3D11RenderTargetView *RenderTargetView,
                                 ID3D11DepthStencilView *DepthStencilView)
{
    if (!RenderTargetView)
    {
        return false;
    }

    return ViewportCompositor &&
           ViewportCompositor->Compose(*this, Frame, View, RenderTargetView, DepthStencilView, Inputs);
}

bool FRenderer::RenderEditorFrame(const FEditorFrameRequest &Request)
{
    return FEditorFrameRenderer::Render(*this, Request);
}

void FRenderer::ClearDepthBuffer(ID3D11DepthStencilView *DepthStencilView)
{
    ID3D11DeviceContext *DeviceContext = RenderDevice.GetDeviceContext();
    if (!DeviceContext || !DepthStencilView)
    {
        return;
    }

    DeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

const FDecalFrameStats &FRenderer::GetDecalFrameStats() const
{
    static const FDecalFrameStats EmptyStats = {};
    return DecalFeature ? DecalFeature->GetFrameStats() : EmptyStats;
}

FMeshPassFrameStats FRenderer::GetMeshPassFrameStats() const
{
    static const FMeshPassFrameStats EmptyStats = {};
    return SceneRenderer ? SceneRenderer->GetMeshPassFrameStats() : EmptyStats;
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

FGPUFrameStats FRenderer::GetGPUStats() const
{
    FGPUFrameStats Stats;
    const FDecalStats DecalStats = GetDecalStats();
    const FFogStats FogStats = GetFogStats();
    const FMeshPassFrameStats MeshPassStats = GetMeshPassFrameStats();
    const FDecalFrameStats& DecalFrameStats = GetDecalFrameStats();
    const bool bHasDecalPass =
        (DecalStats.Common.Mode == EDecalProjectionMode::VolumeDraw)
            ? (DecalStats.Volume.DecalDrawCalls > 0u)
            : (DecalStats.ClusteredLookup.UploadedDecalCount > 0u && DecalStats.ClusteredLookup.DecalCellRegistrations > 0u);

    Stats.GeometryDrawCalls = MeshPassStats.TotalDrawCalls;
    Stats.DecalDrawCalls = (DecalStats.Common.Mode == EDecalProjectionMode::VolumeDraw)
        ? DecalStats.Volume.DecalDrawCalls
        : (bHasDecalPass ? 1u : 0u);
    Stats.FogDrawCalls = FogStats.Common.DrawCallCount;
    Stats.GeometryTimeMs = MeshPassStats.TotalTimeMs;
    Stats.PixelShadingTimeMs = DecalStats.Common.ShadingPassTimeMs + FogStats.Common.ShadingPassTimeMs;
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

    Stats.DrawCallCount = Stats.GeometryDrawCalls + Stats.DecalDrawCalls + Stats.FogDrawCalls;
    Stats.EstimatedFullscreenPixels = static_cast<uint64>(
        FogStats.Common.FullscreenPassCount +
        ((DecalStats.Common.Mode == EDecalProjectionMode::ClusteredLookup && bHasDecalPass) ? 1u : 0u))
        * static_cast<uint64>(GetBackBufferViewport().Width)
        * static_cast<uint64>(GetBackBufferViewport().Height);

    return Stats;
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
            Stats.Common.BuildTimeMs = VolumeDecalFeature->GetBuildTimeMs();
            Stats.Common.CullIntersectionTimeMs = VolumeDecalFeature->GetCullIntersectionTimeMs();
            Stats.Common.ShadingPassTimeMs = VolumeDecalFeature->GetShadingPassTimeMs();
            Stats.Common.TotalDecalTimeMs = VolumeDecalFeature->GetTotalTimeMs();
        }
        return Stats;
    }

    if (DecalFeature)
    {
        Stats.ClusteredLookup = DecalFeature->GetClusteredStats();
        const FDecalFrameStats &FrameStats = DecalFeature->GetFrameStats();
        Stats.Common.BuildTimeMs = FrameStats.VisibleBuildTimeMs;
        Stats.Common.CullIntersectionTimeMs = FrameStats.ClusterBuildTimeMs;
        Stats.Common.ShadingPassTimeMs = FrameStats.ShadingPassTimeMs;
        Stats.Common.TotalDecalTimeMs = FrameStats.TotalDecalTimeMs;
    }

    return Stats;
}

size_t FRenderer::GetPrevCommandCount() const
{
    return SceneRenderer ? SceneRenderer->GetPrevCommandCount() : 0;
}

bool FRenderer::CreateConstantBuffers()
{
    ID3D11Device *Device = RenderDevice.GetDevice();
    if (!Device)
    {
        return false;
    }

    D3D11_BUFFER_DESC Desc = {};
    Desc.Usage = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    Desc.ByteWidth = sizeof(FFrameConstantBuffer);
    if (FAILED(Device->CreateBuffer(&Desc, nullptr, &FrameConstantBuffer)))
        return false;

    Desc.ByteWidth = sizeof(FObjectConstantBuffer);
    return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, &ObjectConstantBuffer));
}

bool FRenderer::CreateSamplers()
{
    ID3D11Device *Device = RenderDevice.GetDevice();
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

void FRenderer::UpdateFrameConstantBuffer(const FFrameContext &Frame, const FViewContext &View)
{
    ID3D11DeviceContext *DeviceContext = RenderDevice.GetDeviceContext();
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

void FRenderer::UpdateObjectConstantBuffer(const FMatrix &WorldMatrix)
{
    ID3D11DeviceContext *DeviceContext = RenderDevice.GetDeviceContext();
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

void FRenderer::ConfigureMaterialPasses(FMaterial &Material, bool bTexturedMaterial)
{
    ID3D11Device *Device = GetDevice();
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
            Device, (ShaderDir + L"SceneGeometry/DepthOnlyTextureVertexShader.hlsl").c_str(), EVertexLayoutType::MeshVertex);
        GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
            Device, (ShaderDir + L"SceneGeometry/TextureVertexShader.hlsl").c_str(), EVertexLayoutType::MeshVertex);
        GBufferPass.PS =
            FShaderMap::Get().GetOrCreatePixelShader(Device, (ShaderDir + L"SceneGeometry/GBufferTexturePixelShader.hlsl").c_str());
        OutlineMaskPass.VS = GBufferPass.VS;
        OutlineMaskPass.PS = FShaderMap::Get().GetOrCreatePixelShader(
            Device, (ShaderDir + L"SelectionHighlight/OutlineMaskTexturePixelShader.hlsl").c_str());
    }
    else
    {
        DepthPass.VS = FShaderMap::Get().GetOrCreateVertexShader(
            Device, (ShaderDir + L"SceneGeometry/DepthOnlyVertexShader.hlsl").c_str(), EVertexLayoutType::MeshVertex);
        GBufferPass.VS = FShaderMap::Get().GetOrCreateVertexShader(Device, (ShaderDir + L"SceneGeometry/VertexShader.hlsl").c_str(),
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

bool FRenderer::CreateTextureFromSTB(ID3D11Device *Device, const char *FilePath, ID3D11ShaderResourceView **OutSRV)
{
    if (FilePath == nullptr)
    {
        return false;
    }

    return CreateTextureFromSTB(Device, FPaths::ToPath(FilePath), OutSRV);
}

bool FRenderer::CreateTextureFromSTB(ID3D11Device *Device, const std::filesystem::path &FilePath,
                                     ID3D11ShaderResourceView **OutSRV)
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
    if (!File.read(reinterpret_cast<char *>(FileBytes.data()), FileSize))
    {
        return false;
    }

    int W = 0;
    int H = 0;
    int C = 0;
    unsigned char *Data = stbi_load_from_memory(FileBytes.data(), static_cast<int>(FileBytes.size()), &W, &H, &C, 4);
    if (!Data)
        return false;

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = W;
    Desc.Height = H;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA InitData = {Data, static_cast<UINT>(W * 4), 0};
    ID3D11Texture2D *Tex = nullptr;
    HRESULT Hr = Device->CreateTexture2D(&Desc, &InitData, &Tex);
    stbi_image_free(Data);
    if (FAILED(Hr))
        return false;

    Hr = Device->CreateShaderResourceView(Tex, nullptr, OutSRV);
    Tex->Release();
    return SUCCEEDED(Hr);
}

void FRenderer::Release()
{
    if (SceneTargetManager)
        SceneTargetManager->Release();
    if (ViewportCompositor)
        ViewportCompositor->Release();
    FRendererResourceBootstrap::Release(*this);
    RenderDevice.Release();
}

bool FRenderer::IsOccluded()
{
    return RenderDevice.IsOccluded();
}

void FRenderer::OnResize(int32 W, int32 H)
{
    if (W == 0 || H == 0)
        return;
    if (SceneTargetManager)
        SceneTargetManager->Release();
    RenderDevice.OnResize(W, H);
}
