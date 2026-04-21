#pragma once

#include "Renderer/Resources/Shader/ShaderHandles.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

struct FShaderReloadArtifactEntry
{
	std::shared_ptr<IShaderHandle> Handle;
	FShaderCompileArtifact Artifact;
};

struct FShaderReloadTransaction
{
	uint64 TransactionId = 0;
	bool bRequiresFullFallback = false;
	std::vector<FShaderReloadArtifactEntry> Entries;
	std::unordered_set<std::wstring> ChangedFiles;
	std::string ErrorText;
};

class ENGINE_API FShaderRegistry
{
public:
	static FShaderRegistry& Get();

	std::shared_ptr<FVertexShaderHandle> GetOrCreateVertexShaderHandle(ID3D11Device* Device, const FShaderRecipe& Recipe);
	std::shared_ptr<FPixelShaderHandle> GetOrCreatePixelShaderHandle(ID3D11Device* Device, const FShaderRecipe& Recipe);
	std::shared_ptr<FComputeShaderHandle> GetOrCreateComputeShaderHandle(ID3D11Device* Device, const FShaderRecipe& Recipe);

	bool BuildTransactionForChangedFiles(
		const std::unordered_set<std::wstring>& ChangedFiles,
		FShaderReloadTransaction& OutTransaction);

	bool ApplyTransaction(
		ID3D11Device* Device,
		const FShaderReloadTransaction& Transaction,
		std::string& OutError);

	void Clear();

private:
	FShaderRegistry() = default;

	template <typename THandle>
	std::shared_ptr<THandle> BuildHandleInternal(ID3D11Device* Device, const FShaderRecipe& Recipe);

	void UpdateDependencies(const std::shared_ptr<IShaderHandle>& Handle, const FShaderCompileArtifact& Artifact);
	void GatherAllRegisteredHandleKeys(std::unordered_set<FShaderRecipeKey, FShaderRecipeKeyHasher>& OutKeys) const;
	void GatherAffectedHandles(
		const std::unordered_set<std::wstring>& ChangedFiles,
		std::unordered_set<FShaderRecipeKey, FShaderRecipeKeyHasher>& OutKeys,
		bool& bRequiresFullFallback,
		std::vector<std::wstring>& OutUnresolvedIncludeFiles) const;

private:
	mutable std::shared_mutex Mutex;
	std::unordered_map<FShaderRecipeKey, std::shared_ptr<IShaderHandle>, FShaderRecipeKeyHasher> Handles;
	std::unordered_map<FShaderRecipeKey, std::unordered_set<std::wstring>, FShaderRecipeKeyHasher> HandleDependencies;
	std::unordered_map<std::wstring, std::unordered_set<FShaderRecipeKey, FShaderRecipeKeyHasher>> ReverseDependencies;
	uint64 NextTransactionId = 1;
};
