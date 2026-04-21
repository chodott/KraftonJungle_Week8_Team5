#include "Renderer/HotReload/ShaderHotReloadService.h"

#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/ShaderPathUtils.h"
#include "Debug/EngineLog.h"

#include <filesystem>
#include <string>

bool FShaderHotReloadService::Initialize(const std::wstring& InShaderRoot)
{
	Shutdown();

	ShaderRoot = InShaderRoot;
	if (!Watcher.Start(ShaderRoot))
	{
		return false;
	}

	bInitialized = true;
	return true;
}

void FShaderHotReloadService::Shutdown()
{
	if (CompileWorker.joinable())
	{
		CompileWorker.join();
	}

	Watcher.Stop();

	std::lock_guard<std::mutex> Lock(StateMutex);
	PendingChangedFiles.clear();
	ReadyTransaction.reset();
	bCompileRunning = false;
	bInitialized    = false;
}

void FShaderHotReloadService::Tick(FRenderer& Renderer, float DeltaTime)
{
	if (!bInitialized || !bEnabled)
	{
		return;
	}

	std::unordered_set<std::wstring> WatcherChanges;
	bool                             bOverflowed = false;
	if (Watcher.ConsumeChanges(WatcherChanges, bOverflowed))
	{
		std::lock_guard<std::mutex> Lock(StateMutex);
		for (const std::wstring& File : WatcherChanges)
		{
			PendingChangedFiles.insert(File);
		}

		DebounceTimer = DebounceSeconds;
	}

	if (bOverflowed)
	{
		std::unordered_set<std::wstring> RescannedFiles;
		FullRescanShaderDirectory(RescannedFiles);

		std::lock_guard<std::mutex> Lock(StateMutex);
		PendingChangedFiles = std::move(RescannedFiles);
		DebounceTimer       = DebounceSeconds;
	}

	{
		std::lock_guard<std::mutex> Lock(StateMutex);
		if (DebounceTimer > 0.0f)
		{
			DebounceTimer -= DeltaTime;
		}
	}

	StartCompileIfNeeded();

	std::optional<FShaderReloadTransaction> TransactionToApply;
	{
		std::lock_guard<std::mutex> Lock(StateMutex);
		if (ReadyTransaction.has_value())
		{
			TransactionToApply = std::move(ReadyTransaction);
			ReadyTransaction.reset();
		}
	}

	if (TransactionToApply.has_value())
	{
		std::string Error;
		if (!Renderer.ApplyShaderReload(TransactionToApply.value(), Error))
		{
			UE_LOG((std::string("[ShaderHotReload] apply failed: ") + Error + "\n").c_str());
		}
		else
		{
			UE_LOG((std::string("[ShaderHotReload] apply succeeded. entries=") +
				std::to_string(TransactionToApply->Entries.size()) + "\n").c_str());
		}
	}
}

void FShaderHotReloadService::MarkAllDirty()
{
	std::unordered_set<std::wstring> AllFiles;
	FullRescanShaderDirectory(AllFiles);

	std::lock_guard<std::mutex> Lock(StateMutex);
	PendingChangedFiles = std::move(AllFiles);
	DebounceTimer       = DebounceSeconds;
}

void FShaderHotReloadService::StartCompileIfNeeded()
{
	std::unordered_set<std::wstring> FilesToCompile;

	{
		std::lock_guard<std::mutex> Lock(StateMutex);
		if (bCompileRunning || DebounceTimer > 0.0f || PendingChangedFiles.empty())
		{
			return;
		}

		FilesToCompile.swap(PendingChangedFiles);
		bCompileRunning = true;
	}

	if (CompileWorker.joinable())
	{
		CompileWorker.join();
	}

	CompileWorker = std::thread(&FShaderHotReloadService::CompileWorkerMain, this, std::move(FilesToCompile));
}

void FShaderHotReloadService::CompileWorkerMain(std::unordered_set<std::wstring> ChangedFiles)
{
	FShaderReloadTransaction Transaction;
	const bool               bBuilt = FShaderRegistry::Get().BuildTransactionForChangedFiles(ChangedFiles, Transaction);

	std::lock_guard<std::mutex> Lock(StateMutex);
	if (bBuilt)
	{
		UE_LOG((std::string("[ShaderHotReload] compile worker built transaction for files=") +
			std::to_string(ChangedFiles.size()) + "\n").c_str());
		ReadyTransaction = std::move(Transaction);
	}
	else if (!Transaction.ErrorText.empty())
	{
		UE_LOG((std::string("[ShaderHotReload] compile failed: ") + Transaction.ErrorText + "\n").c_str());
	}
	else
	{
		UE_LOG((std::string("[ShaderHotReload] compile worker produced no transaction for files=") +
			std::to_string(ChangedFiles.size()) + "\n").c_str());
	}

	bCompileRunning = false;
}

void FShaderHotReloadService::FullRescanShaderDirectory(std::unordered_set<std::wstring>& OutFiles) const
{
	namespace fs = std::filesystem;

	OutFiles.clear();
	std::error_code Error;
	for (fs::recursive_directory_iterator It(ShaderRoot, fs::directory_options::skip_permission_denied, Error), End;
	     It != End;
	     It.increment(Error))
	{
		if (Error || !It->is_regular_file(Error))
		{
			continue;
		}

		const fs::path& Path = It->path();
		if (!IsShaderSourceExtension(Path) || IsCompiledShaderExtension(Path))
		{
			continue;
		}

		OutFiles.insert(NormalizeShaderPath(Path.c_str()));
	}
}
