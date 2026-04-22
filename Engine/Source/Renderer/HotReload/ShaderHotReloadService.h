#pragma once

#include "Renderer/HotReload/ShaderDirectoryWatcherWin.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"

#include <mutex>
#include <optional>
#include <filesystem>
#include <thread>
#include <unordered_map>
#include <unordered_set>

class FRenderer;

class ENGINE_API FShaderHotReloadService
{
public:
	bool Initialize(const std::wstring& ShaderRoot);
	void Shutdown();

	void Tick(FRenderer& Renderer, float DeltaTime);

	void MarkAllDirty();
	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

private:
	void StartCompileIfNeeded();
	void CompileWorkerMain(std::unordered_set<std::wstring> ChangedFiles);
	void FullRescanShaderDirectory(std::unordered_set<std::wstring>& OutFiles) const;
	void ResetObservedState(const std::unordered_set<std::wstring>& Files);
	void RequeueChangedFiles(const std::unordered_set<std::wstring>& Files);
	bool IsFileSettled(const std::wstring& File);

	struct FPendingFileObservation
	{
		uintmax_t Size = 0;
		std::filesystem::file_time_type WriteTime = {};
	};

private:
	FShaderDirectoryWatcherWin Watcher;

	std::mutex StateMutex;
	std::unordered_set<std::wstring> PendingChangedFiles;
	std::optional<FShaderReloadTransaction> ReadyTransaction;
	std::unordered_map<std::wstring, FPendingFileObservation> PendingFileObservations;

	std::thread CompileWorker;
	bool bCompileRunning = false;
	bool bEnabled = true;
	bool bInitialized = false;
	float DebounceTimer = 0.0f;
	float DebounceSeconds = 0.25f;

	std::wstring ShaderRoot;
};
