#include "Renderer/Resources/Shader/ShaderResource.h"

#include <cstring>
#include <vector>

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

void FShaderResource::ClearCache()
{
	Cache.clear();
}
