#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Decal/DecalProjectionMode.h"

struct ENGINE_API FDecalCommonStats
{
    EDecalProjectionMode Mode = EDecalProjectionMode::ClusteredLookup;

    uint32 TotalDecals = 0;
    uint32 ActiveDecals = 0;
    uint32 VisibleDecals = 0;
    uint32 RejectedDecals = 0;
    uint32 FadeInOutDecals = 0;

    double BuildTimeMs = 0.0;
    double CullIntersectionTimeMs = 0.0;
    double ShadingPassTimeMs = 0.0;
    double TotalDecalTimeMs = 0.0;
};

struct ENGINE_API FVolumeDecalStats
{
    uint32 CandidateObjects = 0;
    uint32 IntersectPassed = 0;
    uint32 DecalDrawCalls = 0;
};

struct ENGINE_API FClusteredLookupDecalStats
{
    uint32 ClustersBuilt = 0;
    uint32 DecalCellRegistrations = 0;
    double AvgDecalsPerCell = 0.0;
    uint32 MaxDecalsPerCell = 0;
};

struct ENGINE_API FDecalStats
{
    FDecalCommonStats Common;
    FVolumeDecalStats Volume;
    FClusteredLookupDecalStats ClusteredLookup;
};
