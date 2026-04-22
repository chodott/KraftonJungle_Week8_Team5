#include "Renderer/Resources/Shader/ShaderCompiler.h"

#include "Core/Paths.h"
#include "Renderer/Resources/Shader/ShaderPathUtils.h"

#include <d3dcompiler.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <Windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace fs = std::filesystem;

namespace
{
	class FTrackingInclude final : public ID3DInclude
	{
	public:
		explicit FTrackingInclude(const std::wstring& InRootSource)
			: RootSourcePath(InRootSource)
		{
		}

		STDMETHOD(Open)(
			D3D_INCLUDE_TYPE IncludeType,
			LPCSTR pFileName,
			LPCVOID pParentData,
			LPCVOID* ppData,
			UINT* pBytes) override
		{
			(void)pParentData;

			if (!pFileName || !ppData || !pBytes)
			{
				return E_INVALIDARG;
			}

			fs::path ParentDir;
			if (IncludeType == D3D_INCLUDE_LOCAL)
			{
				if (!IncludeStack.empty())
				{
					ParentDir = fs::path(IncludeStack.back()).parent_path();
				}
				else
				{
					ParentDir = fs::path(RootSourcePath).parent_path();
				}
			}
			else
			{
				ParentDir = fs::path(FPaths::ShaderDir());
			}

			const fs::path Candidate = (ParentDir / pFileName).lexically_normal();
			const std::wstring Resolved = ResolveShaderPath(Candidate.wstring().c_str());
			if (Resolved.empty() || !fs::exists(Resolved))
			{
				return E_FAIL;
			}

			std::ifstream File(fs::path(Resolved), std::ios::binary | std::ios::ate);
			if (!File.is_open())
			{
				return E_FAIL;
			}

			const std::streamsize Size = File.tellg();
			if (Size <= 0)
			{
				return E_FAIL;
			}

			File.seekg(0, std::ios::beg);

			char* Buffer = new char[static_cast<size_t>(Size)];
			File.read(Buffer, Size);
			if (!File.good())
			{
				delete[] Buffer;
				return E_FAIL;
			}

			*ppData = Buffer;
			*pBytes = static_cast<UINT>(Size);

			IncludeStack.push_back(Resolved);
			IncludedFiles.push_back(Resolved);
			return S_OK;
		}

		STDMETHOD(Close)(LPCVOID pData) override
		{
			delete[] reinterpret_cast<const char*>(pData);

			if (!IncludeStack.empty())
			{
				IncludeStack.pop_back();
			}

			return S_OK;
		}

		const std::vector<std::wstring>& GetIncludedFiles() const
		{
			return IncludedFiles;
		}

	private:
		std::wstring RootSourcePath;
		std::vector<std::wstring> IncludeStack;
		std::vector<std::wstring> IncludedFiles;
	};

	uint64 HashCombine64(uint64 A, uint64 B)
	{
		return A ^ (B + 0x9e3779b97f4a7c15ull + (A << 6) + (A >> 2));
	}

	std::vector<uint8> ReadAllBytes(const std::wstring& Path)
	{
		std::vector<uint8> Result;
		std::ifstream File(fs::path(Path), std::ios::binary | std::ios::ate);
		if (!File.is_open())
		{
			return Result;
		}

		const std::streamsize Size = File.tellg();
		if (Size <= 0)
		{
			return Result;
		}

		File.seekg(0, std::ios::beg);
		Result.resize(static_cast<size_t>(Size));
		File.read(reinterpret_cast<char*>(Result.data()), Size);
		if (!File.good())
		{
			Result.clear();
		}

		return Result;
	}

	uint64 HashBytes(const std::vector<uint8>& Bytes)
	{
		uint64 Hash = 0;
		for (uint8 Byte : Bytes)
		{
			Hash = HashCombine64(Hash, static_cast<uint64>(Byte));
		}

		return Hash;
	}
}

FShaderCompileArtifact FShaderCompiler::Compile(const FShaderRecipe& Recipe, bool bForceRecompile)
{
	(void)bForceRecompile;

	FShaderCompileArtifact Out = {};
	Out.ResolvedSourcePath = ResolveShaderPath(Recipe.SourcePath.c_str());
	if (Out.ResolvedSourcePath.empty() || !fs::exists(Out.ResolvedSourcePath))
	{
		Out.ErrorText = "Shader source not found.";
		return Out;
	}

	FTrackingInclude IncludeHandler(Out.ResolvedSourcePath);
	std::vector<D3D_SHADER_MACRO> Macros = BuildD3DShaderMacros(Recipe);

	ID3DBlob* Blob = nullptr;
	ID3DBlob* ErrorBlob = nullptr;

	UINT Flags = 0;
#if defined(_DEBUG)
	Flags |= D3DCOMPILE_DEBUG;
	Flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	Flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	const HRESULT Result = D3DCompileFromFile(
		Out.ResolvedSourcePath.c_str(),
		Macros.empty() ? nullptr : Macros.data(),
		&IncludeHandler,
		Recipe.EntryPoint.c_str(),
		Recipe.Target.c_str(),
		Flags,
		0,
		&Blob,
		&ErrorBlob);

	if (FAILED(Result))
	{
		if (ErrorBlob)
		{
			const char* ErrorText = static_cast<const char*>(ErrorBlob->GetBufferPointer());
			Out.ErrorText = ErrorText ? ErrorText : "Unknown shader compile error";
			ErrorBlob->Release();
		}

		if (Blob)
		{
			Blob->Release();
		}

		return Out;
	}

	if (ErrorBlob)
	{
		ErrorBlob->Release();
	}

	Out.IncludedFiles = IncludeHandler.GetIncludedFiles();
	Out.Bytecode.resize(static_cast<size_t>(Blob->GetBufferSize()));
	memcpy(Out.Bytecode.data(), Blob->GetBufferPointer(), Blob->GetBufferSize());
	Blob->Release();

	Out.Fingerprint = BuildFingerprint(Recipe, Out.ResolvedSourcePath, Out.IncludedFiles);
	Out.bSuccess = true;
	return Out;
}

uint64 FShaderCompiler::BuildFingerprint(
	const FShaderRecipe& Recipe,
	const std::wstring& ResolvedSourcePath,
	const std::vector<std::wstring>& IncludedFiles)
{
	uint64 Hash = 0;
	Hash = HashCombine64(Hash, static_cast<uint64>(std::hash<std::wstring> {}(ResolvedSourcePath)));
	Hash = HashCombine64(Hash, static_cast<uint64>(std::hash<std::string> {}(Recipe.EntryPoint)));
	Hash = HashCombine64(Hash, static_cast<uint64>(std::hash<std::string> {}(Recipe.Target)));
	Hash = HashCombine64(Hash, static_cast<uint64>(Recipe.LayoutType));
	Hash = HashCombine64(Hash, HashBytes(ReadAllBytes(ResolvedSourcePath)));

	for (const FShaderMacroDesc& Macro : Recipe.Macros)
	{
		Hash = HashCombine64(Hash, static_cast<uint64>(std::hash<std::string> {}(Macro.Name)));
		Hash = HashCombine64(Hash, static_cast<uint64>(std::hash<std::string> {}(Macro.Value)));
	}

	for (const std::wstring& IncludePath : IncludedFiles)
	{
		Hash = HashCombine64(Hash, static_cast<uint64>(std::hash<std::wstring> {}(IncludePath)));
		Hash = HashCombine64(Hash, HashBytes(ReadAllBytes(IncludePath)));
	}

	return Hash;
}
