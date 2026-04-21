#include "Renderer/Resources/Shader/ShaderPathUtils.h"

#include "Core/Paths.h"

#include <cwchar>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
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
			if (Error || !It->is_regular_file(Error))
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
}

std::wstring NormalizeShaderPath(const wchar_t* Path)
{
	if (!Path)
	{
		return {};
	}

	const fs::path PathObject(Path);
	std::error_code Error;
	const fs::path WeaklyCanonicalPath = fs::weakly_canonical(PathObject, Error);
	if (!Error)
	{
		return WeaklyCanonicalPath.wstring();
	}

	return PathObject.lexically_normal().wstring();
}

std::wstring ResolveShaderPath(const wchar_t* Path)
{
	if (!Path)
	{
		return {};
	}

	const fs::path RequestedPath(Path);
	if (PathExists(RequestedPath))
	{
		return NormalizeShaderPath(Path);
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

	return NormalizeShaderPath(Path);
}

bool IsShaderSourceExtension(const std::filesystem::path& Path)
{
	const std::wstring Extension = Path.extension().wstring();
	return _wcsicmp(Extension.c_str(), L".hlsl") == 0 || _wcsicmp(Extension.c_str(), L".hlsli") == 0;
}

bool IsCompiledShaderExtension(const std::filesystem::path& Path)
{
	const std::wstring Extension = Path.extension().wstring();
	return _wcsicmp(Extension.c_str(), L".cso") == 0;
}
