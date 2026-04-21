#pragma once

#include "EngineAPI.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderHandles.h"
#include <d3d11.h>
#include <string>
#include <unordered_map>
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

private:
	FShaderMap() = default;

	std::unordered_map<std::wstring, std::shared_ptr<FVertexShader>>  VertexShaders;
	std::unordered_map<std::wstring, std::shared_ptr<FPixelShader>>   PixelShaders;
	std::unordered_map<std::wstring, std::shared_ptr<FComputeShader>> ComputeShaders;
};
