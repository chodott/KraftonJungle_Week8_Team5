#include "Renderer/Resources/Shader/ShaderRegistry.h"

#include "Core/Paths.h"
#include "Renderer/Resources/Shader/ShaderCompiler.h"
#include "Renderer/Resources/Shader/ShaderPathUtils.h"
#include "Debug/EngineLog.h"

#include <string>

FShaderRegistry& FShaderRegistry::Get()
{
	static FShaderRegistry Instance;
	return Instance;
}

std::shared_ptr<FVertexShaderHandle> FShaderRegistry::GetOrCreateVertexShaderHandle(ID3D11Device* Device, const FShaderRecipe& Recipe)
{
	return BuildHandleInternal<FVertexShaderHandle>(Device, Recipe);
}

std::shared_ptr<FPixelShaderHandle> FShaderRegistry::GetOrCreatePixelShaderHandle(ID3D11Device* Device, const FShaderRecipe& Recipe)
{
	return BuildHandleInternal<FPixelShaderHandle>(Device, Recipe);
}

std::shared_ptr<FComputeShaderHandle> FShaderRegistry::GetOrCreateComputeShaderHandle(ID3D11Device* Device, const FShaderRecipe& Recipe)
{
	return BuildHandleInternal<FComputeShaderHandle>(Device, Recipe);
}

bool FShaderRegistry::BuildTransactionForChangedFiles(
	const std::unordered_set<std::wstring>& ChangedFiles,
	FShaderReloadTransaction&               OutTransaction)
{
	OutTransaction              = {};
	OutTransaction.ChangedFiles = ChangedFiles;

	{
		std::unique_lock<std::shared_mutex> Lock(Mutex);
		OutTransaction.TransactionId = NextTransactionId++;
	}

	std::unordered_set<FShaderRecipeKey, FShaderRecipeKeyHasher> AffectedKeys;
	GatherAffectedHandles(ChangedFiles, AffectedKeys, OutTransaction.bRequiresFullFallback);

	if (AffectedKeys.empty() && !ChangedFiles.empty())
	{
		std::shared_lock<std::shared_mutex> Lock(Mutex);
		for (const auto& Pair : Handles)
		{
			AffectedKeys.insert(Pair.first);
		}
		OutTransaction.bRequiresFullFallback = true;
		UE_LOG("[ShaderHotReload] no exact dependency match; rebuilding all registered shader handles.\n");
	}

	if (AffectedKeys.empty())
	{
		UE_LOG("[ShaderHotReload] no shader handles registered for changed files.\n");
		return false;
	}

	std::vector<std::shared_ptr<IShaderHandle>> HandlesToBuild;
	{
		std::shared_lock<std::shared_mutex> Lock(Mutex);
		for (const FShaderRecipeKey& Key : AffectedKeys)
		{
			auto Existing = Handles.find(Key);
			if (Existing != Handles.end())
			{
				HandlesToBuild.push_back(Existing->second);
			}
		}
	}

	for (const std::shared_ptr<IShaderHandle>& Handle : HandlesToBuild)
	{
		FShaderCompileArtifact Artifact = FShaderCompiler::Compile(Handle->GetRecipe(), true);
		if (!Artifact.bSuccess)
		{
			OutTransaction.ErrorText = Artifact.ErrorText;
			OutTransaction.Entries.clear();
			return false;
		}

		OutTransaction.Entries.push_back({ Handle, std::move(Artifact) });
	}

	UE_LOG((std::string("[ShaderHotReload] built reload transaction entries=") +
		std::to_string(OutTransaction.Entries.size()) + "\n").c_str());

	return !OutTransaction.Entries.empty();
}

bool FShaderRegistry::ApplyTransaction(
	ID3D11Device*                   Device,
	const FShaderReloadTransaction& Transaction,
	std::string&                    OutError)
{
	for (const FShaderReloadArtifactEntry& Entry : Transaction.Entries)
	{
		if (!Entry.Handle->RebuildFromArtifact(Device, Entry.Artifact, OutError))
		{
			return false;
		}
	}

	for (const FShaderReloadArtifactEntry& Entry : Transaction.Entries)
	{
		UpdateDependencies(Entry.Handle, Entry.Artifact);
	}

	return true;
}

void FShaderRegistry::Clear()
{
	std::unique_lock<std::shared_mutex> Lock(Mutex);
	Handles.clear();
	HandleDependencies.clear();
	ReverseDependencies.clear();
}

template <typename THandle>
std::shared_ptr<THandle> FShaderRegistry::BuildHandleInternal(ID3D11Device* Device, const FShaderRecipe& Recipe)
{
	const FShaderRecipeKey Key = MakeShaderRecipeKey(Recipe);

	{
		std::shared_lock<std::shared_mutex> ReadLock(Mutex);
		auto                                Existing = Handles.find(Key);
		if (Existing != Handles.end())
		{
			return std::static_pointer_cast<THandle>(Existing->second);
		}
	}

	FShaderCompileArtifact Artifact = FShaderCompiler::Compile(Recipe, true);
	if (!Artifact.bSuccess)
	{
		return nullptr;
	}

	std::shared_ptr<THandle> Handle = std::make_shared<THandle>(Recipe);
	std::string              Error;
	if (!Handle->RebuildFromArtifact(Device, Artifact, Error))
	{
		return nullptr;
	}

	{
		std::unique_lock<std::shared_mutex> WriteLock(Mutex);
		Handles[Key] = Handle;
	}

	UpdateDependencies(Handle, Artifact);
	return Handle;
}

void FShaderRegistry::UpdateDependencies(const std::shared_ptr<IShaderHandle>& Handle, const FShaderCompileArtifact& Artifact)
{
	const FShaderRecipeKey Key = Handle->GetKey();

	std::unique_lock<std::shared_mutex> Lock(Mutex);

	auto Existing = HandleDependencies.find(Key);
	if (Existing != HandleDependencies.end())
	{
		for (const std::wstring& OldFile : Existing->second)
		{
			auto Reverse = ReverseDependencies.find(OldFile);
			if (Reverse != ReverseDependencies.end())
			{
				Reverse->second.erase(Key);
				if (Reverse->second.empty())
				{
					ReverseDependencies.erase(Reverse);
				}
			}
		}
	}

	std::unordered_set<std::wstring> NewDependencies;
	NewDependencies.insert(Artifact.ResolvedSourcePath);
	for (const std::wstring& IncludePath : Artifact.IncludedFiles)
	{
		NewDependencies.insert(IncludePath);
	}

	HandleDependencies[Key] = NewDependencies;
	for (const std::wstring& File : NewDependencies)
	{
		ReverseDependencies[File].insert(Key);
	}
}

void FShaderRegistry::GatherAffectedHandles(
	const std::unordered_set<std::wstring>&                       ChangedFiles,
	std::unordered_set<FShaderRecipeKey, FShaderRecipeKeyHasher>& OutKeys,
	bool&                                                         bRequiresFullFallback) const
{
	bRequiresFullFallback = false;

	// TODO : Auto Add If end with .hlsli
	static const std::unordered_set<std::wstring> FullFallbackFiles =
	{
		NormalizeShaderPath((FPaths::ShaderDir() / L"FrameCommon.hlsli").c_str()),
		NormalizeShaderPath((FPaths::ShaderDir() / L"ObjectCommon.hlsli").c_str()),
		NormalizeShaderPath((FPaths::ShaderDir() / L"LightCommon.hlsli").c_str()),
		NormalizeShaderPath((FPaths::ShaderDir() / L"MeshVertexCommon.hlsli").c_str()),
		NormalizeShaderPath((FPaths::ShaderDir() / L"ShaderCommon.hlsli").c_str()),
		NormalizeShaderPath((FPaths::ShaderDir() / L"DecalCommon.hlsli").c_str()),
	};

	std::shared_lock<std::shared_mutex> Lock(Mutex);

	for (const std::wstring& ChangedFile : ChangedFiles)
	{
		if (FullFallbackFiles.contains(ChangedFile))
		{
			bRequiresFullFallback = true;
		}

		auto Reverse = ReverseDependencies.find(ChangedFile);
		if (Reverse == ReverseDependencies.end())
		{
			continue;
		}

		for (const FShaderRecipeKey& Key : Reverse->second)
		{
			OutKeys.insert(Key);
		}
	}
}
