#pragma once

#include "EngineAPI.h"
#include "Renderer/Resources/Shader/ShaderHandles.h"

#include <memory>

class ENGINE_API FShaderMap
{
public:
	std::shared_ptr<FVertexShaderHandle> GetOrCreateVertexShader(
		ID3D11Device*  Device,
		const wchar_t* FilePath);

	std::shared_ptr<FVertexShaderHandle> GetOrCreateVertexShader(
		ID3D11Device*     Device,
		const wchar_t*    FilePath,
		EVertexLayoutType LayoutType);

	std::shared_ptr<FPixelShaderHandle> GetOrCreatePixelShader(
		ID3D11Device*  Device,
		const wchar_t* FilePath);

	std::shared_ptr<FComputeShaderHandle> GetOrCreateComputeShader(
		ID3D11Device*  Device,
		const wchar_t* FilePath,
		const char*    EntryPoint = "main");

	void Clear();

	static FShaderMap& Get();
};
