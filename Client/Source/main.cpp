#include "Core/GameEngine.h"
#include "Platform/Windows/WindowsEngineLaunch.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	FEngineLaunchConfig Config;
	Config.Title = L"Jungle Client";
	Config.Width = 1280;
	Config.Height = 720;
	Config.CreateEngine = []()
	{
		return std::make_unique<FGameEngine>();
	};

	FWindowsEngineLaunch Launch;
	return Launch.Run(hInstance, Config);
}
