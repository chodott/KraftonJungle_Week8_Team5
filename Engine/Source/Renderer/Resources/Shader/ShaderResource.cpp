#include "Renderer/Resources/Shader/ShaderResource.h"

#include "Core/Paths.h"
#include "Renderer/Resources/Shader/ShaderPathUtils.h"

#include <d3dcompiler.h>

#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>
#include <Windows.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace fs = std::filesystem;

namespace
{
	std::wstring NarrowToWide(const char* Text)
	{
		if (!Text)
		{
			return {};
		}

		return std::wstring(Text, Text + std::strlen(Text));
	}

	std::wstring SanitizeFilenameToken(std::wstring Token)
	{
		for (wchar_t& Character : Token)
		{
			const bool bAlphaNumeric = (Character >= L'0' && Character <= L'9')
				|| (Character >= L'A' && Character <= L'Z')
				|| (Character >= L'a' && Character <= L'z');
			if (!bAlphaNumeric)
			{
				Character = L'_';
			}
		}

		return Token;
	}

	std::wstring BuildMacroSuffix(const D3D_SHADER_MACRO* Defines)
	{
		std::wstring Suffix;
		if (!Defines)
		{
			return Suffix;
		}

		for (const D3D_SHADER_MACRO* Define = Defines; Define->Name != nullptr; ++Define)
		{
			Suffix += L"_";
			Suffix += SanitizeFilenameToken(NarrowToWide(Define->Name));
			Suffix += L"_";
			Suffix += SanitizeFilenameToken(NarrowToWide(Define->Definition ? Define->Definition : "1"));
		}

		return Suffix;
	}
}

std::unordered_map<std::wstring, std::shared_ptr<FShaderResource>> FShaderResource::Cache;
std::wstring FShaderResource::ContentDir;

FShaderResource::~FShaderResource()
{
	if (ShaderBlob)
	{
		ShaderBlob->Release();
		ShaderBlob = nullptr;
	}
}

const void* FShaderResource::GetBufferPointer() const
{
	if (bFromCso)
	{
		return RawData.data();
	}

	return ShaderBlob ? ShaderBlob->GetBufferPointer() : nullptr;
}

SIZE_T FShaderResource::GetBufferSize() const
{
	if (bFromCso)
	{
		return RawData.size();
	}

	return ShaderBlob ? ShaderBlob->GetBufferSize() : 0;
}

std::shared_ptr<FShaderResource> FShaderResource::CreateFromBytecode(const void* Data, SIZE_T Size)
{
	if (!Data || Size == 0)
	{
		return nullptr;
	}

	std::shared_ptr<FShaderResource> Resource(new FShaderResource());
	Resource->RawData.resize(static_cast<size_t>(Size));
	memcpy(Resource->RawData.data(), Data, Size);
	Resource->bFromCso = true;
	return Resource;
}

void FShaderResource::SetContentDir(const wchar_t* Dir)
{
	ContentDir = Dir ? Dir : L"";
	if (!ContentDir.empty() && ContentDir.back() != L'/' && ContentDir.back() != L'\\')
	{
		ContentDir += L'/';
	}
}

std::wstring FShaderResource::MakeCacheKey(
	const wchar_t* HlslPath,
	const char* EntryPoint,
	const char* Target,
	const D3D_SHADER_MACRO* Defines)
{
	std::wstring Key = NormalizeShaderPath(HlslPath)
		+ L"|"
		+ NarrowToWide(EntryPoint)
		+ L"|"
		+ NarrowToWide(Target);

	if (Defines)
	{
		for (const D3D_SHADER_MACRO* Define = Defines; Define->Name != nullptr; ++Define)
		{
			Key += L"|";
			Key += NarrowToWide(Define->Name);
			Key += L"=";
			Key += NarrowToWide(Define->Definition ? Define->Definition : "1");
		}
	}

	return Key;
}

std::wstring FShaderResource::MakeCsoPath(
	const wchar_t* HlslPath,
	const char* EntryPoint,
	const char* Target,
	const D3D_SHADER_MACRO* Defines)
{
	const fs::path HlslFile(HlslPath ? HlslPath : L"");
	const std::wstring Stem = HlslFile.stem().wstring();
	const std::wstring Entry = SanitizeFilenameToken(NarrowToWide(EntryPoint));
	const std::wstring Profile = SanitizeFilenameToken(NarrowToWide(Target));
	const std::wstring NormalizedPath = NormalizeShaderPath(HlslPath);
	const uint64 PathHash = static_cast<uint64>(std::hash<std::wstring> {}(NormalizedPath));

	wchar_t HashBuffer[17] = {};
	swprintf_s(HashBuffer, L"%016llx", static_cast<unsigned long long>(PathHash));

	return ContentDir
		+ Stem
		+ L"_"
		+ Entry
		+ L"_"
		+ Profile
		+ L"_"
		+ HashBuffer
		+ BuildMacroSuffix(Defines)
		+ L".cso";
}

bool FShaderResource::IsHlslNewer(const wchar_t* HlslPath, const wchar_t* CsoPath)
{
	std::error_code Error;

	if (!fs::exists(CsoPath, Error))
	{
		return true;
	}

	if (!fs::exists(HlslPath, Error))
	{
		return false;
	}

	const auto HlslTime = fs::last_write_time(HlslPath, Error);
	if (Error)
	{
		return true;
	}

	const auto CsoTime = fs::last_write_time(CsoPath, Error);
	if (Error)
	{
		return true;
	}

	return HlslTime > CsoTime;
}

bool FShaderResource::SaveCso(const wchar_t* CsoPath, const void* Data, SIZE_T Size)
{
	const fs::path Directory = fs::path(CsoPath).parent_path();
	std::error_code Error;
	fs::create_directories(Directory, Error);

	std::ofstream File(fs::path(CsoPath), std::ios::binary);
	if (!File.is_open())
	{
		return false;
	}

	File.write(static_cast<const char*>(Data), Size);
	return File.good();
}

std::shared_ptr<FShaderResource> FShaderResource::LoadCso(const wchar_t* CsoPath)
{
	std::ifstream File(fs::path(CsoPath), std::ios::binary | std::ios::ate);
	if (!File.is_open())
	{
		return nullptr;
	}

	const std::streamsize Size = File.tellg();
	if (Size <= 0)
	{
		return nullptr;
	}

	File.seekg(0, std::ios::beg);

	std::shared_ptr<FShaderResource> Resource(new FShaderResource());
	Resource->RawData.resize(static_cast<size_t>(Size));
	File.read(reinterpret_cast<char*>(Resource->RawData.data()), Size);

	if (!File.good())
	{
		return nullptr;
	}

	Resource->bFromCso = true;
	return Resource;
}

std::shared_ptr<FShaderResource> FShaderResource::GetOrCompile(
	const wchar_t* FilePath,
	const char* EntryPoint,
	const char* Target,
	const D3D_SHADER_MACRO* Defines)
{
	if (ContentDir.empty())
	{
		const std::wstring Temp = FPaths::ShaderCacheDir().wstring();
		SetContentDir(Temp.c_str());
	}

	const std::wstring ResolvedPath = ResolveShaderPath(FilePath);
	const wchar_t* EffectiveFilePath = ResolvedPath.c_str();
	const std::wstring Key = MakeCacheKey(EffectiveFilePath, EntryPoint, Target, Defines);

	auto Existing = Cache.find(Key);
	if (Existing != Cache.end())
	{
		return Existing->second;
	}

	if (!EffectiveFilePath || !fs::exists(EffectiveFilePath))
	{
		std::wstringstream Stream;
		Stream << L"Shader file not found: "
			<< (FilePath ? FilePath : L"(null)")
			<< L" | ResolvedPath=" << ResolvedPath
			<< L" | EntryPoint=" << NarrowToWide(EntryPoint)
			<< L" | Target=" << NarrowToWide(Target);
		OutputDebugStringW((L"[Shader] " + Stream.str() + L"\n").c_str());
		return nullptr;
	}

	const std::wstring CsoPath = MakeCsoPath(EffectiveFilePath, EntryPoint, Target, Defines);

#ifndef _DEBUG
	if (!IsHlslNewer(EffectiveFilePath, CsoPath.c_str()))
	{
		std::shared_ptr<FShaderResource> CachedResource = LoadCso(CsoPath.c_str());
		if (CachedResource)
		{
			Cache[Key] = CachedResource;
			return CachedResource;
		}
	}
#endif

	ID3DBlob* Blob = nullptr;
	ID3DBlob* ErrorBlob = nullptr;

	const HRESULT Result = D3DCompileFromFile(
		EffectiveFilePath,
		Defines,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		EntryPoint,
		Target,
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0,
		&Blob,
		&ErrorBlob);

	if (FAILED(Result))
	{
		std::wstring ErrorMessage = L"Unknown shader compile error";
		if (ErrorBlob)
		{
			const char* ErrorText = static_cast<const char*>(ErrorBlob->GetBufferPointer());
			if (ErrorText)
			{
				OutputDebugStringA(ErrorText);
				ErrorMessage = NarrowToWide(ErrorText);
			}

			ErrorBlob->Release();
			ErrorBlob = nullptr;
		}

		std::wstringstream Stream;
		Stream << L"Shader compile failed: "
			<< EffectiveFilePath
			<< L" | EntryPoint=" << NarrowToWide(EntryPoint)
			<< L" | Target=" << NarrowToWide(Target)
			<< L"\n"
			<< ErrorMessage;
		OutputDebugStringW((L"[Shader] " + Stream.str() + L"\n").c_str());
		return nullptr;
	}

	if (ErrorBlob)
	{
		ErrorBlob->Release();
	}

	if (!SaveCso(CsoPath.c_str(), Blob->GetBufferPointer(), Blob->GetBufferSize()))
	{
		std::wstringstream Stream;
		Stream << L"Failed to save shader cso: " << CsoPath << L" | Source=" << EffectiveFilePath;
		OutputDebugStringW((L"[Shader] " + Stream.str() + L"\n").c_str());
	}

	std::shared_ptr<FShaderResource> Resource(new FShaderResource());
	Resource->ShaderBlob = Blob;

	Cache[Key] = Resource;
	return Resource;
}

void FShaderResource::ClearCache()
{
	Cache.clear();
}
