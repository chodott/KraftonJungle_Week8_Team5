#pragma once

#include "CoreMinimal.h"
#include "Renderer/Features/Decal/DecalProjectionMode.h"

struct ENGINE_API FDecalCommonStats
{
    EDecalProjectionMode Mode = EDecalProjectionMode::ClusteredLookup;

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
    uint32 NonEmptyClusters = 0;
    uint32 DecalCellRegistrations = 0;
    double AvgDecalsPerCell = 0.0;
    double AvgDecalsPerNonEmptyCell = 0.0;
    double AvgCellRegistrationsPerVisibleDecal = 0.0;
    uint32 MaxDecalsPerCell = 0;
    uint32 UploadedDecalCount = 0;
    uint32 UploadedClusterHeaderCount = 0;
    uint32 UploadedClusterIndexCount = 0;
    uint64 DecalBufferBytes = 0;
    uint64 ClusterHeaderBufferBytes = 0;
    uint64 ClusterIndexBufferBytes = 0;
    uint64 TotalUploadBytes = 0;
};

struct ENGINE_API FDecalStats
{
    FDecalCommonStats Common;
    FVolumeDecalStats Volume;
    FClusteredLookupDecalStats ClusteredLookup;
};
