#include "../LightCommon.hlsli"

Texture2D<float> InputDepthTexture : register(t1);
StructuredBuffer<FLightCullProxyGPU> InputLightCullProxies : register(t0);
RWStructuredBuffer<uint> OutClusterLightMasks : register(u0);

static const float kEpsilon = 1e-5f;
static const float kDepthSkyThreshold = 0.999999f;

float DeviceDepthToViewZ(float deviceDepth)
{
    float4 clipPos = float4(0.0f, 0.0f, deviceDepth, 1.0f);
    float4 viewPos = mul(clipPos, ClusterInverseProjection);
    return max(-viewPos.z / max(viewPos.w, kEpsilon), NearZ);
}

void ComputeClusterSliceDepthRange(uint clusterZ, out float zNear, out float zFar)
{
    zNear = exp((float(clusterZ)     - LogZBias) / LogZScale);
    zFar  = exp((float(clusterZ + 1) - LogZBias) / LogZScale);

    zNear = clamp(zNear, NearZ, FarZ);
    zFar  = clamp(zFar,  NearZ, FarZ);
}

bool ComputeTileDepthBounds(uint2 tileCoord, out float minViewZ, out float maxViewZ)
{
    uint2 tileMinPx = tileCoord * uint2(16, 16);
    uint2 tileMaxPx = min(tileMinPx + uint2(16, 16), uint2((uint)ScreenParams.x, (uint)ScreenParams.y));

    minViewZ = FarZ;
    maxViewZ = NearZ;

    bool hasGeometry = false;

    [loop]
    for (uint y = tileMinPx.y; y < tileMaxPx.y; ++y)
    {
        [loop]
        for (uint x = tileMinPx.x; x < tileMaxPx.x; ++x)
        {
            float deviceDepth = InputDepthTexture.Load(int3(x, y, 0));

            if (deviceDepth >= kDepthSkyThreshold)
            {
                continue;
            }

            float viewZ = DeviceDepthToViewZ(deviceDepth);
            minViewZ = min(minViewZ, viewZ);
            maxViewZ = max(maxViewZ, viewZ);
            hasGeometry = true;
        }
    }

    return hasGeometry;
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

float3 NdcToView(float2 ndcXY, float viewZ)
{
    float4 clipPos = float4(ndcXY, 1.0f, 1.0f);
    float4 viewPos = mul(clipPos, ClusterInverseProjection);
    viewPos.xyz /= max(viewPos.w, kEpsilon);

    float scale = viewZ / max(-viewPos.z, kEpsilon);
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

bool IntersectsDepthAwareCluster(
    uint3 clusterCoord,
    float tileMinViewZ,
    float tileMaxViewZ,
    FLightCullProxyGPU proxy)
{
    float3 clusterCenterVS;
    float clusterRadius;
    if (!BuildDepthAwareClusterBoundingSphere(
        clusterCoord.xy,
        clusterCoord.z,
        tileMinViewZ,
        tileMaxViewZ,
        clusterCenterVS,
        clusterRadius))
    {
        return false;
    }

    float3 clusterCenterWS = mul(float4(clusterCenterVS, 1.0f), ClusterInverseView).xyz;

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

[numthreads(1, 1, 1)]
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

    uint baseWord = clusterIndex * LIGHT_MASK_WORD_COUNT;

    [unroll]
    for (uint w = 0; w < LIGHT_MASK_WORD_COUNT; ++w)
    {
        OutClusterLightMasks[baseWord + w] = 0u;
    }

    float tileMinViewZ;
    float tileMaxViewZ;
    if (!ComputeTileDepthBounds(uint2(clusterX, clusterY), tileMinViewZ, tileMaxViewZ))
    {
        return;
    }

    uint tileMinSlice = ComputeZSlice(tileMinViewZ);
    uint tileMaxSlice = ComputeZSlice(tileMaxViewZ);

    tileMinSlice = (tileMinSlice > 0u) ? (tileMinSlice - 1u) : 0u;
    tileMaxSlice = min(tileMaxSlice + 1u, ClusterCountZ - 1u);

    if (clusterZ < tileMinSlice || clusterZ > tileMaxSlice)
    {
        return;
    }

    uint localMask[LIGHT_MASK_WORD_COUNT];
    [unroll]
    for (uint i = 0; i < LIGHT_MASK_WORD_COUNT; ++i)
    {
        localMask[i] = 0u;
    }

    [loop]
    for (uint lightIndex = 0; lightIndex < LocalLightCount && lightIndex < MAX_LOCAL_LIGHTS; ++lightIndex)
    {
        FLightCullProxyGPU proxy = InputLightCullProxies[lightIndex];

        if (IntersectsDepthAwareCluster(uint3(clusterX, clusterY, clusterZ), tileMinViewZ, tileMaxViewZ, proxy))
        {
            uint word = lightIndex >> 5;
            uint bit  = lightIndex & 31u;
            localMask[word] |= (1u << bit);
        }
    }

    [unroll]
    for (uint w = 0; w < LIGHT_MASK_WORD_COUNT; ++w)
    {
        OutClusterLightMasks[baseWord + w] = localMask[w];
    }
}