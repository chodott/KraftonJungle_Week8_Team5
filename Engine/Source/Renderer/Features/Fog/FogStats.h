#pragma once

#include "CoreMinimal.h"

struct ENGINE_API FFogCommonStats
{
    uint32 TotalFogVolumes = 0;
    uint32 GlobalFogVolumes = 0;
    uint32 LocalFogVolumes = 0;

    uint32 RegisteredLocalFogVolumes = 0;
    uint32 ClusterCount = 0;
    uint32 NonEmptyClusterCount = 0;
    uint32 ClusterIndexCount = 0;
    uint32 MaxFogPerCluster = 0;

    uint32 FullscreenPassCount = 0;
    uint32 DrawCallCount = 0;

    uint64 GlobalFogBufferBytes = 0;
    uint64 LocalFogBufferBytes = 0;
    uint64 ClusterHeaderBufferBytes = 0;
    uint64 ClusterIndexBufferBytes = 0;
    uint64 SceneColorCopyBytes = 0;
    uint64 TotalUploadBytes = 0;

    double ClusterBuildTimeMs = 0.0;
    double ConstantBufferUpdateTimeMs = 0.0;
    double StructuredBufferUploadTimeMs = 0.0;
    double ShadingPassTimeMs = 0.0;
    double TotalFogTimeMs = 0.0;
};

struct ENGINE_API FFogGlobalFogStats
{
    uint32 VolumeCount = 0;
    uint32 FullscreenPassCount = 0;
    uint32 DrawCallCount = 0;
    uint64 BufferBytes = 0;
    uint64 SceneColorCopyBytes = 0;
    double ConstantBufferUpdateTimeMs = 0.0;
    double ShadingPassTimeMs = 0.0;
};

struct ENGINE_API FFogClusteredLocalFogStats
{
    uint32 VolumeCount = 0;
    uint32 RegisteredVolumeCount = 0;
    uint32 ClusterCount = 0;
    uint32 NonEmptyClusterCount = 0;
    uint32 ClusterIndexCount = 0;
    uint32 MaxFogPerCluster = 0;
    double AvgFogPerCluster = 0.0;
    double AvgFogPerNonEmptyCluster = 0.0;
    double AvgClusterRegistrationsPerRegisteredVolume = 0.0;
    uint64 LocalFogBufferBytes = 0;
    uint64 ClusterHeaderBufferBytes = 0;
    uint64 ClusterIndexBufferBytes = 0;
    uint64 TotalUploadBytes = 0;
    double ClusterBuildTimeMs = 0.0;
    double StructuredBufferUploadTimeMs = 0.0;
};

struct ENGINE_API FFogStats
{
    FFogCommonStats Common;
    FFogGlobalFogStats Global;
    FFogClusteredLocalFogStats ClusteredLocal;
};
