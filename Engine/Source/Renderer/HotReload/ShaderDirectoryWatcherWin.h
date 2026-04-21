#pragma once

#include "CoreMinimal.h"
#include "EngineAPI.h"

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

class ENGINE_API FShaderDirectoryWatcherWin
{
public:
	FShaderDirectoryWatcherWin();
	~FShaderDirectoryWatcherWin();

	bool Start(const std::wstring& Directory);
	void Stop();

	bool ConsumeChanges(std::unordered_set<std::wstring>& OutChangedFiles, bool& bOutOverflowed);

private:
	void ThreadMain();
	void EnqueueChangedFile(const std::wstring& File);

private:
	std::wstring WatchedDirectory;
	HANDLE DirectoryHandle = INVALID_HANDLE_VALUE;
	HANDLE StopEvent = nullptr;
	std::thread Worker;
	std::atomic<bool> bRunning = false;

	OVERLAPPED Overlapped = {};
	HANDLE OverlappedEvent = nullptr;
	std::vector<uint8> Buffer;

	std::mutex PendingMutex;
	std::unordered_set<std::wstring> PendingChangedFiles;
	bool bOverflowed = false;
};
