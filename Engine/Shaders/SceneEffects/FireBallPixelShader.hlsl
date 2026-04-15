cbuffer FireBallData : register(b0)
{
	float4x4 InverseViewProjection;
	float4 Color;
	float4 FireballOrigin;
	float Intensity;
	float Radius;
	float RadiusFalloff;
	float PAD0;
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

float4 main(VSOutput Input) : SV_Target
{
	float Depth = DepthTexture.Sample(DepthSampler, Input.UV).r;
	
	if (Depth>= 1 - 1e-6f)
	{
		return float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	
	float3 WorldPosition = ReconstructWorldPosition(Input.UV, Depth);

	float3 ddxWP = ddx(WorldPosition);
	float3 ddyWP = ddy(WorldPosition);
	float3 SurfaceNormal = normalize(cross(ddxWP, ddyWP));

	// 표면이 파이어볼을 등지고 있으면 기여 없음
	float3 FireBallDirection = normalize(FireballOrigin.xyz - WorldPosition);
	float NdotL = dot(SurfaceNormal, FireBallDirection);
	if (NdotL <= 0.0f)
	{
		discard;
	}

	float Distance = length(FireballOrigin.xyz - WorldPosition);
	float Attenuation = saturate(1.0f - (Distance / max(Radius, 1e-6f)));
	Attenuation = pow(Attenuation, RadiusFalloff);
	// Attenuation *= NdotL; Not Work Well...

	float4 FinalColor = Color * Intensity * Attenuation;
	FinalColor.a = Attenuation;
	return FinalColor;
};