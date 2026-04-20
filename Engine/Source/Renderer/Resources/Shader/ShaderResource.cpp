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

	bool PathExists(const fs::path& Path)
	{
		std::error_code Error;
		return !Path.empty() && fs::exists(Path, Error);
	}

	bool TryResolveUnderShaderRoot(const fs::path& ShaderRoot, const fs::path& RequestedPath, fs::path& OutResolvedPath)
	{
		if (!PathExists(ShaderRoot))
		{
			return false;
		}

		fs::path SuffixFromShaders;
		bool bFoundShadersSegment = false;
		for (auto It = RequestedPath.begin(); It != RequestedPath.end(); ++It)
		{
			if (_wcsicmp(It->c_str(), L"Shaders") == 0)
			{
				bFoundShadersSegment = true;
				++It;
				for (; It != RequestedPath.end(); ++It)
				{
					SuffixFromShaders /= *It;
				}
				break;
			}
		}

		if (bFoundShadersSegment && !SuffixFromShaders.empty())
		{
			const fs::path Candidate = (ShaderRoot / SuffixFromShaders).lexically_normal();
			if (PathExists(Candidate))
			{
				OutResolvedPath = Candidate;
				return true;
			}
		}

		if (RequestedPath.is_relative() && !RequestedPath.empty())
		{
			const fs::path Candidate = (ShaderRoot / RequestedPath).lexically_normal();
			if (PathExists(Candidate))
			{
				OutResolvedPath = Candidate;
				return true;
			}
		}

		const fs::path Filename = RequestedPath.filename();
		if (Filename.empty())
		{
			return false;
		}

		const fs::path FlatCandidate = (ShaderRoot / Filename).lexically_normal();
		if (PathExists(FlatCandidate))
		{
			OutResolvedPath = FlatCandidate;
			return true;
		}

		std::error_code Error;
		for (fs::recursive_directory_iterator It(ShaderRoot, fs::directory_options::skip_permission_denied, Error), End;
			It != End;
			It.increment(Error))
		{
			if (Error)
			{
				continue;
			}

			if (!It->is_regular_file(Error))
			{
				continue;
			}

			if (_wcsicmp(It->path().filename().c_str(), Filename.c_str()) == 0)
			{
				OutResolvedPath = It->path().lexically_normal();
				return true;
			}
		}

		return false;
	}

	std::wstring ResolveShaderPath(const wchar_t* HlslPath)
	{
		if (!HlslPath)
		{
			return {};
		}

		const fs::path RequestedPath(HlslPath);
		if (PathExists(RequestedPath))
		{
			return NormalizeShaderPath(HlslPath);
		}

		std::vector<fs::path> ShaderRoots;
		auto AddShaderRoot = [&ShaderRoots](const fs::path& RootPath)
		{
			if (!PathExists(RootPath))
			{
				return;
			}

			const fs::path NormalizedRoot = RootPath.lexically_normal();
			for (const fs::path& ExistingRoot : ShaderRoots)
			{
				if (_wcsicmp(ExistingRoot.c_str(), NormalizedRoot.c_str()) == 0)
				{
					return;
				}
			}

			ShaderRoots.push_back(NormalizedRoot);
		};

		AddShaderRoot(FPaths::ShaderDir());
		AddShaderRoot(FPaths::ProjectRoot() / "Engine/Shaders/");

		fs::path ResolvedPath;
		for (const fs::path& ShaderRoot : ShaderRoots)
		{
			if (TryResolveUnderShaderRoot(ShaderRoot, RequestedPath.lexically_normal(), ResolvedPath))
			{
				return NormalizeShaderPath(ResolvedPath.c_str());
			}
		}

		return NormalizeShaderPath(HlslPath);
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
			MessageBoxW(nullptr, Message.c_str(), L"Shader Fatal Error", MB_OK | MB_ICONERROR);
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
	const char* Target,
	const D3D_SHADER_MACRO* Defines)
{
	// ContentDir이 비어 있으면 FPaths에서 초기화
	if (ContentDir.empty())
	{
		std::wstring Temp = FPaths::ShaderCacheDir().wstring();
		SetContentDir(Temp.c_str());
	}

	const std::wstring RequestedPath = NormalizeShaderPath(FilePath);
	const std::wstring ResolvedPath = ResolveShaderPath(FilePath);
	const wchar_t* EffectiveFilePath = ResolvedPath.c_str();

	std::wstring Key = MakeCacheKey(EffectiveFilePath, EntryPoint, Target);
	if (Defines)
	{
		for (const D3D_SHADER_MACRO* D = Defines; D->Name != nullptr; ++D)
		{
			Key += L"|";
			Key += NarrowToWide(D->Name);
			Key += L"=";
			Key += NarrowToWide(D->Definition ? D->Definition : "1");
		}
	}

	auto It = Cache.find(Key);
	if (It != Cache.end())
	{
		return It->second;
	}

	if (!EffectiveFilePath || !fs::exists(EffectiveFilePath))
	{
		std::wstringstream Stream;
		Stream << L"Shader file not found: "
			<< (FilePath ? FilePath : L"(null)")
			<< L" | ResolvedPath=" << ResolvedPath
			<< L" | EntryPoint=" << NarrowToWide(EntryPoint)
			<< L" | Target=" << NarrowToWide(Target);
		FatalShaderError(Stream.str());
	}

	std::wstring CsoPath = MakeCsoPath(EffectiveFilePath, EntryPoint, Target);
	if (Defines)
	{
		// cso 경로에 매크로 suffix 추가
		std::wstring MacroSuffix;
		for (const D3D_SHADER_MACRO* D = Defines; D->Name != nullptr; ++D)
		{
			MacroSuffix += L"_";
			MacroSuffix += NarrowToWide(D->Name);
		}

		const size_t DotPos = CsoPath.rfind(L'.');
		if (DotPos != std::wstring::npos)
		{
			CsoPath.insert(DotPos, MacroSuffix);
		}
		else
		{
			CsoPath += MacroSuffix;
		}
	}

	// .cso가 존재하고 hlsl보다 최신이면 cso에서 로드
    // DEBUG 모드시 매번 쉐이더 컴파일 시도
    #ifndef _DEBUG
	if (!IsHlslNewer(EffectiveFilePath, CsoPath.c_str()))
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
		EffectiveFilePath, Defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
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
			<< EffectiveFilePath
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
			<< L" | Source=" << EffectiveFilePath;
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
