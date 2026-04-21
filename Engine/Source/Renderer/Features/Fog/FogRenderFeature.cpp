#include "Renderer/Features/Fog/FogRenderFeature.h"

#include "Core/Paths.h"
#include "Debug/EngineLog.h"
#include "Math/Transform.h"
#include "Renderer/GraphicsCore/FullscreenPass.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace
{
    static constexpr UINT FOG_COMPOSITE_CB_SLOT = 0;
    static constexpr UINT FOG_CLUSTER_CB_SLOT = 1;
    static constexpr UINT FOG_SCENECOLOR_SRV_SLOT = 0;
    static constexpr UINT FOG_DEPTH_SRV_SLOT = 1;
    static constexpr UINT FOG_CLUSTER_HEADERS_SRV_SLOT = 10;
    static constexpr UINT FOG_CLUSTER_INDICES_SRV_SLOT = 11;
    static constexpr UINT FOG_DATA_SRV_SLOT = 12;
    static constexpr UINT FOG_GLOBAL_DATA_SRV_SLOT = 13;
    static constexpr UINT FOG_SCENECOLOR_SAMPLER_SLOT = 0;
    static constexpr UINT FOG_DEPTH_SAMPLER_SLOT = 1;

    static constexpr uint32 FOG_CLUSTER_COUNT_X = 16;
    static constexpr uint32 FOG_CLUSTER_COUNT_Y = 9;
    static constexpr uint32 FOG_CLUSTER_COUNT_Z = 24;
    static constexpr uint32 FOG_MAX_CLUSTER_ITEMS = 64;

    struct FFogCompositeConstantBuffer
    {
        FMatrix InverseViewProjection = FMatrix::Identity;
        FMatrix ViewMatrix = FMatrix::Identity;
        FVector4 CameraPosition = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
        FVector4 ScreenSize = FVector4(0.0f, 0.0f, 0.0f, 0.0f);     // width, height, 1/width, 1/height
        FVector4 ClusterParams = FVector4(0.0f, 0.0f, 0.0f, 0.0f);  // tileCountX, tileCountY, sliceCountZ, nearZ
        FVector4 ClusterParams2 = FVector4(0.0f, 0.0f, 0.0f, 0.0f); // farZ, logZScale, logZBias, globalFogCount
        FVector4 ViewParams = FVector4(0.0f, 0.0f, 0.0f, 0.0f);     // x: orthographic flag
    };

    struct FFogClusterConstantBuffer
    {
        uint32 ClusterCountX = 0;
        uint32 ClusterCountY = 0;
        uint32 ClusterCountZ = 0;
        uint32 MaxClusterItems = 0;
        float ViewportWidth = 0.0f;
        float ViewportHeight = 0.0f;
        float NearZ = 0.0f;
        float FarZ = 0.0f;
        float LogZScale = 0.0f;
        float LogZBias = 0.0f;
        float TileWidth = 0.0f;
        float TileHeight = 0.0f;
    };

    struct FFogGPUData
    {
        FMatrix WorldToFogVolume = FMatrix::Identity;
        FVector4 FogOrigin = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
        FVector4 FogColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        FVector4 FogParams = FVector4(0.0f, 0.0f, 0.0f, 0.0f);  // density, falloff, start, cutoff
        FVector4 FogParams2 = FVector4(1.0f, 1.0f, 0.0f, 0.0f); // maxOpacity, allowBackground, isLocalVolume, pad
    };

    struct FFogClusterHeaderGPU
    {
        uint32 Offset = 0;
        uint32 Count = 0;
        uint32 Pad0 = 0;
        uint32 Pad1 = 0;
    };

    struct FFogClusterRange
    {
        bool bValid = false;
        uint32 MinTileX = 0;
        uint32 MaxTileX = 0;
        uint32 MinTileY = 0;
        uint32 MaxTileY = 0;
        uint32 MinSliceZ = 0;
        uint32 MaxSliceZ = 0;
    };

    static uint32 Align16(uint32 Size)
    {
        return (Size + 15u) & ~15u;
    }

    static uint32 FlattenClusterIndex(uint32 X, uint32 Y, uint32 Z)
    {
        return X + Y * FOG_CLUSTER_COUNT_X + Z * FOG_CLUSTER_COUNT_X * FOG_CLUSTER_COUNT_Y;
    }

    static uint32 ComputeTileX(float ScreenX, float ViewportWidth)
    {
        const float TileWidth = ViewportWidth / static_cast<float>(FOG_CLUSTER_COUNT_X);
        int32 X = static_cast<int32>(std::floor(ScreenX / TileWidth));
        X = std::clamp(X, 0, static_cast<int32>(FOG_CLUSTER_COUNT_X) - 1);
        return static_cast<uint32>(X);
    }

    static uint32 ComputeTileY(float ScreenY, float ViewportHeight)
    {
        const float TileHeight = ViewportHeight / static_cast<float>(FOG_CLUSTER_COUNT_Y);
        int32 Y = static_cast<int32>(std::floor(ScreenY / TileHeight));
        Y = std::clamp(Y, 0, static_cast<int32>(FOG_CLUSTER_COUNT_Y) - 1);
        return static_cast<uint32>(Y);
    }

    static uint32 ComputeZSlice(float ViewDepth, float NearZ, float FarZ)
    {
        const float Depth = std::clamp(ViewDepth, NearZ, FarZ);
        const float LogScale = static_cast<float>(FOG_CLUSTER_COUNT_Z) / std::log(FarZ / NearZ);
        const float LogBias = -std::log(NearZ) * LogScale;
        int32 Slice = static_cast<int32>(std::floor(std::log(Depth) * LogScale + LogBias));
        Slice = std::clamp(Slice, 0, static_cast<int32>(FOG_CLUSTER_COUNT_Z) - 1);
        return static_cast<uint32>(Slice);
    }

    static bool ComputeFogClusterRange(const FViewContext& View, const FFogRenderItem& Item, FFogClusterRange& OutRange)
    {
        if (!Item.IsLocalFogVolume() || View.Viewport.Width <= 1.0f || View.Viewport.Height <= 1.0f || View.NearZ <= 0.0f || View.FarZ <= View.NearZ)
        {
            return false;
        }

        static constexpr std::array<FVector, 8> CornerSigns =
        {
            FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, -1.0f, -1.0f),
            FVector(-1.0f, 1.0f, -1.0f),  FVector(1.0f, 1.0f, -1.0f),
            FVector(-1.0f, -1.0f, 1.0f),  FVector(1.0f, -1.0f, 1.0f),
            FVector(-1.0f, 1.0f, 1.0f),   FVector(1.0f, 1.0f, 1.0f)
        };

        float MinScreenX = View.Viewport.Width;
        float MaxScreenX = 0.0f;
        float MinScreenY = View.Viewport.Height;
        float MaxScreenY = 0.0f;
        float MinViewDepth = FLT_MAX;
        float MaxViewDepth = 0.0f;
        bool bHasProjectedPoint = false;
        bool bTouchesNearPlane = false;

        const DirectX::XMMATRIX ViewProjXM = View.ViewProjection.ToXMMatrix();
        const float ViewportWidth = View.Viewport.Width;
        const float ViewportHeight = View.Viewport.Height;

        for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
        {
            const FVector LocalUnitCorner = CornerSigns[CornerIndex] * 0.5f;
            const FVector WorldCorner = Item.FogVolumeWorld.TransformPosition(LocalUnitCorner);
            const FVector ViewCorner = View.View.TransformPosition(WorldCorner);
            const float ViewDepth = ViewCorner.X;
            MinViewDepth = std::min(MinViewDepth, ViewDepth);
            MaxViewDepth = std::max(MaxViewDepth, ViewDepth);
            if (ViewDepth <= View.NearZ)
            {
                bTouchesNearPlane = true;
            }

            const DirectX::XMVECTOR WorldCorner4 = DirectX::XMVectorSet(WorldCorner.X, WorldCorner.Y, WorldCorner.Z, 1.0f);
            const DirectX::XMVECTOR ClipCornerXM = DirectX::XMVector4Transform(WorldCorner4, ViewProjXM);
            const float ClipX = DirectX::XMVectorGetX(ClipCornerXM);
            const float ClipY = DirectX::XMVectorGetY(ClipCornerXM);
            const float ClipW = DirectX::XMVectorGetW(ClipCornerXM);
            if (std::fabs(ClipW) <= 1e-4f)
            {
                continue;
            }

            const float InvW = 1.0f / ClipW;
            const float NdcX = ClipX * InvW;
            const float NdcY = ClipY * InvW;
            const float ScreenX = (NdcX * 0.5f + 0.5f) * ViewportWidth;
            const float ScreenY = (-NdcY * 0.5f + 0.5f) * ViewportHeight;
            MinScreenX = std::min(MinScreenX, ScreenX);
            MaxScreenX = std::max(MaxScreenX, ScreenX);
            MinScreenY = std::min(MinScreenY, ScreenY);
            MaxScreenY = std::max(MaxScreenY, ScreenY);
            bHasProjectedPoint = true;
        }

        if (MaxViewDepth < View.NearZ || MinViewDepth > View.FarZ)
        {
            return false;
        }

        MinViewDepth = std::clamp(MinViewDepth, View.NearZ, View.FarZ);
        MaxViewDepth = std::clamp(MaxViewDepth, View.NearZ, View.FarZ);
        OutRange.MinSliceZ = ComputeZSlice(MinViewDepth, View.NearZ, View.FarZ);
        OutRange.MaxSliceZ = ComputeZSlice(MaxViewDepth, View.NearZ, View.FarZ);

        if (bTouchesNearPlane || !bHasProjectedPoint)
        {
            OutRange.MinTileX = 0;
            OutRange.MaxTileX = FOG_CLUSTER_COUNT_X - 1;
            OutRange.MinTileY = 0;
            OutRange.MaxTileY = FOG_CLUSTER_COUNT_Y - 1;
            OutRange.bValid = true;
            return true;
        }

        if (MaxScreenX < 0.0f || MaxScreenY < 0.0f || MinScreenX >= ViewportWidth || MinScreenY >= ViewportHeight)
        {
            return false;
        }

        MinScreenX = std::clamp(MinScreenX, 0.0f, ViewportWidth - 1.0f);
        MaxScreenX = std::clamp(MaxScreenX, 0.0f, ViewportWidth - 1.0f);
        MinScreenY = std::clamp(MinScreenY, 0.0f, ViewportHeight - 1.0f);
        MaxScreenY = std::clamp(MaxScreenY, 0.0f, ViewportHeight - 1.0f);
        OutRange.MinTileX = ComputeTileX(MinScreenX, ViewportWidth);
        OutRange.MaxTileX = ComputeTileX(MaxScreenX, ViewportWidth);
        OutRange.MinTileY = ComputeTileY(MinScreenY, ViewportHeight);
        OutRange.MaxTileY = ComputeTileY(MaxScreenY, ViewportHeight);
        OutRange.bValid = true;
        return true;
    }
}

FFogRenderFeature::~FFogRenderFeature()
{
    Release();
}

bool FFogRenderFeature::Initialize(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    if (!Device)
    {
        return false;
    }

    auto CreateDynamicConstantBuffer = [&](ID3D11Buffer*& Buffer, uint32 ByteWidth) -> bool
    {
        if (Buffer)
        {
            return true;
        }
        D3D11_BUFFER_DESC Desc = {};
        Desc.Usage = D3D11_USAGE_DYNAMIC;
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Desc.ByteWidth = ByteWidth;
        return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, &Buffer));
    };

    if (!CreateDynamicConstantBuffer(FogCompositeConstantBuffer, sizeof(FFogCompositeConstantBuffer))
        || !CreateDynamicConstantBuffer(FogClusterConstantBuffer, sizeof(FFogClusterConstantBuffer)))
    {
        return false;
    }

    if (!NoDepthState)
    {
        D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
        DepthDesc.DepthEnable = FALSE;
        DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(Device->CreateDepthStencilState(&DepthDesc, &NoDepthState)))
        {
            return false;
        }
    }

    if (!FogRasterizerState)
    {
        D3D11_RASTERIZER_DESC RasterDesc = {};
        RasterDesc.FillMode = D3D11_FILL_SOLID;
        RasterDesc.CullMode = D3D11_CULL_NONE;
        RasterDesc.DepthClipEnable = TRUE;
        if (FAILED(Device->CreateRasterizerState(&RasterDesc, &FogRasterizerState)))
        {
            return false;
        }
    }

    if (!LinearSampler)
    {
        D3D11_SAMPLER_DESC SamplerDesc = {};
        SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.MinLOD = 0.0f;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(Device->CreateSamplerState(&SamplerDesc, &LinearSampler)))
        {
            return false;
        }
    }

    if (!DepthSampler)
    {
        D3D11_SAMPLER_DESC SamplerDesc = {};
        SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.MinLOD = 0.0f;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(Device->CreateSamplerState(&SamplerDesc, &DepthSampler)))
        {
            return false;
        }
    }

    const std::wstring ShaderDir = FPaths::ShaderDir().wstring();
    if (!FogPostVS)
    {
        FShaderRecipe Recipe = {};
        Recipe.Stage = EShaderStage::Vertex;
        Recipe.SourcePath = ShaderDir + L"FinalImagePostProcess/BlitVertexShader.hlsl";
        Recipe.EntryPoint = "main";
        Recipe.Target = "vs_5_0";
        Recipe.LayoutType = EVertexLayoutType::FullscreenNone;
        FogPostVS = FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
    }

    if (!FogPostPS)
    {
        FShaderRecipe Recipe = {};
        Recipe.Stage = EShaderStage::Pixel;
        Recipe.SourcePath = ShaderDir + L"SceneEffects/FogCompositeClusteredPixelShader.hlsl";
        Recipe.EntryPoint = "main";
        Recipe.Target = "ps_5_0";
        FogPostPS = FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
    }

    return FogPostVS != nullptr && FogPostPS != nullptr;
}

bool FFogRenderFeature::UpdateFogCompositeConstantBuffer(FRenderer& Renderer, const FViewContext& View, uint32 TotalFogCount, uint32 GlobalFogCount, uint32 LocalFogCount)
{
    ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
    if (!FogCompositeConstantBuffer || !DeviceContext)
    {
        return false;
    }

    FFogCompositeConstantBuffer CBData = {};
    CBData.InverseViewProjection = View.InverseViewProjection.GetTransposed();
    CBData.ViewMatrix = View.View.GetTransposed();
    CBData.CameraPosition = FVector4(View.CameraPosition.X, View.CameraPosition.Y, View.CameraPosition.Z, 0.0f);
    const float InvWidth = View.Viewport.Width > 0.0f ? 1.0f / View.Viewport.Width : 0.0f;
    const float InvHeight = View.Viewport.Height > 0.0f ? 1.0f / View.Viewport.Height : 0.0f;
    CBData.ScreenSize = FVector4(View.Viewport.Width, View.Viewport.Height, InvWidth, InvHeight);
    CBData.ClusterParams = FVector4(static_cast<float>(FOG_CLUSTER_COUNT_X), static_cast<float>(FOG_CLUSTER_COUNT_Y), static_cast<float>(FOG_CLUSTER_COUNT_Z), View.NearZ);
    const float LogZScale = static_cast<float>(FOG_CLUSTER_COUNT_Z) / std::log(View.FarZ / View.NearZ);
    const float LogZBias = -std::log(View.NearZ) * LogZScale;
    CBData.ClusterParams2 = FVector4(View.FarZ, LogZScale, LogZBias, static_cast<float>(GlobalFogCount));
    CBData.ViewParams = FVector4(View.bOrthographic ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(DeviceContext->Map(FogCompositeConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        return false;
    }
    std::memcpy(Mapped.pData, &CBData, sizeof(CBData));
    DeviceContext->Unmap(FogCompositeConstantBuffer, 0);
    return true;
}

bool FFogRenderFeature::UpdateFogClusterConstantBuffer(FRenderer& Renderer, const FViewContext& View)
{
    ID3D11DeviceContext* DeviceContext = Renderer.GetDeviceContext();
    if (!FogClusterConstantBuffer || !DeviceContext || View.Viewport.Width <= 0.0f || View.Viewport.Height <= 0.0f || View.NearZ <= 0.0f || View.FarZ <= View.NearZ)
    {
        return false;
    }

    FFogClusterConstantBuffer CBData = {};
    CBData.ClusterCountX = FOG_CLUSTER_COUNT_X;
    CBData.ClusterCountY = FOG_CLUSTER_COUNT_Y;
    CBData.ClusterCountZ = FOG_CLUSTER_COUNT_Z;
    CBData.MaxClusterItems = FOG_MAX_CLUSTER_ITEMS;
    CBData.ViewportWidth = View.Viewport.Width;
    CBData.ViewportHeight = View.Viewport.Height;
    CBData.NearZ = View.NearZ;
    CBData.FarZ = View.FarZ;
    CBData.TileWidth = View.Viewport.Width / static_cast<float>(FOG_CLUSTER_COUNT_X);
    CBData.TileHeight = View.Viewport.Height / static_cast<float>(FOG_CLUSTER_COUNT_Y);
    CBData.LogZScale = static_cast<float>(FOG_CLUSTER_COUNT_Z) / std::log(View.FarZ / View.NearZ);
    CBData.LogZBias = -std::log(View.NearZ) * CBData.LogZScale;

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(DeviceContext->Map(FogClusterConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        return false;
    }
    std::memcpy(Mapped.pData, &CBData, sizeof(CBData));
    DeviceContext->Unmap(FogClusterConstantBuffer, 0);
    return true;
}

bool FFogRenderFeature::BuildFogClusters(const FViewContext& View, const TArray<FFogRenderItem>& Items)
{
    PreparedGlobalFogItems.clear();
    PreparedLocalFogItems.clear();
    PreparedFogItems.clear();

    for (const FFogRenderItem& Item : Items)
    {
        if (Item.IsLocalFogVolume())
        {
            PreparedLocalFogItems.push_back(Item);
        }
        else
        {
            PreparedGlobalFogItems.push_back(Item);
        }
    }

    LastStats.Common.TotalFogVolumes = static_cast<uint32>(Items.size());
    LastStats.Common.GlobalFogVolumes = static_cast<uint32>(PreparedGlobalFogItems.size());
    LastStats.Common.LocalFogVolumes = static_cast<uint32>(PreparedLocalFogItems.size());
    LastStats.Global.VolumeCount = LastStats.Common.GlobalFogVolumes;
    LastStats.ClusteredLocal.VolumeCount = LastStats.Common.LocalFogVolumes;

    PreparedFogItems.reserve(PreparedGlobalFogItems.size() + PreparedLocalFogItems.size());
    PreparedFogItems.insert(PreparedFogItems.end(), PreparedGlobalFogItems.begin(), PreparedGlobalFogItems.end());
    PreparedFogItems.insert(PreparedFogItems.end(), PreparedLocalFogItems.begin(), PreparedLocalFogItems.end());

    const uint32 ClusterCount = FOG_CLUSTER_COUNT_X * FOG_CLUSTER_COUNT_Y * FOG_CLUSTER_COUNT_Z;
    LastStats.Common.ClusterCount = ClusterCount;

    std::vector<std::vector<uint32>> ClusterLists(ClusterCount);
    uint32 RegisteredLocalFogCount = 0u;
    for (uint32 LocalFogIndex = 0; LocalFogIndex < static_cast<uint32>(PreparedLocalFogItems.size()); ++LocalFogIndex)
    {
        FFogClusterRange Range;
        const bool bRangeComputed = ComputeFogClusterRange(View, PreparedLocalFogItems[LocalFogIndex], Range);
        if (!bRangeComputed || !Range.bValid)
        {
            continue;
        }

        bool bRegisteredThisFog = false;
        for (uint32 Z = Range.MinSliceZ; Z <= Range.MaxSliceZ; ++Z)
        {
            for (uint32 Y = Range.MinTileY; Y <= Range.MaxTileY; ++Y)
            {
                for (uint32 X = Range.MinTileX; X <= Range.MaxTileX; ++X)
                {
                    std::vector<uint32>& Cluster = ClusterLists[FlattenClusterIndex(X, Y, Z)];
                    if (Cluster.size() >= FOG_MAX_CLUSTER_ITEMS)
                    {
                        continue;
                    }
                    Cluster.push_back(LocalFogIndex);
                    bRegisteredThisFog = true;
                }
            }
        }

        if (bRegisteredThisFog)
        {
            ++RegisteredLocalFogCount;
        }
        else
        {
            UE_LOG("[FogCluster] Local[%u] computed a valid range but registered to 0 clusters.", LocalFogIndex);
        }
    }

    ClusterHeadersCPU.clear();
    ClusterHeadersCPU.resize(ClusterCount * 4u, 0u);
    ClusterIndexListCPU.clear();
    ClusterIndexListCPU.reserve(PreparedLocalFogItems.size() * 8u);

    uint32 NonEmptyClusterCount = 0u;
    uint32 MaxItemsInSingleCluster = 0u;
    for (uint32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
    {
        const uint32 Offset = static_cast<uint32>(ClusterIndexListCPU.size());
        const std::vector<uint32>& Cluster = ClusterLists[ClusterIndex];
        for (uint32 FogIndex : Cluster)
        {
            ClusterIndexListCPU.push_back(FogIndex);
        }

        ClusterHeadersCPU[ClusterIndex * 4u + 0u] = Offset;
        ClusterHeadersCPU[ClusterIndex * 4u + 1u] = static_cast<uint32>(Cluster.size());
        if (!Cluster.empty())
        {
            ++NonEmptyClusterCount;
            MaxItemsInSingleCluster = std::max(MaxItemsInSingleCluster, static_cast<uint32>(Cluster.size()));
        }
    }

    LastStats.Common.RegisteredLocalFogVolumes = RegisteredLocalFogCount;
    LastStats.Common.NonEmptyClusterCount = NonEmptyClusterCount;
    LastStats.Common.ClusterIndexCount = static_cast<uint32>(ClusterIndexListCPU.size());
    LastStats.Common.MaxFogPerCluster = MaxItemsInSingleCluster;
    LastStats.ClusteredLocal.RegisteredVolumeCount = RegisteredLocalFogCount;
    LastStats.ClusteredLocal.ClusterCount = ClusterCount;
    LastStats.ClusteredLocal.NonEmptyClusterCount = NonEmptyClusterCount;
    LastStats.ClusteredLocal.ClusterIndexCount = LastStats.Common.ClusterIndexCount;
    LastStats.ClusteredLocal.MaxFogPerCluster = MaxItemsInSingleCluster;
    LastStats.ClusteredLocal.AvgFogPerCluster = ClusterCount > 0u
        ? static_cast<double>(LastStats.Common.ClusterIndexCount) / static_cast<double>(ClusterCount)
        : 0.0;
    LastStats.ClusteredLocal.AvgFogPerNonEmptyCluster = NonEmptyClusterCount > 0u
        ? static_cast<double>(LastStats.Common.ClusterIndexCount) / static_cast<double>(NonEmptyClusterCount)
        : 0.0;
    LastStats.ClusteredLocal.AvgClusterRegistrationsPerRegisteredVolume = RegisteredLocalFogCount > 0u
        ? static_cast<double>(LastStats.Common.ClusterIndexCount) / static_cast<double>(RegisteredLocalFogCount)
        : 0.0;

    return true;
}

namespace
{
    static bool UploadFogBufferCommon(FRenderer& Renderer, const TArray<FFogRenderItem>& Items, ID3D11Buffer*& Buffer, ID3D11ShaderResourceView*& BufferSRV)
    {
        ID3D11Device* Device = Renderer.GetDevice();
        ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
        if (!Device || !Context || Items.empty())
        {
            return false;
        }

        std::vector<FFogGPUData> GPUItems;
        GPUItems.reserve(Items.size());
        for (const FFogRenderItem& Item : Items)
        {
            FFogGPUData Data = {};
            Data.WorldToFogVolume = Item.WorldToFogVolume.GetTransposed();
            Data.FogOrigin = FVector4(Item.FogOrigin.X, Item.FogOrigin.Y, Item.FogOrigin.Z, 0.0f);
            Data.FogColor = Item.FogInscatteringColor.ToVector4();
            Data.FogParams = FVector4(Item.FogDensity, Item.FogHeightFalloff, Item.StartDistance, Item.FogCutoffDistance);
            Data.FogParams2 = FVector4(Item.FogMaxOpacity, Item.AllowBackground, Item.IsLocalFogVolume() ? 1.0f : 0.0f, 0.0f);
            GPUItems.push_back(Data);
        }

        const uint32 ElementCount = static_cast<uint32>(GPUItems.size());
        const uint32 ElementSize = static_cast<uint32>(sizeof(FFogGPUData));
        const uint32 RequiredByteWidth = Align16(ElementCount * ElementSize);

        bool bNeedsCreate = !Buffer || !BufferSRV;
        if (!bNeedsCreate)
        {
            D3D11_BUFFER_DESC Desc = {};
            Buffer->GetDesc(&Desc);
            bNeedsCreate = Desc.ByteWidth < RequiredByteWidth;
        }

        if (bNeedsCreate)
        {
            if (BufferSRV) { BufferSRV->Release(); BufferSRV = nullptr; }
            if (Buffer) { Buffer->Release(); Buffer = nullptr; }

            D3D11_BUFFER_DESC BufferDesc = {};
            BufferDesc.ByteWidth = RequiredByteWidth;
            BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
            BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            BufferDesc.StructureByteStride = ElementSize;
            if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &Buffer)))
            {
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
            SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
            SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            SRVDesc.Buffer.FirstElement = 0;
            SRVDesc.Buffer.NumElements = ElementCount;
            if (FAILED(Device->CreateShaderResourceView(Buffer, &SRVDesc, &BufferSRV)))
            {
                Buffer->Release();
                Buffer = nullptr;
                return false;
            }
        }

        D3D11_MAPPED_SUBRESOURCE Mapped = {};
        if (FAILED(Context->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
        {
            return false;
        }
        std::memcpy(Mapped.pData, GPUItems.data(), ElementCount * ElementSize);
        Context->Unmap(Buffer, 0);
        return true;
    }
}

bool FFogRenderFeature::UploadGlobalFogStructuredBuffer(FRenderer& Renderer)
{
    return UploadFogBufferCommon(Renderer, PreparedGlobalFogItems, GlobalFogStructuredBuffer, GlobalFogStructuredBufferSRV);
}

bool FFogRenderFeature::UploadLocalFogStructuredBuffer(FRenderer& Renderer)
{
    return UploadFogBufferCommon(Renderer, PreparedLocalFogItems, LocalFogStructuredBuffer, LocalFogStructuredBufferSRV);
}

bool FFogRenderFeature::UploadClusterHeaderStructuredBuffer(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
    if (!Device || !Context || ClusterHeadersCPU.empty())
    {
        return false;
    }

    const uint32 ElementCount = static_cast<uint32>(ClusterHeadersCPU.size() / 4u);
    const uint32 ElementSize = static_cast<uint32>(sizeof(FFogClusterHeaderGPU));
    const uint32 RequiredByteWidth = Align16(ElementCount * ElementSize);

    bool bNeedsCreate = !ClusterHeaderStructuredBuffer || !ClusterHeaderStructuredBufferSRV;
    if (!bNeedsCreate)
    {
        D3D11_BUFFER_DESC Desc = {};
        ClusterHeaderStructuredBuffer->GetDesc(&Desc);
        bNeedsCreate = Desc.ByteWidth < RequiredByteWidth;
    }

    if (bNeedsCreate)
    {
        if (ClusterHeaderStructuredBufferSRV) { ClusterHeaderStructuredBufferSRV->Release(); ClusterHeaderStructuredBufferSRV = nullptr; }
        if (ClusterHeaderStructuredBuffer) { ClusterHeaderStructuredBuffer->Release(); ClusterHeaderStructuredBuffer = nullptr; }

        D3D11_BUFFER_DESC BufferDesc = {};
        BufferDesc.ByteWidth = RequiredByteWidth;
        BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        BufferDesc.StructureByteStride = ElementSize;
        if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &ClusterHeaderStructuredBuffer)))
        {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.FirstElement = 0;
        SRVDesc.Buffer.NumElements = ElementCount;
        if (FAILED(Device->CreateShaderResourceView(ClusterHeaderStructuredBuffer, &SRVDesc, &ClusterHeaderStructuredBufferSRV)))
        {
            ClusterHeaderStructuredBuffer->Release();
            ClusterHeaderStructuredBuffer = nullptr;
            return false;
        }
    }

    std::vector<FFogClusterHeaderGPU> Headers;
    Headers.resize(ElementCount);
    for (uint32 i = 0; i < ElementCount; ++i)
    {
        Headers[i].Offset = ClusterHeadersCPU[i * 4u + 0u];
        Headers[i].Count = ClusterHeadersCPU[i * 4u + 1u];
    }

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(Context->Map(ClusterHeaderStructuredBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        return false;
    }
    std::memcpy(Mapped.pData, Headers.data(), ElementCount * ElementSize);
    Context->Unmap(ClusterHeaderStructuredBuffer, 0);
    return true;
}

bool FFogRenderFeature::UploadClusterIndexStructuredBuffer(FRenderer& Renderer)
{
    ID3D11Device* Device = Renderer.GetDevice();
    ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
    if (!Device || !Context)
    {
        return false;
    }

    const uint32 ElementCount = (std::max)(1u, static_cast<uint32>(ClusterIndexListCPU.size()));
    const uint32 ElementSize = static_cast<uint32>(sizeof(uint32));
    const uint32 RequiredByteWidth = Align16(ElementCount * ElementSize);

    bool bNeedsCreate = !ClusterIndexStructuredBuffer || !ClusterIndexStructuredBufferSRV;
    if (!bNeedsCreate)
    {
        D3D11_BUFFER_DESC Desc = {};
        ClusterIndexStructuredBuffer->GetDesc(&Desc);
        bNeedsCreate = Desc.ByteWidth < RequiredByteWidth;
    }

    if (bNeedsCreate)
    {
        if (ClusterIndexStructuredBufferSRV) { ClusterIndexStructuredBufferSRV->Release(); ClusterIndexStructuredBufferSRV = nullptr; }
        if (ClusterIndexStructuredBuffer) { ClusterIndexStructuredBuffer->Release(); ClusterIndexStructuredBuffer = nullptr; }

        D3D11_BUFFER_DESC BufferDesc = {};
        BufferDesc.ByteWidth = RequiredByteWidth;
        BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        BufferDesc.StructureByteStride = ElementSize;
        if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &ClusterIndexStructuredBuffer)))
        {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
        SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
        SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        SRVDesc.Buffer.FirstElement = 0;
        SRVDesc.Buffer.NumElements = ElementCount;
        if (FAILED(Device->CreateShaderResourceView(ClusterIndexStructuredBuffer, &SRVDesc, &ClusterIndexStructuredBufferSRV)))
        {
            ClusterIndexStructuredBuffer->Release();
            ClusterIndexStructuredBuffer = nullptr;
            return false;
        }
    }

    std::vector<uint32> UploadData = ClusterIndexListCPU;
    if (UploadData.empty())
    {
        UploadData.push_back(0u);
    }

    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (FAILED(Context->Map(ClusterIndexStructuredBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        return false;
    }
    std::memcpy(Mapped.pData, UploadData.data(), UploadData.size() * ElementSize);
    Context->Unmap(ClusterIndexStructuredBuffer, 0);
    return true;
}

bool FFogRenderFeature::Render(FRenderer& Renderer, const FFrameContext& Frame, const FViewContext& View, FSceneRenderTargets& Targets, const TArray<FFogRenderItem>& Items)
{
    LastStats = {};
    if (Items.empty()
        || !Targets.GetSceneColorRenderTarget()
        || !Targets.GetSceneColorWriteRenderTarget()
        || !Targets.GetSceneColorShaderResource()
        || !Targets.SceneDepthSRV
        || !Initialize(Renderer))
    {
        return true;
    }

    ID3D11DeviceContext* Context = Renderer.GetDeviceContext();
    if (!Context)
    {
        return false;
    }

    const auto TotalStart = std::chrono::high_resolution_clock::now();

    const auto ClusterBuildStart = std::chrono::high_resolution_clock::now();
    if (!BuildFogClusters(View, Items) || PreparedFogItems.empty())
    {
        return false;
    }
    const auto ClusterBuildEnd = std::chrono::high_resolution_clock::now();
    LastStats.Common.ClusterBuildTimeMs = std::chrono::duration<double, std::milli>(ClusterBuildEnd - ClusterBuildStart).count();
    LastStats.ClusteredLocal.ClusterBuildTimeMs = LastStats.Common.ClusterBuildTimeMs;

    const auto ConstantBufferStart = std::chrono::high_resolution_clock::now();
    if (!UpdateFogCompositeConstantBuffer(Renderer, View, static_cast<uint32>(PreparedFogItems.size()), static_cast<uint32>(PreparedGlobalFogItems.size()), static_cast<uint32>(PreparedLocalFogItems.size()))
        || !UpdateFogClusterConstantBuffer(Renderer, View))
    {
        return false;
    }
    const auto ConstantBufferEnd = std::chrono::high_resolution_clock::now();
    LastStats.Common.ConstantBufferUpdateTimeMs = std::chrono::duration<double, std::milli>(ConstantBufferEnd - ConstantBufferStart).count();
    LastStats.Global.ConstantBufferUpdateTimeMs = LastStats.Common.ConstantBufferUpdateTimeMs;

    const auto UploadStart = std::chrono::high_resolution_clock::now();
    if ((!PreparedGlobalFogItems.empty() && !UploadGlobalFogStructuredBuffer(Renderer))
        || (!PreparedLocalFogItems.empty() && !UploadLocalFogStructuredBuffer(Renderer))
        || !UploadClusterHeaderStructuredBuffer(Renderer)
        || !UploadClusterIndexStructuredBuffer(Renderer))
    {
        return false;
    }
    const auto UploadEnd = std::chrono::high_resolution_clock::now();
    LastStats.Common.StructuredBufferUploadTimeMs = std::chrono::duration<double, std::milli>(UploadEnd - UploadStart).count();
    LastStats.ClusteredLocal.StructuredBufferUploadTimeMs = LastStats.Common.StructuredBufferUploadTimeMs;

    LastStats.Common.GlobalFogBufferBytes = static_cast<uint64>(PreparedGlobalFogItems.size()) * sizeof(FFogGPUData);
    LastStats.Common.LocalFogBufferBytes = static_cast<uint64>(PreparedLocalFogItems.size()) * sizeof(FFogGPUData);
    LastStats.Common.ClusterHeaderBufferBytes = static_cast<uint64>((FOG_CLUSTER_COUNT_X * FOG_CLUSTER_COUNT_Y * FOG_CLUSTER_COUNT_Z)) * sizeof(FFogClusterHeaderGPU);
    LastStats.Common.ClusterIndexBufferBytes = static_cast<uint64>(ClusterIndexListCPU.size()) * sizeof(uint32);
    LastStats.Common.TotalUploadBytes = LastStats.Common.GlobalFogBufferBytes
        + LastStats.Common.LocalFogBufferBytes
        + LastStats.Common.ClusterHeaderBufferBytes
        + LastStats.Common.ClusterIndexBufferBytes;
    LastStats.Common.FullscreenPassCount = 1;
    LastStats.Common.DrawCallCount = 1;
    LastStats.Common.SceneColorCopyBytes = 0u;
    LastStats.Global.FullscreenPassCount = LastStats.Common.FullscreenPassCount;
    LastStats.Global.DrawCallCount = LastStats.Common.DrawCallCount;
    LastStats.Global.BufferBytes = LastStats.Common.GlobalFogBufferBytes;
    LastStats.Global.SceneColorCopyBytes = LastStats.Common.SceneColorCopyBytes;
    LastStats.ClusteredLocal.LocalFogBufferBytes = LastStats.Common.LocalFogBufferBytes;
    LastStats.ClusteredLocal.ClusterHeaderBufferBytes = LastStats.Common.ClusterHeaderBufferBytes;
    LastStats.ClusteredLocal.ClusterIndexBufferBytes = LastStats.Common.ClusterIndexBufferBytes;
    LastStats.ClusteredLocal.TotalUploadBytes = LastStats.Common.LocalFogBufferBytes
        + LastStats.Common.ClusterHeaderBufferBytes
        + LastStats.Common.ClusterIndexBufferBytes;

    const FFullscreenPassConstantBufferBinding ConstantBuffers[] =
    {
        { FOG_COMPOSITE_CB_SLOT, FogCompositeConstantBuffer },
        { FOG_CLUSTER_CB_SLOT, FogClusterConstantBuffer },
    };
    const FFullscreenPassShaderResourceBinding ShaderResources[] =
    {
        { FOG_SCENECOLOR_SRV_SLOT, Targets.GetSceneColorShaderResource() },
        { FOG_DEPTH_SRV_SLOT, Targets.SceneDepthSRV },
        { FOG_CLUSTER_HEADERS_SRV_SLOT, ClusterHeaderStructuredBufferSRV },
        { FOG_CLUSTER_INDICES_SRV_SLOT, ClusterIndexStructuredBufferSRV },
        { FOG_DATA_SRV_SLOT, LocalFogStructuredBufferSRV },
        { FOG_GLOBAL_DATA_SRV_SLOT, PreparedGlobalFogItems.empty() ? nullptr : GlobalFogStructuredBufferSRV },
    };
    const FFullscreenPassSamplerBinding Samplers[] =
    {
        { FOG_SCENECOLOR_SAMPLER_SLOT, LinearSampler },
        { FOG_DEPTH_SAMPLER_SLOT, DepthSampler },
    };
    const FFullscreenPassBindings Bindings
    {
        ConstantBuffers,
        static_cast<uint32>(sizeof(ConstantBuffers) / sizeof(ConstantBuffers[0])),
        ShaderResources,
        static_cast<uint32>(sizeof(ShaderResources) / sizeof(ShaderResources[0])),
        Samplers,
        static_cast<uint32>(sizeof(Samplers) / sizeof(Samplers[0]))
    };

    FFullscreenPassPipelineState PipelineState;
    PipelineState.DepthStencilState = NoDepthState;
    PipelineState.RasterizerState = FogRasterizerState;

    const auto ShadingStart = std::chrono::high_resolution_clock::now();
    const bool bRendered = ExecuteFullscreenPass(
        Renderer,
        Frame,
        View,
        Targets.GetSceneColorWriteRenderTarget(),
        nullptr,
        View.Viewport,
        { FogPostVS, FogPostPS },
        PipelineState,
        Bindings,
        [&](ID3D11DeviceContext& DrawContext)
        {
            DrawContext.Draw(3, 0);
        });

    const auto ShadingEnd = std::chrono::high_resolution_clock::now();
    LastStats.Common.ShadingPassTimeMs = std::chrono::duration<double, std::milli>(ShadingEnd - ShadingStart).count();
    LastStats.Common.TotalFogTimeMs = std::chrono::duration<double, std::milli>(ShadingEnd - TotalStart).count();
    LastStats.Global.ShadingPassTimeMs = LastStats.Common.ShadingPassTimeMs;
    if (bRendered)
    {
        Targets.SwapSceneColor();
    }
    return bRendered;
}

void FFogRenderFeature::Release()
{
    auto SafeRelease = [](IUnknown*& Resource)
    {
        if (Resource)
        {
            Resource->Release();
            Resource = nullptr;
        }
    };

    SafeRelease(reinterpret_cast<IUnknown*&>(FogCompositeConstantBuffer));
    SafeRelease(reinterpret_cast<IUnknown*&>(FogClusterConstantBuffer));
    SafeRelease(reinterpret_cast<IUnknown*&>(NoDepthState));
    SafeRelease(reinterpret_cast<IUnknown*&>(FogRasterizerState));
    SafeRelease(reinterpret_cast<IUnknown*&>(LinearSampler));
    SafeRelease(reinterpret_cast<IUnknown*&>(DepthSampler));
    FogPostVS.reset();
    FogPostPS.reset();
    SafeRelease(reinterpret_cast<IUnknown*&>(LocalFogStructuredBuffer));
    SafeRelease(reinterpret_cast<IUnknown*&>(LocalFogStructuredBufferSRV));
    SafeRelease(reinterpret_cast<IUnknown*&>(GlobalFogStructuredBuffer));
    SafeRelease(reinterpret_cast<IUnknown*&>(GlobalFogStructuredBufferSRV));
    SafeRelease(reinterpret_cast<IUnknown*&>(ClusterHeaderStructuredBuffer));
    SafeRelease(reinterpret_cast<IUnknown*&>(ClusterHeaderStructuredBufferSRV));
    SafeRelease(reinterpret_cast<IUnknown*&>(ClusterIndexStructuredBuffer));
    SafeRelease(reinterpret_cast<IUnknown*&>(ClusterIndexStructuredBufferSRV));

    PreparedFogItems.clear();
    PreparedGlobalFogItems.clear();
    PreparedLocalFogItems.clear();
    ClusterHeadersCPU.clear();
    ClusterIndexListCPU.clear();
}
