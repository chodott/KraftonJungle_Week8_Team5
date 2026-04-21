#include "Renderer/Resources/Shader/ShaderHandles.h"

#include "Renderer/Resources/Shader/ShaderResource.h"

namespace
{
	std::shared_ptr<FShaderResource> MakeResourceFromBytecode(const std::vector<uint8>& Bytecode)
	{
		return FShaderResource::CreateFromBytecode(Bytecode.data(), Bytecode.size());
	}
}

FVertexShaderHandle::FVertexShaderHandle(const FShaderRecipe& InRecipe)
	: Recipe(InRecipe)
	, Key(MakeShaderRecipeKey(InRecipe))
{
}

std::shared_ptr<FVertexShader> FVertexShaderHandle::GetCurrent() const
{
	std::shared_lock<std::shared_mutex> Lock(Mutex);
	return Current;
}

void FVertexShaderHandle::Bind(ID3D11DeviceContext* DeviceContext) const
{
	const std::shared_ptr<FVertexShader> Shader = GetCurrent();
	if (Shader)
	{
		Shader->Bind(DeviceContext);
	}
}

bool FVertexShaderHandle::RebuildFromArtifact(ID3D11Device* Device, const FShaderCompileArtifact& Artifact, std::string& OutError)
{
	const std::shared_ptr<FShaderResource> Resource = MakeResourceFromBytecode(Artifact.Bytecode);
	if (!Resource)
	{
		OutError = "Failed to create shader resource from bytecode.";
		return false;
	}

	const std::shared_ptr<FVertexShader> NewShader = FVertexShader::Create(Device, Resource, Recipe.LayoutType);
	if (!NewShader)
	{
		OutError = "Failed to create vertex shader object.";
		return false;
	}

	{
		std::unique_lock<std::shared_mutex> Lock(Mutex);
		Current = NewShader;
	}

	return true;
}

FPixelShaderHandle::FPixelShaderHandle(const FShaderRecipe& InRecipe)
	: Recipe(InRecipe)
	, Key(MakeShaderRecipeKey(InRecipe))
{
}

std::shared_ptr<FPixelShader> FPixelShaderHandle::GetCurrent() const
{
	std::shared_lock<std::shared_mutex> Lock(Mutex);
	return Current;
}

void FPixelShaderHandle::Bind(ID3D11DeviceContext* DeviceContext) const
{
	const std::shared_ptr<FPixelShader> Shader = GetCurrent();
	if (Shader)
	{
		Shader->Bind(DeviceContext);
	}
}

bool FPixelShaderHandle::RebuildFromArtifact(ID3D11Device* Device, const FShaderCompileArtifact& Artifact, std::string& OutError)
{
	const std::shared_ptr<FShaderResource> Resource = MakeResourceFromBytecode(Artifact.Bytecode);
	if (!Resource)
	{
		OutError = "Failed to create shader resource from bytecode.";
		return false;
	}

	const std::shared_ptr<FPixelShader> NewShader = FPixelShader::Create(Device, Resource);
	if (!NewShader)
	{
		OutError = "Failed to create pixel shader object.";
		return false;
	}

	{
		std::unique_lock<std::shared_mutex> Lock(Mutex);
		Current = NewShader;
	}

	return true;
}

FComputeShaderHandle::FComputeShaderHandle(const FShaderRecipe& InRecipe)
	: Recipe(InRecipe)
	, Key(MakeShaderRecipeKey(InRecipe))
{
}

std::shared_ptr<FComputeShader> FComputeShaderHandle::GetCurrent() const
{
	std::shared_lock<std::shared_mutex> Lock(Mutex);
	return Current;
}

void FComputeShaderHandle::Bind(ID3D11DeviceContext* DeviceContext) const
{
	const std::shared_ptr<FComputeShader> Shader = GetCurrent();
	if (Shader)
	{
		Shader->Bind(DeviceContext);
	}
}

bool FComputeShaderHandle::RebuildFromArtifact(ID3D11Device* Device, const FShaderCompileArtifact& Artifact, std::string& OutError)
{
	const std::shared_ptr<FShaderResource> Resource = MakeResourceFromBytecode(Artifact.Bytecode);
	if (!Resource)
	{
		OutError = "Failed to create shader resource from bytecode.";
		return false;
	}

	const std::shared_ptr<FComputeShader> NewShader = FComputeShader::Create(Device, Resource);
	if (!NewShader)
	{
		OutError = "Failed to create compute shader object.";
		return false;
	}

	{
		std::unique_lock<std::shared_mutex> Lock(Mutex);
		Current = NewShader;
	}

	return true;
}
