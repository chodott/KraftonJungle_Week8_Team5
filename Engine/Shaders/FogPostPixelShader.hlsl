cbuffer FFogCompositeCB : register(b0)
{
    row_major float4x4 InvViewProj;
    row_major float4x4 ViewMatrix;
    float4 CameraPosition;
    float4 ViewInfo; // x=width, y=height, z=near, w=far
    float4 FogInfo;  // x=total fog count, y=global fog count, z=local fog count
};

cbuffer FFogClusterCB : register(b1)
{
    uint ClusterCountX;
    uint ClusterCountY;
    uint ClusterCountZ;
    uint MaxClusterItems;

    float ViewportWidth;
    float ViewportHeight;
    float NearZ;
    float FarZ;

    float LogZScale;
    float LogZBias;
    float TileWidth;
    float TileHeight;
};

struct FFogClusterHeader
{
    uint Offset;
    uint Count;
    uint Pad0;
    uint Pad1;
};

 
{
    row_major float4x4 WorldToFogVolume;
    float4 FogOrigin;
    float4 FogColor;
    float4 FogParams;
    float4 FogParams2;
};

Texture2D SceneColorTexture : register(t0);
Texture2D SceneDepthTexture : register(t1);
StructuredBuffer<FFogClusterHeader> FogClusterHeaders : register(t10);
StructuredBuffer<uint> FogClusterIndices : register(t11);
StructuredBuffer<FFogGPUData> FogDataBuffer : register(t12);

SamplerState LinearSampler : register(s0);
SamplerState DepthSampler : register(s1);

static const float kEpsilon = 1e-5f;

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float2 ndc;
    ndc.x = uv.x * 2.0f - 1.0f;
    ndc.y = 1.0f - uv.y * 2.0f;

    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0f);
    float4 worldPos = mul(clipPos, InvViewProj);
    return worldPos.xyz / max(worldPos.w, kEpsilon);
}

uint ComputeClusterIndex(float2 pixelPos, float viewDepth)
{
    uint tileX = min((uint)floor(pixelPos.x / max(TileWidth, 1.0f)), ClusterCountX - 1);
    uint tileY = min((uint)floor(pixelPos.y / max(TileHeight, 1.0f)), ClusterCountY - 1);

    float clampedDepth = clamp(viewDepth, NearZ, FarZ);
    uint sliceZ = (uint)clamp(floor(log(max(clampedDepth, NearZ)) * LogZScale + LogZBias), 0.0f, (float)(ClusterCountZ - 1));

    return tileX + tileY * ClusterCountX + sliceZ * ClusterCountX * ClusterCountY;
}

float ComputeFogAlpha(FFogGPUData fog, float3 worldPos, float viewDistance)
{
    if (viewDistance < fog.FogParams.z)
    {
        return 0.0f;
    }

    if (fog.FogParams.w > 0.0f && viewDistance > fog.FogParams.w)
    {
        return 0.0f;
    }

    if (fog.FogParams2.z > 0.5f)
    {
        float3 fogLocal = mul(float4(worldPos, 1.0f), fog.WorldToFogVolume).xyz;
        if (abs(fogLocal.x) > 0.5f || abs(fogLocal.y) > 0.5f || abs(fogLocal.z) > 0.5f)
        {
            return 0.0f;
        }
    }

    float heightDelta = worldPos.z - fog.FogOrigin.z;
    float heightFactor = exp(-abs(heightDelta) * max(fog.FogParams.y, 0.0f));
    float density = max(fog.FogParams.x, 0.0f) * heightFactor;
    float alpha = 1.0f - exp(-density * max(viewDistance - fog.FogParams.z, 0.0f));
    return saturate(min(alpha, fog.FogParams2.x));
}

void AccumulateFog(inout float3 fogColorAccum, inout float fogAlphaAccum, FFogGPUData fog, float alpha)
{
    if (alpha <= 0.0f)
    {
        return;
    }

    fogColorAccum += (1.0f - fogAlphaAccum) * fog.FogColor.rgb * alpha;
    fogAlphaAccum += (1.0f - fogAlphaAccum) * alpha;
}

float4 main(float4 PositionCS : SV_POSITION) : SV_TARGET
{
    float2 uv = PositionCS.xy / ViewInfo.xy;
    float4 sceneColor = SceneColorTexture.Sample(LinearSampler, uv);
    float depth = SceneDepthTexture.Load(int3((int2)PositionCS.xy, 0)).r;

    float3 fogColorAccum = 0.0f;
    float fogAlphaAccum = 0.0f;

    // 1) Global fog 먼저 누적
    [loop]
    for (uint fogIndex = 0; fogIndex < (uint)FogInfo.y; ++fogIndex)
    {
        FFogGPUData fog = FogDataBuffer[fogIndex];
        float alpha = saturate(min(max(fog.FogColor.a, 0.0f), fog.FogParams2.x));
        if (depth < 1.0f)
        {
            float3 worldPosPre = ReconstructWorldPosition(uv, depth);
            float viewDistancePre = length(worldPosPre - CameraPosition.xyz);
            alpha = ComputeFogAlpha(fog, worldPosPre, viewDistancePre);
        }
        AccumulateFog(fogColorAccum, fogAlphaAccum, fog, alpha);
    }

    if (depth < 1.0f)
    {
        float3 worldPos = ReconstructWorldPosition(uv, depth);
        float viewDistance = length(worldPos - CameraPosition.xyz);
        float viewDepth = max(mul(float4(worldPos, 1.0f), ViewMatrix).x, NearZ);

        // 2) Local fog는 cluster에서 꺼내서 누적
        uint clusterIndex = ComputeClusterIndex(PositionCS.xy, viewDepth);
        FFogClusterHeader header = FogClusterHeaders[clusterIndex];

        [loop]
        for (uint localIndex = 0; localIndex < header.Count && localIndex < MaxClusterItems; ++localIndex)
        {
            uint fogIndex = FogClusterIndices[header.Offset + localIndex];
            FFogGPUData fog = FogDataBuffer[fogIndex];
            float alpha = ComputeFogAlpha(fog, worldPos, viewDistance);
            AccumulateFog(fogColorAccum, fogAlphaAccum, fog, alpha);
            if (fogAlphaAccum >= 0.999f)
            {
                break;
            }
        }
    }

    // 3) fog끼리 누적 후 마지막에 한 번만 scene color에 합성
    float3 outColor = sceneColor.rgb * (1.0f - fogAlphaAccum) + fogColorAccum;
    return float4(outColor, sceneColor.a);
}
