#include "Renderer/Resources/Shader/ShaderMap.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderResource.h"

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

void FShaderMap::Clear()
{
	VertexShaders.clear();
	PixelShaders.clear();
	FShaderResource::ClearCache();
}
