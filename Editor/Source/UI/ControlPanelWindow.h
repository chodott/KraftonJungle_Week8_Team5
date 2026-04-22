#pragma once
#include "CoreMinimal.h"
#include <functional>

class FEditorEngine;

class FControlPanelWindow
{
public:
	void Render(FEditorEngine* Engine);
	void RenderLevelGameplay(FEditorEngine* Engine, bool* bOpen = nullptr);

	std::function<void()> OnSettingsChanged;

private:
	TArray<FString> SceneFiles;
	int32 SelectedSceneIndex = -1;
};
