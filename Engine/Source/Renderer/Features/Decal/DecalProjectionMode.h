#pragma once

#include "CoreMinimal.h"

// Decal projection path selection shared by engine/editor.
enum class ENGINE_API EDecalProjectionMode : uint8
{
    ClusteredLookup = 0,
    VolumeDraw = 1,
};

inline const char* GetDecalProjectionModeLabel(EDecalProjectionMode Mode)
{
    switch (Mode)
    {
    case EDecalProjectionMode::ClusteredLookup:
        return "Clustered Lookup";
    case EDecalProjectionMode::VolumeDraw:
        return "Volume Draw";
    default:
        return "Unknown";
    }
}
