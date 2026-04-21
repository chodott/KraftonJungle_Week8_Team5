#include "../LightCommon.hlsli"

StructuredBuffer<FLightCullProxyGPU> InputLightCullProxies : register(t0);
StructuredBuffer<FTileDepthBoundsGPU> InputTileDepthBounds : register(t1);
RWStructuredBuffer<FLightClusterHeader> OutClusterLightHeaders : register(u0);
RWStructuredBuffer<uint>                OutClusterLightIndices : register(u1);

static const float kEpsilon = 1e-5f;

void ComputeClusterSliceDepthRange(uint clusterZ, out float zNear, out float zFar)
{
    zNear = exp((float(clusterZ)     - LogZBias) / LogZScale);
    zFar  = exp((float(clusterZ + 1) - LogZBias) / LogZScale);

    zNear = clamp(zNear, NearZ, FarZ);
    zFar  = clamp(zFar,  NearZ, FarZ);
}

float2 ComputeTileNdcMin(uint2 tileCoord)
{
    float2 tileMinPx = float2(tileCoord * uint2(16, 16));
    float2 tileMaxPx = tileMinPx + float2(16.0f, 16.0f);
    float2 screenSize = ScreenParams.xy;

    return float2(
        (tileMinPx.x / screenSize.x) * 2.0f - 1.0f,
        1.0f - (tileMaxPx.y / screenSize.y) * 2.0f);
}

float2 ComputeTileNdcMax(uint2 tileCoord)
{
    float2 tileMinPx = float2(tileCoord * uint2(16, 16));
    float2 tileMaxPx = tileMinPx + float2(16.0f, 16.0f);
    float2 screenSize = ScreenParams.xy;

    return float2(
        (tileMaxPx.x / screenSize.x) * 2.0f - 1.0f,
        1.0f - (tileMinPx.y / screenSize.y) * 2.0f);
}

float3 NdcToView(float2 ndcXY, float viewDepth)
{
    float4 clipPos = float4(ndcXY, 1.0f, 1.0f);
    float4 viewPos = mul(clipPos, ClusterInverseProjection);
    viewPos.xyz /= max(viewPos.w, kEpsilon);

    float scale = viewDepth / max(viewPos.x, kEpsilon);
    return viewPos.xyz * scale;
}

bool BuildDepthAwareClusterBoundingSphere(
    uint2 tileCoord,
    uint clusterZ,
    float tileMinViewZ,
    float tileMaxViewZ,
    out float3 centerVS,
    out float radius)
{
    float sliceNear, sliceFar;
    ComputeClusterSliceDepthRange(clusterZ, sliceNear, sliceFar);

    float zNear = max(sliceNear, tileMinViewZ);
    float zFar  = min(sliceFar,  tileMaxViewZ);

    if (zFar <= zNear)
    {
        centerVS = 0.0f.xxx;
        radius   = 0.0f;
        return false;
    }

    float2 ndcMin = ComputeTileNdcMin(tileCoord);
    float2 ndcMax = ComputeTileNdcMax(tileCoord);

    float3 corners[8];
    corners[0] = NdcToView(float2(ndcMin.x, ndcMin.y), zNear);
    corners[1] = NdcToView(float2(ndcMax.x, ndcMin.y), zNear);
    corners[2] = NdcToView(float2(ndcMin.x, ndcMax.y), zNear);
    corners[3] = NdcToView(float2(ndcMax.x, ndcMax.y), zNear);

    corners[4] = NdcToView(float2(ndcMin.x, ndcMin.y), zFar);
    corners[5] = NdcToView(float2(ndcMax.x, ndcMin.y), zFar);
    corners[6] = NdcToView(float2(ndcMin.x, ndcMax.y), zFar);
    corners[7] = NdcToView(float2(ndcMax.x, ndcMax.y), zFar);

    centerVS = 0.0f.xxx;
    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        centerVS += corners[i];
    }
    centerVS /= 8.0f;

    radius = 0.0f;
    [unroll]
    for (uint j = 0; j < 8; ++j)
    {
        radius = max(radius, distance(centerVS, corners[j]));
    }

    return true;
}

bool BuildClusterBoundingSphereWS(
    uint3 clusterCoord,
    float tileMinViewZ,
    float tileMaxViewZ,
    out float3 centerWS,
    out float radius)
{
    float3 centerVS;
    if (!BuildDepthAwareClusterBoundingSphere(
        clusterCoord.xy,
        clusterCoord.z,
        tileMinViewZ,
        tileMaxViewZ,
        centerVS,
        radius))
    {
        centerWS = 0.0f.xxx;
        radius   = 0.0f;
        return false;
    }

    centerWS = mul(float4(centerVS, 1.0f), ClusterInverseView).xyz;
    return true;
}

bool IntersectsSphereBroadphase(float3 clusterCenterWS, float clusterRadius, FLightCullProxyGPU proxy)
{
    float lightRadius = proxy.CullCenterRadius.w;
    float combined = clusterRadius + lightRadius;
    float3 delta = clusterCenterWS - proxy.CullCenterRadius.xyz;
    float d2 = dot(delta, delta);
    return d2 <= combined * combined;
}

bool IntersectsConeNarrowphase(float3 clusterCenterWS, float clusterRadius, FLightCullProxyGPU proxy)
{
    float3 lightPos = proxy.PositionRange.xyz;
    float range = proxy.PositionRange.w;

    float3 toCluster = clusterCenterWS - lightPos;
    float dist = length(toCluster);

    if (dist > (range + clusterRadius))
        return false;

    if (dist <= 1e-4f)
        return true;

    float3 dir = normalize(proxy.DirectionType.xyz);
    float3 centerDir = toCluster / dist;

    float cosAngle = dot(dir, centerDir);
    float outerCos = proxy.AngleParams.y;

    float angularSlack = saturate(clusterRadius / dist);
    return cosAngle >= (outerCos - angularSlack);
}

bool IntersectsPrecomputedCluster(
    float3 clusterCenterWS,
    float clusterRadius,
    FLightCullProxyGPU proxy)
{
    if (!IntersectsSphereBroadphase(clusterCenterWS, clusterRadius, proxy))
        return false;

    switch (proxy.CullShapeType)
    {
    case CULL_SHAPE_SPHERE:
        return true;
    case CULL_SHAPE_CONE:
        return IntersectsConeNarrowphase(clusterCenterWS, clusterRadius, proxy);
    default:
        return true;
    }
}

[numthreads(8, 8, 1)]
void main(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint clusterX = DispatchThreadID.x;
    uint clusterY = DispatchThreadID.y;
    uint clusterZ = DispatchThreadID.z;

    if (clusterX >= ClusterCountX || clusterY >= ClusterCountY || clusterZ >= ClusterCountZ)
        return;

    uint clusterIndex = clusterZ * (ClusterCountX * ClusterCountY)
                      + clusterY * ClusterCountX
                      + clusterX;

    FLightClusterHeader header;
    header.Offset = clusterIndex * RuntimeMaxLightsPerCluster;
    header.Count = 0u;
    header.RawCount = 0u;
    header.Pad1 = 0u;

    OutClusterLightHeaders[clusterIndex] = header;

    const uint tileIndex = clusterY * ClusterCountX + clusterX;
    FTileDepthBoundsGPU tileDepthBounds = InputTileDepthBounds[tileIndex];

    if (tileDepthBounds.HasGeometry == 0u)
    {
        return;
    }

    if (clusterZ < tileDepthBounds.TileMinSlice || clusterZ > tileDepthBounds.TileMaxSlice)
    {
        return;
    }

    float3 clusterCenterWS;
    float clusterRadius;
    if (!BuildClusterBoundingSphereWS(
        uint3(clusterX, clusterY, clusterZ),
        tileDepthBounds.MinViewZ,
        tileDepthBounds.MaxViewZ,
        clusterCenterWS,
        clusterRadius))
    {
        return;
    }

    uint writeCount = 0u;
    uint rawCount = 0u;

    [loop]
    for (uint lightIndex = 0; lightIndex < LocalLightCount && lightIndex < MAX_LOCAL_LIGHTS; ++lightIndex)
    {
        FLightCullProxyGPU proxy = InputLightCullProxies[lightIndex];

        if (IntersectsPrecomputedCluster(clusterCenterWS, clusterRadius, proxy))
        {
            if (writeCount < RuntimeMaxLightsPerCluster)
            {
                OutClusterLightIndices[header.Offset + writeCount] = lightIndex;
                ++writeCount;
            }
            ++rawCount;
        }
    }

    header.Count = min(writeCount, RuntimeMaxLightsPerCluster);
    header.RawCount = rawCount;
    OutClusterLightHeaders[clusterIndex] = header;
}
