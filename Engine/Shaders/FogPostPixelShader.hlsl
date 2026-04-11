cbuffer FogData : register(b0)
{
	float4x4 InverseViewProjection;
	float4 CameraPosition;
	float4 FogOrigin;
	float4 FogColor;
	float4 FogParams;
	float4 FogParams2;
};

struct VSOutput
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
};

Texture2D DepthTexture : register(t0);
SamplerState DepthSampler : register(s0);

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

float4 main(VSOutput Input) : SV_Target
{
	float Depth = DepthTexture.Sample(DepthSampler, Input.UV).r;

	float FogDensity = max(FogParams.x, 0.0f);
	float HeightFalloff = max(FogParams.y, 0.0f);
	float StartDistance = max(FogParams.z, 0.0f);
	float CutoffDistance = max(FogParams.w, 0.0f);
	float MaxOpacity = saturate(FogParams2.x);
	bool AllowBackground = (FogParams2.y > 0.5f);

	if (FogDensity <= 0.0f || MaxOpacity <= 0.0f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	
	if (!AllowBackground && Depth >= 0.999999f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}

	float3 WorldPosition = ReconstructWorldPosition(Input.UV, Depth);
	float3 ViewRay = WorldPosition - CameraPosition.xyz;
	float ViewDistance = length(ViewRay);

	if (ViewDistance <= StartDistance)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}

	if (CutoffDistance > 0.0f && ViewDistance > CutoffDistance && Depth < 0.999999f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}

	float CameraHeightRelative = CameraPosition.z - FogOrigin.z;
	
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
	
	FogAmount *= saturate(FogColor.a);

	return float4(FogColor.rgb, FogAmount);
}