#pragma once

#include "CoreMinimal.h"

struct ENGINE_API FLightStats
{
    uint32 TotalSceneLights      = 0;  // 씬에 있는 전체 로컬 라이트 수
    uint32 TotalLocalLights      = 0;  // GPU에 업로드된 수 (최대 MaxLocalLights)
    uint32 BudgetCulledLights    = 0;  // MaxLocalLights 초과로 잘린 수
    uint32 MaxLocalLights        = 0;

    uint32 ClusterCountX         = 0;
    uint32 ClusterCountY         = 0;
    uint32 ClusterCountZ         = 0;
    uint32 TotalClusters         = 0;

    uint32 MaxLightsPerCluster          = 0;
    uint32 TotalLightClusterAssignments = 0;  // 전체 라이트×클러스터 배정 수 (이전 프레임)
    uint32 OverflowCulledSlots          = 0;  // 클러스터 슬롯 초과로 잘린 수 (이전 프레임)

    uint64 LocalLightBufferBytes     = 0;
    uint64 CullProxyBufferBytes      = 0;
    uint64 ClusterHeaderBufferBytes  = 0;
    uint64 ClusterIndexBufferBytes   = 0;
    uint64 TotalBufferBytes          = 0;
};
