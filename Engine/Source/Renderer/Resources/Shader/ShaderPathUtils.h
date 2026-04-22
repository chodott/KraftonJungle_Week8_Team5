#pragma once

#include "EngineAPI.h"

#include <filesystem>
#include <string>

ENGINE_API std::wstring NormalizeShaderPath(const wchar_t* Path);
ENGINE_API std::wstring ResolveShaderPath(const wchar_t* Path);
ENGINE_API bool IsShaderSourceExtension(const std::filesystem::path& Path);
ENGINE_API bool IsCompiledShaderExtension(const std::filesystem::path& Path);
