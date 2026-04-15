#pragma once

#include "CoreMinimal.h"

struct ENGINE_API FGPUFrameStats
{
    uint32 GeometryDrawCalls = 0;
    uint32 FullscreenPassCount = 0;
    uint32 DrawCallCount = 0;
    uint32 PassCount = 0;

    uint32 DecalDrawCalls = 0;
    uint32 FogDrawCalls = 0;

    uint64 UploadBytes = 0;
    uint64 CopyBytes = 0;

    double GeometryTimeMs = 0.0;
    double PixelShadingTimeMs = 0.0;
    double MemoryBandwidthTimeMs = 0.0;
    double OverdrawFillrateTimeMs = 0.0;

    uint64 EstimatedFullscreenPixels = 0;
};