cbuffer FogGlobals : register(b0)
{
    float4x4 InverseViewProjection;
    float4x4 ViewMatrix;
    float4 CameraPosition;
    float4 ScreenSize;
    float4 ClusterParams;
    float4 ClusterParams2;
    float4 ViewParams;
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

static const uint MAX_BACKGROUND_LOCAL_FOG_CANDIDATES = 64u;

float3 ReconstructWorldPosition(float2 UV, float Depth)
{
    float2 NdcXY = float2(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f);
    float4 ClipPosition = float4(NdcXY, Depth, 1.0f);
    float4 WorldPosition = mul(ClipPosition, InverseViewProjection);
    return WorldPosition.xyz / max(WorldPosition.w, 1.0e-6f);
}

float3 ReconstructWorldPositionAtFarPlane(float2 UV)
{
    return ReconstructWorldPosition(UV, 1.0f);
}

float3 ReconstructWorldPositionAtNearPlane(float2 UV)
{
    return ReconstructWorldPosition(UV, 0.0f);
}

bool IsOrthographicView()
{
    return ViewParams.x > 0.5f;
}

void BuildViewRay(float2 UV, out float3 OutRayOriginWS, out float3 OutRayDirWS)
{
    float3 FarWorldPosition = ReconstructWorldPositionAtFarPlane(UV);

    if (IsOrthographicView())
    {
        float3 NearWorldPosition = ReconstructWorldPositionAtNearPlane(UV);
        float3 RayVectorWS = FarWorldPosition - NearWorldPosition;
        float RayLength = length(RayVectorWS);

        OutRayOriginWS = NearWorldPosition;
        OutRayDirWS = (RayLength > 1.0e-6f) ? (RayVectorWS / RayLength) : float3(1.0f, 0.0f, 0.0f);
        return;
    }

    OutRayOriginWS = CameraPosition.xyz;
    OutRayDirWS = normalize(FarWorldPosition - CameraPosition.xyz);
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

float ComputeLocalDensityFromRayDepth(
    FFogGPUData Fog,
    float3 WorldPosition,
    float Depth01)
{
    float FogDensity = max(Fog.FogParams.x, 0.0f);
    float HeightFalloff = max(Fog.FogParams.y, 0.0f);

    if (FogDensity <= 0.0f)
    {
        return 0.0f;
    }

    float3 LocalPosition = mul(float4(WorldPosition, 1.0f), Fog.WorldToFogVolume).xyz;
    float Height01 = saturate(LocalPosition.z + 0.5f);

    // 로컬 전용 falloff 증폭
    const float LocalHeightFalloffScale = 3.0f;
    const float LocalDepthFalloffScale  = 2.0f;

    float HeightTerm = (HeightFalloff > 1.0e-4f)
        ? exp2(-(HeightFalloff * LocalHeightFalloffScale) * Height01)
        : 1.0f;

    float DepthTerm = (HeightFalloff > 1.0e-4f)
        ? exp2(-(HeightFalloff * LocalDepthFalloffScale) * Depth01)
        : 1.0f;

    return FogDensity * HeightTerm * DepthTerm;
}


float ComputeLocalFogAmount(
    FFogGPUData Fog,
    bool bHasSurfaceDepth,
    float SurfaceDistance,
    float3 RayOriginWS,
    float3 RayDirWS)
{
    float MaxOpacity = saturate(Fog.FogParams2.x);
    bool AllowBackground = (Fog.FogParams2.y > 0.5f);

    if (MaxOpacity <= 0.0f)
    {
        return 0.0f;
    }

    if (!AllowBackground && !bHasSurfaceDepth)
    {
        return 0.0f;
    }

    float TEnter = 0.0f;
    float TExit = 0.0f;
    if (!IntersectLocalFogVolume(Fog, RayOriginWS, RayDirWS, TEnter, TExit))
    {
        return 0.0f;
    }

    float TStart = max(TEnter, 0.0f);
    float TEnd = TExit;

    if (bHasSurfaceDepth)
    {
        TEnd = min(TEnd, SurfaceDistance);
    }

    if (TEnd <= TStart)
    {
        return 0.0f;
    }

    float SegmentLength = TEnd - TStart;
    if (SegmentLength <= 1.0e-4f)
    {
        return 0.0f;
    }

    float FullRange = max(TExit - TEnter, 1.0e-4f);

    // 두 샘플 위치
    float SampleT0 = lerp(TStart, TEnd, 0.25f);
    float SampleT1 = lerp(TStart, TEnd, 0.75f);

    // 월드 위치
    float3 SampleWorldPosition0 = RayOriginWS + RayDirWS * SampleT0;
    float3 SampleWorldPosition1 = RayOriginWS + RayDirWS * SampleT1;

    // depth 정규화
    float Depth010 = saturate((SampleT0 - TEnter) / FullRange);
    float Depth011 = saturate((SampleT1 - TEnter) / FullRange);

    // density 계산
    float Density0 = ComputeLocalDensityFromRayDepth(Fog, SampleWorldPosition0, Depth010);
    float Density1 = ComputeLocalDensityFromRayDepth(Fog, SampleWorldPosition1, Depth011);

    // 평균 density
    float AverageDensity = 0.5f * (Density0 + Density1);

    // optical depth
    float OpticalDepth = AverageDensity * SegmentLength;
    float FogAmount = 1.0f - exp2(-OpticalDepth);

    FogAmount = min(FogAmount, MaxOpacity);
    return saturate(FogAmount);
}

float ComputeGlobalFogAmount(
    FFogGPUData Fog,
    float Depth,
    float3 RayOriginWS,
    float3 ViewRay,
    float ViewDistance)
{
    float FogDensity      = max(Fog.FogParams.x, 0.0f);
    float HeightFalloff   = max(Fog.FogParams.y, 0.0f);
    float StartDistance   = max(Fog.FogParams.z, 0.0f);
    float CutoffDistance  = max(Fog.FogParams.w, 0.0f);
    float MaxOpacity      = saturate(Fog.FogParams2.x);
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

    float CameraHeightRelative = RayOriginWS.z - Fog.FogOrigin.z;
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
    return saturate(FogAmount);
}

uint ComputeFogClusterIndex(float2 UV, float3 WorldPosition)
{
    uint ClusterCountX = (uint)ClusterParams.x;
    uint ClusterCountY = (uint)ClusterParams.y;
    uint ClusterCountZ = (uint)ClusterParams.z;

    uint TileX = min((uint)(UV.x * ClusterCountX), ClusterCountX - 1u);
    uint TileY = min((uint)(UV.y * ClusterCountY), ClusterCountY - 1u);

    float3 ViewPosition = mul(float4(WorldPosition, 1.0f), ViewMatrix).xyz;
    float ViewDepth = max(ViewPosition.x, 1.0e-4f);

    float LogZScale = ClusterParams2.y;
    float LogZBias  = ClusterParams2.z;
    uint TileZ = (uint)clamp(log(ViewDepth) * LogZScale + LogZBias, 0.0f, (float)(ClusterCountZ - 1u));

    return TileX + TileY * ClusterCountX + TileZ * ClusterCountX * ClusterCountY;
}

uint ComputeFogSliceIndex(float3 WorldPosition)
{
    uint ClusterCountZ = (uint)ClusterParams.z;

    float3 ViewPosition = mul(float4(WorldPosition, 1.0f), ViewMatrix).xyz;
    float ViewDepth = max(ViewPosition.x, 1.0e-4f);

    float LogZScale = ClusterParams2.y;
    float LogZBias  = ClusterParams2.z;
    return (uint)clamp(log(ViewDepth) * LogZScale + LogZBias, 0.0f, (float)(ClusterCountZ - 1u));
}

uint ComputeFogClusterIndexFromTile(uint TileX, uint TileY, uint TileZ, uint ClusterCountX, uint ClusterCountY)
{
    return TileX + TileY * ClusterCountX + TileZ * ClusterCountX * ClusterCountY;
}

bool ContainsFogIndex(uint FogIndices[MAX_BACKGROUND_LOCAL_FOG_CANDIDATES], uint Count, uint FogIndex)
{
    for (uint FogArrayIndex = 0u; FogArrayIndex < Count; ++FogArrayIndex)
    {
        if (FogIndices[FogArrayIndex] == FogIndex)
        {
            return true;
        }
    }

    return false;
}

float4 main(VSOutput Input) : SV_TARGET
{
    float4 SceneColor = SceneColorTexture.Sample(LinearClampSampler, Input.UV);
    float Depth = DepthTexture.Sample(DepthSampler, Input.UV).r;

    bool bHasSurfaceDepth = (Depth < 0.999999f);

    float3 RayOriginWS = float3(0.0f, 0.0f, 0.0f);
    float3 RayDirWS = float3(1.0f, 0.0f, 0.0f);
    BuildViewRay(Input.UV, RayOriginWS, RayDirWS);

    float SurfaceDistance = 0.0f;
    float3 WorldPosition = ReconstructWorldPositionAtFarPlane(Input.UV);

    if (bHasSurfaceDepth)
    {
        WorldPosition = ReconstructWorldPosition(Input.UV, Depth);
        SurfaceDistance = length(WorldPosition - RayOriginWS);
    }

    float RayDistanceForGlobal = length(WorldPosition - RayOriginWS);
    float3 ViewRay = RayDirWS * RayDistanceForGlobal;

    float3 AccumulatedFogColor = float3(0.0f, 0.0f, 0.0f);
    float RemainingTransmittance = 1.0f;

    uint ClusterCountX = (uint)ClusterParams.x;
    uint ClusterCountY = (uint)ClusterParams.y;
    uint ClusterCountZ = (uint)ClusterParams.z;

    uint TileX = min((uint)(Input.UV.x * ClusterCountX), ClusterCountX - 1u);
    uint TileY = min((uint)(Input.UV.y * ClusterCountY), ClusterCountY - 1u);

    uint UniqueFogIndices[MAX_BACKGROUND_LOCAL_FOG_CANDIDATES];
    uint UniqueFogCount = 0u;
    uint MaxTileZ = bHasSurfaceDepth
        ? ComputeFogSliceIndex(WorldPosition)
        : (ClusterCountZ - 1u);

    // Local fog can live anywhere between camera and surface, so gathering only the
    // surface slice causes per-view popping when the surface depth crosses slice boundaries.
    [loop]
    for (uint TileZ = 0u; TileZ <= MaxTileZ; ++TileZ)
    {
        uint ClusterIndex = ComputeFogClusterIndexFromTile(TileX, TileY, TileZ, ClusterCountX, ClusterCountY);
        FFogClusterHeader Header = FogClusterHeaders[ClusterIndex];

        [loop]
        for (uint i = 0u; i < Header.Count; ++i)
        {
            uint FogIndex = FogClusterIndices[Header.Offset + i];

            if (ContainsFogIndex(UniqueFogIndices, UniqueFogCount, FogIndex))
            {
                continue;
            }

            if (UniqueFogCount >= MAX_BACKGROUND_LOCAL_FOG_CANDIDATES)
            {
                break;
            }

            UniqueFogIndices[UniqueFogCount++] = FogIndex;
        }
    }

    [loop]
    for (uint i = 0u; i < UniqueFogCount; ++i)
    {
        uint FogIndex = UniqueFogIndices[i];
        FFogGPUData Fog = FogDataBuffer[FogIndex];

        float FogAmount = ComputeLocalFogAmount(
            Fog,
            bHasSurfaceDepth,
            SurfaceDistance,
            RayOriginWS,
            RayDirWS);

        if (FogAmount <= 0.0f)
        {
            continue;
        }

        float3 FogColor = Fog.FogColor.rgb;
        AccumulatedFogColor += FogColor * FogAmount * RemainingTransmittance;
        RemainingTransmittance *= (1.0f - FogAmount);

        if (RemainingTransmittance <= 0.01f)
        {
            RemainingTransmittance = 0.0f;
            break;
        }
    }

    uint GlobalFogCount = (uint)ClusterParams2.w;
    [loop]
    for (uint GlobalFogIndex = 0u; GlobalFogIndex < GlobalFogCount; ++GlobalFogIndex)
    {
        FFogGPUData Fog = GlobalFogBuffer[GlobalFogIndex];
        float FogAmount = ComputeGlobalFogAmount(Fog, Depth, RayOriginWS, ViewRay, RayDistanceForGlobal);
        if (FogAmount <= 0.0f)
        {
            continue;
        }

        float3 FogColor = Fog.FogColor.rgb;
        AccumulatedFogColor += FogColor * FogAmount * RemainingTransmittance;
        RemainingTransmittance *= (1.0f - FogAmount);

        if (RemainingTransmittance <= 0.01f)
        {
            RemainingTransmittance = 0.0f;
            break;
        }
    }

    float3 FinalColor = SceneColor.rgb * RemainingTransmittance + AccumulatedFogColor;
    return float4(FinalColor, SceneColor.a);
}
