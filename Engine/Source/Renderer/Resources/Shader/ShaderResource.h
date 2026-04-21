#pragma once

#include "EngineAPI.h"

#include <d3d11.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ENGINE_API FShaderResource
{
public:
	~FShaderResource();

	const void* GetBufferPointer() const;
	SIZE_T GetBufferSize() const;

	static std::shared_ptr<FShaderResource> CreateFromBytecode(const void* Data, SIZE_T Size);
	static std::shared_ptr<FShaderResource> GetOrCompile(
		const wchar_t* FilePath,
		const char* EntryPoint,
		const char* Target,
		const D3D_SHADER_MACRO* Defines = nullptr);

	static void ClearCache();
	static void SetContentDir(const wchar_t* Dir);

private:
	FShaderResource() = default;

	static std::wstring MakeCacheKey(
		const wchar_t* HlslPath,
		const char* EntryPoint,
		const char* Target,
		const D3D_SHADER_MACRO* Defines);

	static std::wstring MakeCsoPath(
		const wchar_t* HlslPath,
		const char* EntryPoint,
		const char* Target,
		const D3D_SHADER_MACRO* Defines);

	static bool IsHlslNewer(const wchar_t* HlslPath, const wchar_t* CsoPath);
	static bool SaveCso(const wchar_t* CsoPath, const void* Data, SIZE_T Size);
	static std::shared_ptr<FShaderResource> LoadCso(const wchar_t* CsoPath);

	ID3DBlob* ShaderBlob = nullptr;
	std::vector<uint8_t> RawData;
	bool bFromCso = false;

	static std::unordered_map<std::wstring, std::shared_ptr<FShaderResource>> Cache;
	static std::wstring ContentDir;
};
