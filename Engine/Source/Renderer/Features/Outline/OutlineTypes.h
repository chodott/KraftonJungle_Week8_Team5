#pragma once

#include "CoreMinimal.h"

class FMaterial;
struct FRenderMesh;

struct ENGINE_API FOutlineRenderItem
{
	FRenderMesh* Mesh = nullptr;
	FMaterial* Material = nullptr;
	FMatrix WorldMatrix = FMatrix::Identity;
	uint32 IndexStart = 0;
	uint32 IndexCount = 0;
	bool bDisableCulling = false;
};

struct ENGINE_API FOutlineRenderRequest
{
	TArray<FOutlineRenderItem> Items;
	bool bEnabled = true;
};
