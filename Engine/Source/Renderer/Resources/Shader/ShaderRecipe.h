#pragma once

#include "CoreMinimal.h"
#include "Renderer/Resources/Shader/Shader.h"

#include <d3dcompiler.h>

#include <cstdint>
#include <string>
#include <vector>

enum class EShaderStage : uint8
{
	Vertex,
	Pixel,
	Compute,
};

struct FShaderMacroDesc
{
	std::string Name;
	std::string Value;

	bool operator==(const FShaderMacroDesc& Other) const
	{
		return Name == Other.Name && Value == Other.Value;
	}
};

struct FShaderRecipe
{
	EShaderStage Stage = EShaderStage::Pixel;
	std::wstring SourcePath;
	std::string EntryPoint = "main";
	std::string Target;
	std::vector<FShaderMacroDesc> Macros;
	EVertexLayoutType LayoutType = EVertexLayoutType::MeshVertex;
	std::wstring DebugName;
};

struct FShaderRecipeKey
{
	uint64 Hash = 0;

	bool operator==(const FShaderRecipeKey& Other) const
	{
		return Hash == Other.Hash;
	}
};

struct FShaderRecipeKeyHasher
{
	size_t operator()(const FShaderRecipeKey& Key) const
	{
		return static_cast<size_t>(Key.Hash);
	}
};

ENGINE_API FShaderRecipeKey MakeShaderRecipeKey(const FShaderRecipe& Recipe);
ENGINE_API std::wstring MakeShaderRecipeDebugString(const FShaderRecipe& Recipe);
ENGINE_API std::vector<D3D_SHADER_MACRO> BuildD3DShaderMacros(const FShaderRecipe& Recipe);
