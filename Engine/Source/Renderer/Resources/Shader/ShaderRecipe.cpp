#include "Renderer/Resources/Shader/ShaderRecipe.h"

#include <algorithm>
#include <functional>

namespace
{
	uint64 HashCombine64(uint64 A, uint64 B)
	{
		return A ^ (B + 0x9e3779b97f4a7c15ull + (A << 6) + (A >> 2));
	}

	uint64 HashWString(const std::wstring& Value)
	{
		return static_cast<uint64>(std::hash<std::wstring> {}(Value));
	}

	uint64 HashString(const std::string& Value)
	{
		return static_cast<uint64>(std::hash<std::string> {}(Value));
	}
}

FShaderRecipeKey MakeShaderRecipeKey(const FShaderRecipe& Recipe)
{
	uint64 Hash = 0;
	Hash = HashCombine64(Hash, static_cast<uint64>(Recipe.Stage));
	Hash = HashCombine64(Hash, HashWString(Recipe.SourcePath));
	Hash = HashCombine64(Hash, HashString(Recipe.EntryPoint));
	Hash = HashCombine64(Hash, HashString(Recipe.Target));
	Hash = HashCombine64(Hash, static_cast<uint64>(Recipe.LayoutType));

	std::vector<FShaderMacroDesc> SortedMacros = Recipe.Macros;
	std::sort(
		SortedMacros.begin(),
		SortedMacros.end(),
		[](const FShaderMacroDesc& A, const FShaderMacroDesc& B)
		{
			if (A.Name != B.Name)
			{
				return A.Name < B.Name;
			}

			return A.Value < B.Value;
		});

	for (const FShaderMacroDesc& Macro : SortedMacros)
	{
		Hash = HashCombine64(Hash, HashString(Macro.Name));
		Hash = HashCombine64(Hash, HashString(Macro.Value));
	}

	return { Hash };
}

std::wstring MakeShaderRecipeDebugString(const FShaderRecipe& Recipe)
{
	std::wstring Result = Recipe.SourcePath;
	Result += L" | ";
	Result += std::wstring(Recipe.EntryPoint.begin(), Recipe.EntryPoint.end());
	Result += L" | ";
	Result += std::wstring(Recipe.Target.begin(), Recipe.Target.end());

	for (const FShaderMacroDesc& Macro : Recipe.Macros)
	{
		Result += L" | ";
		Result += std::wstring(Macro.Name.begin(), Macro.Name.end());
		Result += L"=";
		Result += std::wstring(Macro.Value.begin(), Macro.Value.end());
	}

	return Result;
}

std::vector<D3D_SHADER_MACRO> BuildD3DShaderMacros(const FShaderRecipe& Recipe)
{
	std::vector<D3D_SHADER_MACRO> Result;
	Result.reserve(Recipe.Macros.size() + 1);

	for (const FShaderMacroDesc& MacroDesc : Recipe.Macros)
	{
		D3D_SHADER_MACRO Macro = {};
		Macro.Name = MacroDesc.Name.c_str();
		Macro.Definition = MacroDesc.Value.c_str();
		Result.push_back(Macro);
	}

	Result.push_back({ nullptr, nullptr });
	return Result;
}
