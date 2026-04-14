cbuffer FogGlobals : register(b0)
{
    float4x4 InverseViewProjection;
    float4x4 ViewMatrix;
    float4 CameraPosition;
    float4 ScreenSize;
    float4 ClusterParams;
    float4 ClusterParams2;
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
    float4x4 WorldToFogVolume;
    float4 FogOrigin;
    float4 FogColor;
    float4 FogParams;
    float4 FogParams2;
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
StructuredBuffer<FFogGPUData>       FogDataBuffer     : register(t12);
StructuredBuffer<FFogGPUData>       GlobalFogBuffer   : register(t13);

SamplerState LinearClampSampler : register(s0);
SamplerState DepthSampler       : register(s1);

static const uint LOCAL_FOG_INTEGRATION_STEPS = 8u;

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

bool IntersectLocalFogVolume(FFogGPUData Fog, float3 RayOriginWS, float3 RayDirWS, out float OutEnterT, out float OutExitT)
{
    float3 RayOriginLS = mul(float4(RayOriginWS, 1.0f), Fog.WorldToFogVolume).xyz;
    float3 RayDirLS = mul(float4(RayDirWS, 0.0f), Fog.WorldToFogVolume).xyz;

    const float Epsilon = 1.0e-6f;
    float3 SafeDir = float3(
        abs(RayDirLS.x) > Epsilon ? RayDirLS.x : (RayDirLS.x >= 0.0f ? Epsilon : -Epsilon),
        abs(RayDirLS.y) > Epsilon ? RayDirLS.y : (RayDirLS.y >= 0.0f ? Epsilon : -Epsilon),
        abs(RayDirLS.z) > Epsilon ? RayDirLS.z : (RayDirLS.z >= 0.0f ? Epsilon : -Epsilon));

    float3 InvDir = 1.0f / SafeDir;
    float3 T0 = (-0.5f - RayOriginLS) * InvDir;
    float3 T1 = ( 0.5f - RayOriginLS) * InvDir;
    float3 Tmin3 = min(T0, T1);
    float3 Tmax3 = max(T0, T1);

    float TEnter = max(max(Tmin3.x, Tmin3.y), Tmin3.z);
    float TExit = min(min(Tmax3.x, Tmax3.y), Tmax3.z);

    OutEnterT = TEnter;
    OutExitT = TExit;
    return TExit >= max(TEnter, 0.0f);
}

float ComputeLocalDensity(FFogGPUData Fog, float3 WorldPosition)
{
    float FogDensity = max(Fog.FogParams.x, 0.0f);
    float HeightFalloff = max(Fog.FogParams.y, 0.0f);

    if (FogDensity <= 0.0f)
    {
        return 0.0f;
    }

    if (HeightFalloff <= 1.0e-4f)
    {
        return FogDensity;
    }

    float3 LocalPosition = mul(float4(WorldPosition, 1.0f), Fog.WorldToFogVolume).xyz;
    float Height01 = saturate(LocalPosition.z + 0.5f);
    return FogDensity * exp2(-HeightFalloff * Height01);
}

float ComputeLocalFogAmount(
    FFogGPUData Fog,
    float Depth,
    float3 WorldPosition,
    float3 ViewRay,
    float ViewDistance)
{
    float StartDistance = max(Fog.FogParams.z, 0.0f);
    float CutoffDistance = max(Fog.FogParams.w, 0.0f);
    float MaxOpacity = saturate(Fog.FogParams2.x);
    bool AllowBackground = (Fog.FogParams2.y > 0.5f);

    if (MaxOpacity <= 0.0f)
    {
        return 0.0f;
    }

    if (!AllowBackground && Depth >= 0.999999f)
    {
        return 0.0f;
    }

    float3 RayOriginWS = CameraPosition.xyz;
    float3 RayDirWS = (ViewDistance > 1.0e-4f) ? (ViewRay / ViewDistance) : float3(1.0f, 0.0f, 0.0f);

    float TEnter = 0.0f;
    float TExit = 0.0f;
    if (!IntersectLocalFogVolume(Fog, RayOriginWS, RayDirWS, TEnter, TExit))
    {
        return 0.0f;
    }

    float SurfaceT = ViewDistance;
    float TStart = max(max(TEnter, StartDistance), 0.0f);
    float TEnd = min(TExit, SurfaceT);

    if (CutoffDistance > 0.0f)
    {
        TEnd = min(TEnd, CutoffDistance);
    }

    if (TEnd <= TStart)
    {
        return 0.0f;
    }

    float3 RayDirVS = mul(float4(RayDirWS, 0.0f), ViewMatrix).xyz;
    float ViewDepthScale = max(RayDirVS.x, 1.0e-4f);
    float EntryDepth = max(TStart * ViewDepthScale, 1.0e-4f);
    float ExitDepth = max(TEnd * ViewDepthScale, EntryDepth + 1.0e-4f);
    float LogEntryDepth = log2(EntryDepth);
    float LogExitDepth = log2(ExitDepth);

    float OpticalDepth = 0.0f;
    [unroll]
    for (uint SampleIndex = 0u; SampleIndex < LOCAL_FOG_INTEGRATION_STEPS; ++SampleIndex)
    {
        float U0 = (float)SampleIndex / (float)LOCAL_FOG_INTEGRATION_STEPS;
        float U1 = (float)(SampleIndex + 1u) / (float)LOCAL_FOG_INTEGRATION_STEPS;

        float SegmentDepth0 = exp2(lerp(LogEntryDepth, LogExitDepth, U0));
        float SegmentDepth1 = exp2(lerp(LogEntryDepth, LogExitDepth, U1));
        float SegmentT0 = SegmentDepth0 / ViewDepthScale;
        float SegmentT1 = SegmentDepth1 / ViewDepthScale;
        float SegmentMidT = 0.5f * (SegmentT0 + SegmentT1);
        float SegmentLength = max(SegmentT1 - SegmentT0, 0.0f);

        float3 SampleWorldPosition = RayOriginWS + RayDirWS * SegmentMidT;
        OpticalDepth += ComputeLocalDensity(Fog, SampleWorldPosition) * SegmentLength;
    }

    float FogAmount = 1.0f - exp2(-OpticalDepth);
    FogAmount *= saturate(Fog.FogColor.a);
    FogAmount = min(FogAmount, MaxOpacity);
    return saturate(FogAmount);
}

float ComputeGlobalFogAmount(
    FFogGPUData Fog,
    float Depth,
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

uint ComputeFogClusterIndex(float2 UV, float3 WorldPosition)
{
    uint TileCountX = max((uint)ClusterParams.x, 1u);
    uint TileCountY = max((uint)ClusterParams.y, 1u);
    uint SliceCountZ = max((uint)ClusterParams.z, 1u);

    uint TileX = min((uint)(UV.x * TileCountX), TileCountX - 1u);
    uint TileY = min((uint)(UV.y * TileCountY), TileCountY - 1u);

    float NearDepth = max(ClusterParams.w, 1.0e-4f);
    float FarDepth = max(ClusterParams2.x, NearDepth + 1.0e-4f);

    float3 ViewPosition = mul(float4(WorldPosition, 1.0f), ViewMatrix).xyz;
    float ViewDepth = clamp(ViewPosition.x, NearDepth, FarDepth);

    float DepthRatio = saturate(log(max(ViewDepth, NearDepth) / NearDepth) / log(FarDepth / NearDepth));
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

    [loop]
    for (uint i = 0; i < (uint)ClusterParams2.y; ++i)
    {
        FFogGPUData Fog = GlobalFogBuffer[i];
        float FogAmount = ComputeGlobalFogAmount(Fog, Depth, ViewRay, ViewDistance);
        AccumulateFog(Fog.FogColor.rgb, FogAmount, AccumFogColor, AccumFogAlpha);

        if (AccumFogAlpha >= 0.999f)
        {
            break;
        }
    }

    if (AccumFogAlpha < 0.999f && ClusterParams2.z > 0.5f)
    {
        uint ClusterIndex = ComputeFogClusterIndex(Input.UV, WorldPosition);
        FFogClusterHeader Header = FogClusterHeaders[ClusterIndex];

        [loop]
        for (uint i = 0; i < Header.Count; ++i)
        {
            uint LocalFogIndex = FogClusterIndices[Header.Offset + i];
            FFogGPUData Fog = FogDataBuffer[LocalFogIndex];
            float FogAmount = ComputeLocalFogAmount(Fog, Depth, WorldPosition, ViewRay, ViewDistance);
            AccumulateFog(Fog.FogColor.rgb, FogAmount, AccumFogColor, AccumFogAlpha);

            if (AccumFogAlpha >= 0.999f)
            {
                break;
            }
        }
    }

    float3 FinalColor = SceneColor.rgb * (1.0f - AccumFogAlpha) + AccumFogColor;
    return float4(FinalColor, SceneColor.a);
}
