#include "Renderer/Resources/Shader/ShaderResource.h"
#include "Core/Paths.h"
#include <d3dcompiler.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cwchar>
#include <cassert>
#include <cstdlib>
#include <sstream>
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

	std::wstring NormalizeShaderPath(const wchar_t* HlslPath)
	{
		if (!HlslPath)
		{
			return {};
		}

		fs::path Path(HlslPath);
		std::error_code Error;
		const fs::path WeaklyCanonicalPath = fs::weakly_canonical(Path, Error);
		if (!Error)
		{
			return WeaklyCanonicalPath.wstring();
		}

		return Path.lexically_normal().wstring();
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

	[[noreturn]] void FatalShaderError(const std::wstring& Message)
	{
		OutputDebugStringW((L"[Shader Fatal] " + Message + L"\n").c_str());
		assert(false && "Fatal shader error");
		std::abort();
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

void FShaderResource::SetContentDir(const wchar_t* Dir)
{
	ContentDir = Dir;
	if (!ContentDir.empty() && ContentDir.back() != L'/' && ContentDir.back() != L'\\')
	{
		ContentDir += L'/';
	}
}

std::wstring FShaderResource::MakeCacheKey(const wchar_t* HlslPath, const char* EntryPoint, const char* Target)
{
	return NormalizeShaderPath(HlslPath)
		+ L"|"
		+ NarrowToWide(EntryPoint)
		+ L"|"
		+ NarrowToWide(Target);
}

std::wstring FShaderResource::MakeCsoPath(const wchar_t* HlslPath, const char* EntryPoint, const char* Target)
{
	const fs::path HlslFile(HlslPath ? HlslPath : L"");
	const std::wstring Stem = HlslFile.stem().wstring();
	const std::wstring Entry = SanitizeFilenameToken(NarrowToWide(EntryPoint));
	const std::wstring Profile = SanitizeFilenameToken(NarrowToWide(Target));
	const std::wstring NormalizedPath = NormalizeShaderPath(HlslPath);
	const uint64 PathHash = static_cast<uint64>(std::hash<std::wstring>{}(NormalizedPath));

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
		+ L".cso";
}

bool FShaderResource::IsHlslNewer(const wchar_t* HlslPath, const wchar_t* CsoPath)
{
	std::error_code Ec;

	if (!fs::exists(CsoPath, Ec))
	{
		return true;
	}

	if (!fs::exists(HlslPath, Ec))
	{
		return false;
	}

	auto HlslTime = fs::last_write_time(HlslPath, Ec);
	if (Ec) return true;

	auto CsoTime = fs::last_write_time(CsoPath, Ec);
	if (Ec) return true;

	return HlslTime > CsoTime;
}

bool FShaderResource::SaveCso(const wchar_t* CsoPath, const void* Data, SIZE_T Size)
{
	fs::path Dir = fs::path(CsoPath).parent_path();
	std::error_code Ec;
	fs::create_directories(Dir, Ec);

	std::ofstream File(CsoPath, std::ios::binary);
	if (!File.is_open())
	{
		return false;
	}

	File.write(static_cast<const char*>(Data), Size);
	return File.good();
}

std::shared_ptr<FShaderResource> FShaderResource::LoadCso(const wchar_t* CsoPath)
{
	std::ifstream File(CsoPath, std::ios::binary | std::ios::ate);
	if (!File.is_open())
	{
		return nullptr;
	}

	std::streamsize Size = File.tellg();
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
	const char* Target)
{
	// ContentDir이 비어 있으면 FPaths에서 초기화
	if (ContentDir.empty())
	{
		std::wstring Temp = FPaths::ShaderCacheDir().wstring();
		SetContentDir(Temp.c_str());
	}

	const std::wstring Key = MakeCacheKey(FilePath, EntryPoint, Target);

	auto It = Cache.find(Key);
	if (It != Cache.end())
	{
		return It->second;
	}

	if (!FilePath || !fs::exists(FilePath))
	{
		std::wstringstream Stream;
		Stream << L"Shader file not found: "
			<< (FilePath ? FilePath : L"(null)")
			<< L" | EntryPoint=" << NarrowToWide(EntryPoint)
			<< L" | Target=" << NarrowToWide(Target);
		FatalShaderError(Stream.str());
	}

	const std::wstring CsoPath = MakeCsoPath(FilePath, EntryPoint, Target);

	// .cso가 존재하고 hlsl보다 최신이면 cso에서 로드
    // DEBUG 모드시 매번 쉐이더 컴파일 시도
    #ifndef _DEBUG
	if (!IsHlslNewer(FilePath, CsoPath.c_str()))
	{
		auto Resource = LoadCso(CsoPath.c_str());
		if (Resource)
		{
			Cache[Key] = Resource;
			return Resource;
		}
	}
    #endif

	// hlsl에서 컴파일
	ID3DBlob* Blob = nullptr;
	ID3DBlob* ErrorBlob = nullptr;

	HRESULT Hr = D3DCompileFromFile(
		FilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		EntryPoint, Target,
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
		0, &Blob, &ErrorBlob
	);

	if (FAILED(Hr))
	{
		std::wstring ErrorMessage = L"Unknown shader compile error";
		if (ErrorBlob)
		{
			const char* ErrorText = static_cast<const char*>(ErrorBlob->GetBufferPointer());
			OutputDebugStringA(ErrorText);
			ErrorMessage = NarrowToWide(ErrorText);
			ErrorBlob->Release();
			ErrorBlob = nullptr;
		}

		std::wstringstream Stream;
		Stream << L"Shader compile failed: "
			<< FilePath
			<< L" | EntryPoint=" << NarrowToWide(EntryPoint)
			<< L" | Target=" << NarrowToWide(Target)
			<< L"\n"
			<< ErrorMessage;
		FatalShaderError(Stream.str());
	}

	if (ErrorBlob)
	{
		ErrorBlob->Release();
	}

	// 컴파일 결과를 .cso로 저장
	if (!SaveCso(CsoPath.c_str(), Blob->GetBufferPointer(), Blob->GetBufferSize()))
	{
		std::wstringstream Stream;
		Stream << L"Failed to save shader cso: "
			<< CsoPath
			<< L" | Source=" << FilePath;
		FatalShaderError(Stream.str());
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
