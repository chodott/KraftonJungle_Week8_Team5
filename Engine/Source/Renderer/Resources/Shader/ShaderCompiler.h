#pragma once

#include "Renderer/Resources/Shader/ShaderRecipe.h"

#include <cstdint>
#include <string>
#include <vector>

struct FShaderCompileArtifact
{
	bool bSuccess = false;
	std::wstring ResolvedSourcePath;
	std::vector<uint8> Bytecode;
	std::vector<std::wstring> IncludedFiles;
	std::string ErrorText;
	uint64 Fingerprint = 0;
};

class ENGINE_API FShaderCompiler
{
public:
	static FShaderCompileArtifact Compile(const FShaderRecipe& Recipe, bool bForceRecompile = true);

private:
	static uint64 BuildFingerprint(
		const FShaderRecipe& Recipe,
		const std::wstring& ResolvedSourcePath,
		const std::vector<std::wstring>& IncludedFiles);
};
