#pragma once
#include "CoreMinimal.h"

class FEditorEngine;

class FControlPanelWindow
{
public:
	void Render(FEditorEngine* Engine);
	void RenderLevelGameplay(FEditorEngine* Engine, bool* bOpen = nullptr);

private:
	TArray<FString> SceneFiles;
	int32 SelectedSceneIndex = -1;
};
