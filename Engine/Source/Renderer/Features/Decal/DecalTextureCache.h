#pragma once

#include "CoreMinimal.h"

#include <d3d11.h>
#include <filesystem>
#include <vector>

struct FSceneViewData;

class ENGINE_API FDecalTextureCache
{
public:
    bool InitializeFallbackTexture(ID3D11Device* Device);
    void ResolveTextureArray(ID3D11Device* Device, FSceneViewData& InOutSceneViewData);
    void Release();

private:
    static bool CreateSolidColorTextureSRV(ID3D11Device* Device, uint32 PackedRGBA, ID3D11ShaderResourceView** OutSRV);
    static bool LoadTexturePixels(const std::wstring& TexturePath, std::vector<unsigned char>& OutPixels, uint32& OutWidth, uint32& OutHeight);
    ID3D11ShaderResourceView* GetOrLoadBaseColorTexture(ID3D11Device* Device, const std::wstring& TexturePath);

private:
    ID3D11ShaderResourceView* FallbackBaseColorSRV = nullptr;
    TMap<std::wstring, ID3D11ShaderResourceView*> BaseColorTextureCache;
    ID3D11Texture2D* BaseColorTextureArrayResource = nullptr;
    ID3D11ShaderResourceView* BaseColorTextureArraySRV = nullptr;
    TArray<std::wstring> BaseColorTextureArrayPaths;
};
