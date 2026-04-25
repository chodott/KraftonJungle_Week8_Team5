#include "Renderer/Resources/Shader/ShaderRegistry.h"

#include "Core/Paths.h"
#include "Renderer/Resources/Shader/ShaderCompiler.h"
#include "Renderer/Resources/Shader/ShaderPathUtils.h"
#include "Debug/EngineLog.h"

#include <filesystem>
#include <string>

namespace
{
	std::string NarrowPathForLog(const std::wstring& Path)
	{
		return std::filesystem::path(Path).string();
	}

	std::string JoinPathsForLog(const std::vector<std::wstring>& Paths)
	{
		std::string Result;
		for (size_t Index = 0; Index < Paths.size(); ++Index)
		{
			if (Index > 0)
			{
				Result += ", ";
			}

			Result += NarrowPathForLog(Paths[Index]);
		}

		return Result;
	}
}

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
	std::vector<std::wstring>                                    UnresolvedIncludeFiles;
	GatherAffectedHandles(ChangedFiles, AffectedKeys, OutTransaction.bRequiresFullFallback, UnresolvedIncludeFiles);

	if (!UnresolvedIncludeFiles.empty())
	{
		GatherAllRegisteredHandleKeys(AffectedKeys);
		OutTransaction.bRequiresFullFallback = true;
		UE_LOG((std::string("[ShaderHotReload] unresolved include dependency fallback for: ")
			+ JoinPathsForLog(UnresolvedIncludeFiles) + "\n").c_str());
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
	std::vector<std::function<void()>> CommitSteps;
	CommitSteps.reserve(Transaction.Entries.size());

	for (const FShaderReloadArtifactEntry& Entry : Transaction.Entries)
	{
		std::function<void()> CommitStep;
		if (!Entry.Handle->PrepareRebuildFromArtifact(Device, Entry.Artifact, CommitStep, OutError))
		{
			return false;
		}

		CommitSteps.push_back(std::move(CommitStep));
	}

	for (std::function<void()>& CommitStep : CommitSteps)
	{
		CommitStep();
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
		UE_LOG(
			(std::string("[ShaderRegistry] Shader compile failed: ")
				+ NarrowPathForLog(Recipe.SourcePath)
				+ "\n"
				+ Artifact.ErrorText
				+ "\n").c_str());

		return nullptr;
	}

	std::shared_ptr<THandle> Handle = std::make_shared<THandle>(Recipe);
	std::function<void()>    CommitStep;
	std::string              Error;
	if (!Handle->PrepareRebuildFromArtifact(Device, Artifact, CommitStep, Error))
	{
		UE_LOG(
			(std::string("[ShaderRegistry] Shader object creation failed: ")
				+ NarrowPathForLog(Recipe.SourcePath)
				+ "\n"
				+ Error
				+ "\n").c_str());

		return nullptr;
	}

	{
		std::unique_lock<std::shared_mutex> WriteLock(Mutex);
		auto                                Existing = Handles.find(Key);
		if (Existing != Handles.end())
		{
			return std::static_pointer_cast<THandle>(Existing->second);
		}

		CommitStep();
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

void FShaderRegistry::GatherAllRegisteredHandleKeys(std::unordered_set<FShaderRecipeKey, FShaderRecipeKeyHasher>& OutKeys) const
{
	std::shared_lock<std::shared_mutex> Lock(Mutex);
	for (const auto& Pair : Handles)
	{
		OutKeys.insert(Pair.first);
	}
}

void FShaderRegistry::GatherAffectedHandles(
	const std::unordered_set<std::wstring>&                       ChangedFiles,
	std::unordered_set<FShaderRecipeKey, FShaderRecipeKeyHasher>& OutKeys,
	bool&                                                         bRequiresFullFallback,
	std::vector<std::wstring>&                                    OutUnresolvedIncludeFiles) const
{
	bRequiresFullFallback = false;
	OutUnresolvedIncludeFiles.clear();

	std::shared_lock<std::shared_mutex> Lock(Mutex);

	for (const std::wstring& ChangedFile : ChangedFiles)
	{
		auto Reverse = ReverseDependencies.find(ChangedFile);
		if (Reverse == ReverseDependencies.end())
		{
			if (std::filesystem::path(ChangedFile).extension() == L".hlsli")
			{
				bRequiresFullFallback = true;
				OutUnresolvedIncludeFiles.push_back(ChangedFile);
			}
			continue;
		}

		for (const FShaderRecipeKey& Key : Reverse->second)
		{
			OutKeys.insert(Key);
		}
	}
}
