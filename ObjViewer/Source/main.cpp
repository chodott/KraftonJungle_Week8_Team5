#include "ObjViewerEngine.h"
#include "Platform/Windows/WindowsEngineLaunch.h"
#include "Core/Paths.h"

#include <shellapi.h>

namespace
{
	FObjViewerLaunchOptions ParseLaunchOptions()
	{
		FObjViewerLaunchOptions Options;

		int32 ArgCount = 0;
		LPWSTR* Arguments = ::CommandLineToArgvW(::GetCommandLineW(), &ArgCount);
		if (Arguments == nullptr)
		{
			return Options;
		}

		for (int32 ArgIndex = 1; ArgIndex < ArgCount; ++ArgIndex)
		{
			const std::wstring Argument = Arguments[ArgIndex];
			if (Argument == L"--input" && ArgIndex + 1 < ArgCount)
			{
				Options.InputFilePath = FPaths::FromWide(Arguments[++ArgIndex]);
			}
			else if (Argument == L"--export-model" && ArgIndex + 1 < ArgCount)
			{
				Options.ExportModelPath = FPaths::FromWide(Arguments[++ArgIndex]);
			}
			else if (Argument == L"--close-when-done")
			{
				Options.bCloseWhenDone = true;
			}
		}

		::LocalFree(Arguments);
		return Options;
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	const FObjViewerLaunchOptions LaunchOptions = ParseLaunchOptions();
	FEngineLaunchConfig Config;
	Config.Title = L"Jungle ObjViewer";
	Config.Width = 1280;
	Config.Height = 720;
	Config.bShowWindow = !LaunchOptions.bCloseWhenDone;
	Config.CreateEngine = [LaunchOptions]()
	{
		return std::make_unique<FObjViewerEngine>(LaunchOptions);
	};

	FWindowsEngineLaunch Launch;
	return Launch.Run(hInstance, Config);
}
