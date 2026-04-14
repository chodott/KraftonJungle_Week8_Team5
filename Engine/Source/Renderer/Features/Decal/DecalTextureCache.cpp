#include "Renderer/Features/Decal/DecalTextureCache.h"

#include "Renderer/Scene/SceneViewData.h"
#include "ThirdParty/stb_image.h"
#include "Debug/EngineLog.h"

#include <fstream>
#include <vector>

namespace
{
    static constexpr uint32 DECAL_MAX_TEXTURE_SLICES = 16;
}

bool FDecalTextureCache::CreateSolidColorTextureSRV(ID3D11Device* Device, uint32 PackedRGBA, ID3D11ShaderResourceView** OutSRV)
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

bool FDecalTextureCache::LoadTexturePixels(const std::wstring& TexturePath, std::vector<unsigned char>& OutPixels, uint32& OutWidth, uint32& OutHeight)
{
    OutPixels.clear();
    OutWidth = 0;
    OutHeight = 0;

    std::ifstream File(TexturePath, std::ios::binary | std::ios::ate);
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
    if (!Data)
    {
        return false;
    }

    OutWidth = static_cast<uint32>(W);
    OutHeight = static_cast<uint32>(H);
    OutPixels.assign(Data, Data + (W * H * 4));
    stbi_image_free(Data);
    return true;
}

bool FDecalTextureCache::InitializeFallbackTexture(ID3D11Device* Device)
{
    if (FallbackBaseColorSRV)
    {
        return true;
    }

    return CreateSolidColorTextureSRV(Device, 0xFFFFFFFFu, &FallbackBaseColorSRV);
}

ID3D11ShaderResourceView* FDecalTextureCache::GetOrLoadBaseColorTexture(ID3D11Device* Device, const std::wstring& TexturePath)
{
    if (TexturePath.empty())
    {
        return FallbackBaseColorSRV;
    }

    const std::wstring NormalizedPath = std::filesystem::path(TexturePath).lexically_normal().wstring();
    auto Found = BaseColorTextureCache.find(NormalizedPath);
    if (Found != BaseColorTextureCache.end())
    {
        return Found->second;
    }

    if (!Device)
    {
        return FallbackBaseColorSRV;
    }

    std::vector<unsigned char> Pixels;
    uint32 Width = 0;
    uint32 Height = 0;
    if (!LoadTexturePixels(NormalizedPath, Pixels, Width, Height) || Pixels.empty())
    {
        return FallbackBaseColorSRV;
    }

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA InitData = { Pixels.data(), Width * 4, 0 };
    ID3D11Texture2D* Texture = nullptr;
    if (FAILED(Device->CreateTexture2D(&Desc, &InitData, &Texture)) || !Texture)
    {
        return FallbackBaseColorSRV;
    }

    ID3D11ShaderResourceView* LoadedSRV = nullptr;
    const HRESULT Hr = Device->CreateShaderResourceView(Texture, nullptr, &LoadedSRV);
    Texture->Release();
    if (FAILED(Hr) || !LoadedSRV)
    {
        return FallbackBaseColorSRV;
    }

    BaseColorTextureCache.emplace(NormalizedPath, LoadedSRV);
    return LoadedSRV;
}

void FDecalTextureCache::ResolveTextureArray(ID3D11Device* Device, FSceneViewData& InOutSceneViewData)
{
    auto& DecalItems = InOutSceneViewData.PostProcessInputs.DecalItems;

    TArray<std::wstring> SlicePaths;
    SlicePaths.push_back(L"");
    TMap<std::wstring, uint32> PathToSlice;
    PathToSlice.emplace(L"", 0u);

    for (FDecalRenderItem& Item : DecalItems)
    {
        Item.TextureIndex = 0;
        if (Item.TexturePath.empty())
        {
            continue;
        }

        const std::wstring NormalizedPath = std::filesystem::path(Item.TexturePath).lexically_normal().wstring();
        auto Found = PathToSlice.find(NormalizedPath);
        if (Found != PathToSlice.end())
        {
            Item.TextureIndex = Found->second;
            continue;
        }

        if (SlicePaths.size() >= DECAL_MAX_TEXTURE_SLICES)
        {
            UE_LOG("[Decal] Texture array overflow (%u slices). Using fallback for %ls", static_cast<uint32>(DECAL_MAX_TEXTURE_SLICES), NormalizedPath.c_str());
            PathToSlice.emplace(NormalizedPath, 0u);
            Item.TextureIndex = 0;
            continue;
        }

        const uint32 NewIndex = static_cast<uint32>(SlicePaths.size());
        SlicePaths.push_back(NormalizedPath);
        PathToSlice.emplace(NormalizedPath, NewIndex);
        Item.TextureIndex = NewIndex;
    }

    if (SlicePaths == BaseColorTextureArrayPaths && BaseColorTextureArraySRV)
    {
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = BaseColorTextureArraySRV;
        return;
    }

    if (BaseColorTextureArraySRV)
    {
        BaseColorTextureArraySRV->Release();
        BaseColorTextureArraySRV = nullptr;
    }
    if (BaseColorTextureArrayResource)
    {
        BaseColorTextureArrayResource->Release();
        BaseColorTextureArrayResource = nullptr;
    }
    BaseColorTextureArrayPaths = SlicePaths;

    struct FSlicePixels
    {
        std::vector<unsigned char> Pixels;
        uint32 W = 0;
        uint32 H = 0;
    };

    const uint32 ArraySize = static_cast<uint32>(SlicePaths.size());
    std::vector<FSlicePixels> Slices(ArraySize);

    Slices[0].W = 1;
    Slices[0].H = 1;
    Slices[0].Pixels = { 255, 255, 255, 255 };

    uint32 CanonicalW = 0;
    uint32 CanonicalH = 0;

    for (uint32 i = 1; i < ArraySize; ++i)
    {
        const std::wstring& Path = SlicePaths[i];
        std::vector<unsigned char> Pixels;
        uint32 Width = 0;
        uint32 Height = 0;
        if (!LoadTexturePixels(Path, Pixels, Width, Height) || Pixels.empty())
        {
            UE_LOG("[Decal] Cannot load texture for array: %ls. Using fallback.", Path.c_str());
            continue;
        }

        if (CanonicalW == 0)
        {
            CanonicalW = Width;
            CanonicalH = Height;
        }

        if (Width == CanonicalW && Height == CanonicalH)
        {
            Slices[i].W = Width;
            Slices[i].H = Height;
            Slices[i].Pixels = std::move(Pixels);
        }
        else
        {
            UE_LOG("[Decal] Size mismatch for %ls (%ux%u vs canonical %ux%u). Using fallback.", Path.c_str(), Width, Height, CanonicalW, CanonicalH);
        }
    }

    if (CanonicalW == 0)
    {
        CanonicalW = 1;
        CanonicalH = 1;
    }

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

    if (!Device)
    {
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = nullptr;
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

    if (FAILED(Device->CreateTexture2D(&Desc, InitData.data(), &BaseColorTextureArrayResource)) || !BaseColorTextureArrayResource)
    {
        UE_LOG("[Decal] Failed to create Texture2DArray (%u slices, %ux%u).", ArraySize, CanonicalW, CanonicalH);
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = nullptr;
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVDesc.Texture2DArray.MostDetailedMip = 0;
    SRVDesc.Texture2DArray.MipLevels = 1;
    SRVDesc.Texture2DArray.FirstArraySlice = 0;
    SRVDesc.Texture2DArray.ArraySize = ArraySize;

    if (FAILED(Device->CreateShaderResourceView(BaseColorTextureArrayResource, &SRVDesc, &BaseColorTextureArraySRV)) || !BaseColorTextureArraySRV)
    {
        UE_LOG("[Decal] Failed to create Texture2DArray SRV.");
        InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = nullptr;
        return;
    }

    InOutSceneViewData.PostProcessInputs.DecalBaseColorTextureArraySRV = BaseColorTextureArraySRV;
}

void FDecalTextureCache::Release()
{
    for (auto& Entry : BaseColorTextureCache)
    {
        if (Entry.second)
        {
            Entry.second->Release();
        }
    }
    BaseColorTextureCache.clear();

    if (BaseColorTextureArraySRV)
    {
        BaseColorTextureArraySRV->Release();
        BaseColorTextureArraySRV = nullptr;
    }
    if (BaseColorTextureArrayResource)
    {
        BaseColorTextureArrayResource->Release();
        BaseColorTextureArrayResource = nullptr;
    }
    BaseColorTextureArrayPaths.clear();

    if (FallbackBaseColorSRV)
    {
        FallbackBaseColorSRV->Release();
        FallbackBaseColorSRV = nullptr;
    }
}
