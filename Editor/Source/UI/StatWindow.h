#pragma once
#include "CoreMinimal.h"
#include "imgui.h"

struct FRect;
class FRenderer;

struct FObjectEntry
{
	FString Name;
	FString ClassName;
	uint32 Size = 0;
};

enum class EStatWindowMode : uint8
{
	Memory,
	Decal,
	Fog,
	GPU,
};

class FStatWindow
{
public:
	void Render(const FRect& AreaRect, EStatWindowMode Mode, FRenderer* Renderer = nullptr);
	void SetObjectCount(uint32 InCount) { ObjectCount = InCount; }
	void SetHeapUsage(uint32 InBytes) { HeapUsageBytes = InBytes; }

private:
	void RefreshObjectList();
	void RenderMemoryStats();
	void RenderDecalStats(FRenderer* Renderer);
	void RenderFogStats(FRenderer* Renderer);
	void RenderGPUStats(FRenderer* Renderer);

	uint32 ObjectCount = 0;
	uint32 HeapUsageBytes = 0;

	TArray<FObjectEntry> ObjectEntries;
	bool bShowObjectList = false;
};