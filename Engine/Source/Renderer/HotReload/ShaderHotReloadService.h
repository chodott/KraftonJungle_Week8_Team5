#pragma once

#include "Renderer/HotReload/ShaderDirectoryWatcherWin.h"
#include "Renderer/Resources/Shader/ShaderRegistry.h"

#include <mutex>
#include <optional>
#include <thread>
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

private:
	FShaderDirectoryWatcherWin Watcher;

	std::mutex StateMutex;
	std::unordered_set<std::wstring> PendingChangedFiles;
	std::optional<FShaderReloadTransaction> ReadyTransaction;

	std::thread CompileWorker;
	bool bCompileRunning = false;
	bool bEnabled = true;
	bool bInitialized = false;
	float DebounceTimer = 0.0f;
	float DebounceSeconds = 0.25f;

	std::wstring ShaderRoot;
};
