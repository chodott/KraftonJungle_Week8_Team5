#pragma once

#include "CoreMinimal.h"

struct ENGINE_API FLightStats
{
    uint32 TotalSceneLights      = 0;  // ???????덈츎 ?熬곣뫕???β돦裕뉛쭚???源녿턄????
    uint32 TotalLocalLights      = 0;  // GPU?????놁Ŧ??類ｌ춨 ??(嶺뚣끉裕? MaxLocalLights)
    uint32 BudgetCulledLights    = 0;  // MaxLocalLights ?貫???앹뿉???濡〓탿 ??
    uint32 MaxLocalLights        = 0;

    uint32 ClusterCountX         = 0;
    uint32 ClusterCountY         = 0;
    uint32 ClusterCountZ         = 0;
    uint32 TotalClusters         = 0;

    uint32 MaxLightsPerCluster          = 0;
    uint32 TotalLightClusterAssignments = 0;  // ?熬곣뫕????源녿턄?筌띯돦??源??????꾩룄?????(??怨몄쓧 ?熬곣뫁???
    uint32 OverflowCulledSlots          = 0;  // ???????꾩댉 ?????貫???앹뿉???濡〓탿 ??(??怨몄쓧 ?熬곣뫁???

    uint64 LocalLightBufferBytes     = 0;
    uint64 CullProxyBufferBytes      = 0;
    uint64 TileDepthBoundsBufferBytes = 0;
    uint64 ClusterHeaderBufferBytes  = 0;
    uint64 ClusterIndexBufferBytes   = 0;
    uint64 TotalBufferBytes          = 0;

    double TileDepthBoundsPassTimeMs = 0.0;
    double LightCullingPassTimeMs    = 0.0;
    double TotalCullingTimeMs        = 0.0;
};
