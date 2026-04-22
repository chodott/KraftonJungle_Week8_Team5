#pragma once

#include "CoreMinimal.h"
#include "Viewport/ViewportTypes.h"
#include <functional>
#include <Windows.h>

class FEngine;
class FEditorEngine;
class FEditorViewportRegistry;
class FPicker;
class FGizmo;

class FEditorViewportInputService
{
public:
	void TickCameraNavigation(
		FEngine* Engine,
		FEditorEngine* EditorEngine,
		FEditorViewportRegistry& ViewportRegistry,
		const FGizmo& Gizmo);

	void HandleMessage(
		FEngine* Engine,
		FEditorEngine* EditorEngine,
		HWND Hwnd,
		UINT Msg,
		WPARAM WParam,
		LPARAM LParam,
		FEditorViewportRegistry& ViewportRegistry,
		FPicker& Picker,
		FGizmo& Gizmo,
		const std::function<void()>& OnSelectionChanged);

	int32 GetScreenMouseX() const { return ScreenMouseX; }
	int32 GetScreenMouseY() const { return ScreenMouseY; }
	bool GetMarqueeSelectionRect(FRect& OutRect) const;

private:
	bool bIsMarqueeSelecting = false;
	FViewportId MarqueeViewportId = INVALID_VIEWPORT_ID;
	int32 MarqueeStartWindowX = 0;
	int32 MarqueeStartWindowY = 0;
	int32 MarqueeCurrentWindowX = 0;
	int32 MarqueeCurrentWindowY = 0;

	int32 ScreenWidth = 0;
	int32 ScreenHeight = 0;
	int32 ScreenMouseX = 0;
	int32 ScreenMouseY = 0;
};
