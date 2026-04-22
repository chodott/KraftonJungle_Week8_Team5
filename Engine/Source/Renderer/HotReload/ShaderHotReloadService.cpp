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
			PendingFileObservations.erase(File);
		}

		DebounceTimer = DebounceSeconds;
	}

	if (bOverflowed)
	{
		std::unordered_set<std::wstring> RescannedFiles;
		FullRescanShaderDirectory(RescannedFiles);

		std::lock_guard<std::mutex> Lock(StateMutex);
		PendingChangedFiles = std::move(RescannedFiles);
		ResetObservedState(PendingChangedFiles);
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
			std::lock_guard<std::mutex> Lock(StateMutex);
			RequeueChangedFiles(TransactionToApply->ChangedFiles);
			DebounceTimer = DebounceSeconds;
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
	ResetObservedState(PendingChangedFiles);
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

		for (auto It = PendingChangedFiles.begin(); It != PendingChangedFiles.end();)
		{
			if (IsFileSettled(*It))
			{
				FilesToCompile.insert(*It);
				It = PendingChangedFiles.erase(It);
				continue;
			}

			++It;
		}

		if (FilesToCompile.empty())
		{
			return;
		}

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
		RequeueChangedFiles(ChangedFiles);
		DebounceTimer = DebounceSeconds;
	}
	else
	{
		UE_LOG((std::string("[ShaderHotReload] compile worker produced no transaction for files=") +
			std::to_string(ChangedFiles.size()) + "\n").c_str());
	}

	bCompileRunning = false;
}

void FShaderHotReloadService::ResetObservedState(const std::unordered_set<std::wstring>& Files)
{
	for (const std::wstring& File : Files)
	{
		PendingFileObservations.erase(File);
	}
}

void FShaderHotReloadService::RequeueChangedFiles(const std::unordered_set<std::wstring>& Files)
{
	for (const std::wstring& File : Files)
	{
		PendingChangedFiles.insert(File);
		PendingFileObservations.erase(File);
	}
}

bool FShaderHotReloadService::IsFileSettled(const std::wstring& File)
{
	namespace fs = std::filesystem;

	std::error_code Error;
	if (!fs::exists(File, Error) || Error)
	{
		PendingFileObservations.erase(File);
		return false;
	}

	const uintmax_t FileSize = fs::file_size(File, Error);
	if (Error)
	{
		PendingFileObservations.erase(File);
		return false;
	}

	const fs::file_time_type WriteTime = fs::last_write_time(File, Error);
	if (Error)
	{
		PendingFileObservations.erase(File);
		return false;
	}

	const FPendingFileObservation NewObservation { FileSize, WriteTime };
	auto Existing = PendingFileObservations.find(File);
	if (Existing == PendingFileObservations.end())
	{
		PendingFileObservations.emplace(File, NewObservation);
		return false;
	}

	const bool bSettled = Existing->second.Size == NewObservation.Size
		&& Existing->second.WriteTime == NewObservation.WriteTime;
	Existing->second = NewObservation;
	return bSettled;
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
