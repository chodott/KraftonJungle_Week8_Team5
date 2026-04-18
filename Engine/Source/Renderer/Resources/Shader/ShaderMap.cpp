#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderResource.h"

#include <cstring>

FShaderMap& FShaderMap::Get()
{
	static FShaderMap Instance;
	return Instance;
}

std::shared_ptr<FVertexShader> FShaderMap::GetOrCreateVertexShader(
	ID3D11Device* Device,
	const wchar_t* FilePath)
{
	return GetOrCreateVertexShader(Device, FilePath, EVertexLayoutType::MeshVertex);
}

std::shared_ptr<FVertexShader> FShaderMap::GetOrCreateVertexShader(
	ID3D11Device* Device,
	const wchar_t* FilePath,
	EVertexLayoutType LayoutType)
{
	std::wstring Key(FilePath);
	Key += L"#";
	Key += std::to_wstring(static_cast<unsigned int>(LayoutType));

	auto It = VertexShaders.find(Key);
	if (It != VertexShaders.end())
	{
		return It->second;
	}

	auto Resource = FShaderResource::GetOrCompile(FilePath, "main", "vs_5_0");
	if (!Resource)
	{
		return nullptr;
	}

	auto VS = FVertexShader::Create(Device, Resource, LayoutType);
	if (!VS)
	{
		return nullptr;
	}

	VertexShaders[Key] = VS;
	return VS;
}

std::shared_ptr<FPixelShader> FShaderMap::GetOrCreatePixelShader(
	ID3D11Device* Device,
	const wchar_t* FilePath)
{
	std::wstring Key(FilePath);

	auto It = PixelShaders.find(Key);
	if (It != PixelShaders.end())
	{
		return It->second;
	}

	auto Resource = FShaderResource::GetOrCompile(FilePath, "main", "ps_5_0");
	if (!Resource)
	{
		return nullptr;
	}

	auto PS = FPixelShader::Create(Device, Resource);
	if (!PS)
	{
		return nullptr;
	}

	PixelShaders[Key] = PS;
	return PS;
}

std::shared_ptr<FComputeShader> FShaderMap::GetOrCreateComputeShader(
	ID3D11Device* Device,
	const wchar_t* FilePath,
	const char* EntryPoint)
{
	std::wstring Key(FilePath);
	Key += L"#";
	for (const char* It = EntryPoint; It && *It != '\0'; ++It)
	{
		Key.push_back(static_cast<wchar_t>(*It));
	}
	Key += L"#cs_5_0";

	auto It = ComputeShaders.find(Key);
	if (It != ComputeShaders.end())
	{
		return It->second;
	}

	auto Resource = FShaderResource::GetOrCompile(FilePath, EntryPoint, "cs_5_0");
	if (!Resource)
	{
		return nullptr;
	}

	auto CS = FComputeShader::Create(Device, Resource);
	if (!CS)
	{
		return nullptr;
	}

	ComputeShaders[Key] = CS;
	return CS;
}

void FShaderMap::Clear()
{
	VertexShaders.clear();
	PixelShaders.clear();
	ComputeShaders.clear();
	FShaderResource::ClearCache();
}
