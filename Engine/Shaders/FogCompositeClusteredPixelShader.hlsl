cbuffer FogGlobals : register(b0)
{
    float4x4 InverseViewProjection;
    float4 CameraPosition;      // xyz = camera world position
    float4 ScreenSize;          // x=width, y=height, z=1/width, w=1/height
    float4 ClusterParams;       // x=tileCountX, y=tileCountY, z=sliceCountZ, w=nearDepth
    float4 ClusterParams2;      // x=farDepth, y=globalFogCount, z=enableLocalFog, w=reserved
};

struct FFogClusterHeader
{
    uint Offset;
    uint Count;
    uint Reserved0;
    uint Reserved1;
};

struct FFogGPUData
{
    float4 FogOrigin;   // xyz = origin
    float4 FogColor;    // rgb = fog color, a = extra alpha scale
    float4 FogParams;   // x=density, y=heightFalloff, z=startDistance, w=cutoffDistance
    float4 FogParams2;  // x=maxOpacity, y=allowBackground, z/w reserved
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

Texture2D SceneColorTexture : register(t0);
Texture2D DepthTexture      : register(t1);

StructuredBuffer<FFogClusterHeader> FogClusterHeaders : register(t10);
StructuredBuffer<uint>              FogClusterIndices : register(t11);
StructuredBuffer<FFogGPUData>       FogDataBuffer     : register(t12); // local fog only
StructuredBuffer<FFogGPUData>       GlobalFogBuffer   : register(t13);

SamplerState LinearClampSampler : register(s0);
SamplerState DepthSampler       : register(s1);

float3 ReconstructWorldPosition(float2 UV, float Depth)
{
    float2 NdcXY = float2(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f);
    float4 ClipPosition = float4(NdcXY, Depth, 1.0f);
    float4 WorldPosition = mul(ClipPosition, InverseViewProjection);
    return WorldPosition.xyz / max(WorldPosition.w, 1.0e-6f);
}

float ComputeHeightFogOpticalDepthUEApprox(
    float CameraHeightRelative,
    float RayHeightDelta,
    float RayLength,
    float StartDistance,
    float HeightFalloff,
    float FogDensity)
{
    float SafeRayLength = max(RayLength, 0.0f);
    float SafeStartDistance = max(StartDistance, 0.0f);
    float TravelLength = max(SafeRayLength - SafeStartDistance, 0.0f);

    if (TravelLength <= 0.0f || FogDensity <= 0.0f)
    {
        return 0.0f;
    }

    if (HeightFalloff <= 1.0e-4f)
    {
        return FogDensity * TravelLength;
    }

    float Exponent = max(-127.0f, HeightFalloff * CameraHeightRelative);
    float RayOriginTerms = FogDensity * exp2(-Exponent);

    float EffectiveZ = (abs(RayHeightDelta) > 1.0e-4f) ? RayHeightDelta : 1.0e-4f;
    float Falloff = max(-127.0f, HeightFalloff * EffectiveZ);

    if (abs(Falloff) <= 1.0e-4f)
    {
        return RayOriginTerms * TravelLength;
    }

    float SharedIntegral = RayOriginTerms * (1.0f - exp2(-Falloff)) / Falloff;
    return max(SharedIntegral * TravelLength, 0.0f);
}

float ComputeSingleFogAmount(
    FFogGPUData Fog,
    float Depth,
    float3 WorldPosition,
    float3 ViewRay,
    float ViewDistance)
{
    float FogDensity     = max(Fog.FogParams.x, 0.0f);
    float HeightFalloff  = max(Fog.FogParams.y, 0.0f);
    float StartDistance  = max(Fog.FogParams.z, 0.0f);
    float CutoffDistance = max(Fog.FogParams.w, 0.0f);
    float MaxOpacity     = saturate(Fog.FogParams2.x);
    bool  AllowBackground = (Fog.FogParams2.y > 0.5f);

    if (FogDensity <= 0.0f || MaxOpacity <= 0.0f)
    {
        return 0.0f;
    }

    if (!AllowBackground && Depth >= 0.999999f)
    {
        return 0.0f;
    }

    if (ViewDistance <= StartDistance)
    {
        return 0.0f;
    }

    if (CutoffDistance > 0.0f && ViewDistance > CutoffDistance && Depth < 0.999999f)
    {
        return 0.0f;
    }

    float CameraHeightRelative = CameraPosition.z - Fog.FogOrigin.z;

    float OpticalDepth = ComputeHeightFogOpticalDepthUEApprox(
        CameraHeightRelative,
        ViewRay.z,
        ViewDistance,
        StartDistance,
        HeightFalloff,
        FogDensity);

    float MinTransmittance = 1.0f - MaxOpacity;
    float Transmittance = max(saturate(exp2(-OpticalDepth)), MinTransmittance);
    float FogAmount = 1.0f - Transmittance;

    FogAmount *= saturate(Fog.FogColor.a);
    return saturate(FogAmount);
}

void AccumulateFog(float3 FogColor, float FogAmount, inout float3 AccumFogColor, inout float AccumFogAlpha)
{
    float Remaining = 1.0f - AccumFogAlpha;
    AccumFogColor += Remaining * FogColor * FogAmount;
    AccumFogAlpha += Remaining * FogAmount;
}

uint ComputeFogClusterIndex(float2 UV, float Depth)
{
    uint TileCountX = max((uint)ClusterParams.x, 1u);
    uint TileCountY = max((uint)ClusterParams.y, 1u);
    uint SliceCountZ = max((uint)ClusterParams.z, 1u);

    uint TileX = min((uint)(UV.x * TileCountX), TileCountX - 1u);
    uint TileY = min((uint)(UV.y * TileCountY), TileCountY - 1u);

    float NearDepth = max(ClusterParams.w, 1.0e-4f);
    float FarDepth = max(ClusterParams2.x, NearDepth + 1.0e-4f);

    // 주의:
    // 여기 Depth -> slice 변환은 엔진의 실제 cluster 규약과 맞춰야 한다.
    // 현재는 depth buffer 값을 그대로 [near, far] 로그 slice로 근사한 예시다.
    float LinearDepth = lerp(NearDepth, FarDepth, saturate(Depth));
    float DepthRatio = saturate(log2(max(LinearDepth, NearDepth) / NearDepth) / log2(FarDepth / NearDepth));
    uint SliceZ = min((uint)(DepthRatio * SliceCountZ), SliceCountZ - 1u);

    return (SliceZ * TileCountY + TileY) * TileCountX + TileX;
}

float4 main(VSOutput Input) : SV_Target
{
    float4 SceneColor = SceneColorTexture.Sample(LinearClampSampler, Input.UV);
    float Depth = DepthTexture.Sample(DepthSampler, Input.UV).r;

    float3 WorldPosition = ReconstructWorldPosition(Input.UV, Depth);
    float3 ViewRay = WorldPosition - CameraPosition.xyz;
    float ViewDistance = length(ViewRay);

    float3 AccumFogColor = float3(0.0f, 0.0f, 0.0f);
    float AccumFogAlpha = 0.0f;

    // 1) Global fog 먼저 누적
    [loop]
    for (uint i = 0; i < (uint)ClusterParams2.y; ++i)
    {
        FFogGPUData Fog = GlobalFogBuffer[i];
        float FogAmount = ComputeSingleFogAmount(Fog, Depth, WorldPosition, ViewRay, ViewDistance);
        AccumulateFog(Fog.FogColor.rgb, FogAmount, AccumFogColor, AccumFogAlpha);

        if (AccumFogAlpha >= 0.999f)
        {
            break;
        }
    }

    // 2) Local fog는 cluster에서 꺼내서 누적
    if (AccumFogAlpha < 0.999f && ClusterParams2.z > 0.5f)
    {
        uint ClusterIndex = ComputeFogClusterIndex(Input.UV, Depth);
        FFogClusterHeader Header = FogClusterHeaders[ClusterIndex];

        [loop]
        for (uint i = 0; i < Header.Count; ++i)
        {
            uint LocalFogIndex = FogClusterIndices[Header.Offset + i];
            FFogGPUData Fog = FogDataBuffer[LocalFogIndex];
            float FogAmount = ComputeSingleFogAmount(Fog, Depth, WorldPosition, ViewRay, ViewDistance);
            AccumulateFog(Fog.FogColor.rgb, FogAmount, AccumFogColor, AccumFogAlpha);

            if (AccumFogAlpha >= 0.999f)
            {
                break;
            }
        }
    }

    // 3) fog끼리 누적 후 마지막에 한 번만 scene color에 합성
    float3 FinalColor = SceneColor.rgb * (1.0f - AccumFogAlpha) + AccumFogColor;
    return float4(FinalColor, SceneColor.a);
}
