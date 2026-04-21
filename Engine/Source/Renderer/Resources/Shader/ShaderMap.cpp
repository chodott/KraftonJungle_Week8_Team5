#include "Renderer/Resources/Shader/ShaderMap.h"

#include "Renderer/Resources/Shader/ShaderPathUtils.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"
#include "Renderer/Resources/Shader/ShaderResource.h"

FShaderMap& FShaderMap::Get()
{
	static FShaderMap Instance;
	return Instance;
}

std::shared_ptr<FVertexShaderHandle> FShaderMap::GetOrCreateVertexShader(
	ID3D11Device* Device,
	const wchar_t* FilePath)
{
	return GetOrCreateVertexShader(Device, FilePath, EVertexLayoutType::MeshVertex);
}

std::shared_ptr<FVertexShaderHandle> FShaderMap::GetOrCreateVertexShader(
	ID3D11Device* Device,
	const wchar_t* FilePath,
	EVertexLayoutType LayoutType)
{
	FShaderRecipe Recipe = {};
	Recipe.Stage = EShaderStage::Vertex;
	Recipe.SourcePath = ResolveShaderPath(FilePath);
	Recipe.EntryPoint = "main";
	Recipe.Target = "vs_5_0";
	Recipe.LayoutType = LayoutType;

	return FShaderRegistry::Get().GetOrCreateVertexShaderHandle(Device, Recipe);
}

std::shared_ptr<FPixelShaderHandle> FShaderMap::GetOrCreatePixelShader(
	ID3D11Device* Device,
	const wchar_t* FilePath)
{
	FShaderRecipe Recipe = {};
	Recipe.Stage = EShaderStage::Pixel;
	Recipe.SourcePath = ResolveShaderPath(FilePath);
	Recipe.EntryPoint = "main";
	Recipe.Target = "ps_5_0";

	return FShaderRegistry::Get().GetOrCreatePixelShaderHandle(Device, Recipe);
}

std::shared_ptr<FComputeShaderHandle> FShaderMap::GetOrCreateComputeShader(
	ID3D11Device* Device,
	const wchar_t* FilePath,
	const char* EntryPoint)
{
	FShaderRecipe Recipe = {};
	Recipe.Stage = EShaderStage::Compute;
	Recipe.SourcePath = ResolveShaderPath(FilePath);
	Recipe.EntryPoint = EntryPoint ? EntryPoint : "main";
	Recipe.Target = "cs_5_0";

	return FShaderRegistry::Get().GetOrCreateComputeShaderHandle(Device, Recipe);
}

void FShaderMap::Clear()
{
	FShaderRegistry::Get().Clear();
	FShaderResource::ClearCache();
}
