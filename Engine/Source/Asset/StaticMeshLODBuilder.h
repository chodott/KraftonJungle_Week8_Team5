#pragma once
#include "CoreMinimal.h"

class UStaticMesh;

struct ENGINE_API FStaticMeshLODSettings
{
	int32 NumLODs = 3;
	float TriangleReductionStep = 0.5f;
	float ScreenSizeStep = 0.5f;
};

class ENGINE_API FStaticMeshLODBuilder
{
public:
	static void BuildLODs(UStaticMesh& Asset, const FStaticMeshLODSettings& Settings = {});
};
