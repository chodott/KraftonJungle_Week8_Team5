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
	SIZE_T      GetBufferSize() const;

	static std::shared_ptr<FShaderResource> CreateFromBytecode(const void* Data, SIZE_T Size);

	static void ClearCache();
	static void SetContentDir(const wchar_t* Dir);

private:
	FShaderResource() = default;

	ID3DBlob*            ShaderBlob = nullptr;
	std::vector<uint8_t> RawData;
	bool                 bFromCso = false;

	static std::unordered_map<std::wstring, std::shared_ptr<FShaderResource>> Cache;
	static std::wstring                                                       ContentDir;
};
