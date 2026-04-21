#include "Renderer/HotReload/ShaderDirectoryWatcherWin.h"

#include "Renderer/Resources/Shader/ShaderPathUtils.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace
{
	bool ContainsShaderSourceMarker(const std::filesystem::path& Path)
	{
		if (IsCompiledShaderExtension(Path))
		{
			return false;
		}

		std::wstring Filename = Path.filename().wstring();
		std::transform(Filename.begin(), Filename.end(), Filename.begin(), towlower);
		return Filename.find(L".hlsl") != std::wstring::npos || Filename.find(L".hlsli") != std::wstring::npos;
	}

	bool TryResolveNotifiedShaderPath(const std::filesystem::path& Path, std::wstring& OutNormalizedPath)
	{
		if (IsCompiledShaderExtension(Path))
		{
			return false;
		}

		if (IsShaderSourceExtension(Path))
		{
			OutNormalizedPath = NormalizeShaderPath(Path.c_str());
			return true;
		}

		std::wstring Filename = Path.filename().wstring();
		std::wstring LowerFilename = Filename;
		std::transform(LowerFilename.begin(), LowerFilename.end(), LowerFilename.begin(), towlower);

		size_t ExtensionPos = LowerFilename.find(L".hlsli");
		size_t ExtensionLength = 6;
		if (ExtensionPos == std::wstring::npos)
		{
			ExtensionPos = LowerFilename.find(L".hlsl");
			ExtensionLength = 5;
		}

		if (ExtensionPos == std::wstring::npos)
		{
			return false;
		}

		const std::wstring CanonicalFilename = Filename.substr(0, ExtensionPos + ExtensionLength);
		if (CanonicalFilename.empty())
		{
			return false;
		}

		const std::filesystem::path CanonicalPath = (Path.parent_path() / CanonicalFilename).lexically_normal();
		OutNormalizedPath = NormalizeShaderPath(CanonicalPath.c_str());
		return true;
	}
}

FShaderDirectoryWatcherWin::FShaderDirectoryWatcherWin() = default;

FShaderDirectoryWatcherWin::~FShaderDirectoryWatcherWin()
{
	Stop();
}

bool FShaderDirectoryWatcherWin::Start(const std::wstring& Directory)
{
	Stop();

	WatchedDirectory = Directory;
	Buffer.resize(32 * 1024);

	DirectoryHandle = CreateFileW(
		WatchedDirectory.c_str(),
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);

	if (DirectoryHandle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	OverlappedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	Overlapped = {};
	Overlapped.hEvent = OverlappedEvent;

	bRunning = true;
	Worker = std::thread(&FShaderDirectoryWatcherWin::ThreadMain, this);
	return true;
}

void FShaderDirectoryWatcherWin::Stop()
{
	bRunning = false;

	if (StopEvent)
	{
		SetEvent(StopEvent);
	}

	if (DirectoryHandle != INVALID_HANDLE_VALUE)
	{
		CancelIoEx(DirectoryHandle, &Overlapped);
	}

	if (Worker.joinable())
	{
		Worker.join();
	}

	if (DirectoryHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(DirectoryHandle);
		DirectoryHandle = INVALID_HANDLE_VALUE;
	}

	if (OverlappedEvent)
	{
		CloseHandle(OverlappedEvent);
		OverlappedEvent = nullptr;
	}

	if (StopEvent)
	{
		CloseHandle(StopEvent);
		StopEvent = nullptr;
	}
}

void FShaderDirectoryWatcherWin::EnqueueChangedFile(const std::wstring& File)
{
	std::lock_guard<std::mutex> Lock(PendingMutex);
	PendingChangedFiles.insert(File);
}

bool FShaderDirectoryWatcherWin::ConsumeChanges(std::unordered_set<std::wstring>& OutChangedFiles, bool& bOutOverflowed)
{
	std::lock_guard<std::mutex> Lock(PendingMutex);

	if (PendingChangedFiles.empty() && !bOverflowed)
	{
		bOutOverflowed = false;
		return false;
	}

	OutChangedFiles.swap(PendingChangedFiles);
	bOutOverflowed = bOverflowed;
	bOverflowed = false;
	return true;
}

void FShaderDirectoryWatcherWin::ThreadMain()
{
	while (bRunning)
	{
		ResetEvent(OverlappedEvent);
		Overlapped = {};
		Overlapped.hEvent = OverlappedEvent;

		DWORD BytesReturned = 0;
		if (!ReadDirectoryChangesW(
			DirectoryHandle,
			Buffer.data(),
			static_cast<DWORD>(Buffer.size()),
			TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
			&BytesReturned,
			&Overlapped,
			nullptr))
		{
			const DWORD LastError = GetLastError();
			if (LastError == ERROR_IO_PENDING)
			{
				// OVERLAPPED mode reports queued async work via ERROR_IO_PENDING.
			}
			else if (LastError == ERROR_OPERATION_ABORTED && !bRunning)
			{
				break;
			}
			else
			{
				std::lock_guard<std::mutex> Lock(PendingMutex);
				bOverflowed = true;
				break;
			}
		}

		HANDLE WaitHandles[2] = { StopEvent, OverlappedEvent };
		const DWORD WaitResult = WaitForMultipleObjects(2, WaitHandles, FALSE, INFINITE);
		if (WaitResult == WAIT_OBJECT_0)
		{
			break;
		}
		if (WaitResult != WAIT_OBJECT_0 + 1)
		{
			std::lock_guard<std::mutex> Lock(PendingMutex);
			bOverflowed = true;
			break;
		}

		DWORD ActualBytes = 0;
		if (!GetOverlappedResult(DirectoryHandle, &Overlapped, &ActualBytes, FALSE) || ActualBytes == 0)
		{
			const DWORD LastError = GetLastError();
			if (LastError == ERROR_OPERATION_ABORTED && !bRunning)
			{
				break;
			}

			std::lock_guard<std::mutex> Lock(PendingMutex);
			bOverflowed = true;
			continue;
		}

		BYTE* Cursor = Buffer.data();
		while (true)
		{
			FILE_NOTIFY_INFORMATION* Info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(Cursor);
			std::wstring Filename(Info->FileName, Info->FileNameLength / sizeof(WCHAR));
			std::filesystem::path FullPath = std::filesystem::path(WatchedDirectory) / Filename;
			FullPath = FullPath.lexically_normal();

			std::wstring NormalizedChangedPath;
			if (TryResolveNotifiedShaderPath(FullPath, NormalizedChangedPath))
			{
				EnqueueChangedFile(NormalizedChangedPath);
			}
			else if (ContainsShaderSourceMarker(FullPath))
			{
				std::lock_guard<std::mutex> Lock(PendingMutex);
				bOverflowed = true;
			}

			if (Info->NextEntryOffset == 0)
			{
				break;
			}

			Cursor += Info->NextEntryOffset;
		}
	}
}
