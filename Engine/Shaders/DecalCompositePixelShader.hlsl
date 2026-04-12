#include "DecalCommon.hlsli"

cbuffer DecalCompositeData : register(b4)
{
	float4x4 DecalView;
	float4x4 InverseViewProjection;
};

struct VSOutput
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
};

Texture2D SceneColorTexture : register(t0);
Texture2D DepthTexture : register(t1);

SamplerState SceneColorSampler : register(s0);
SamplerState DepthSampler : register(s1);
SamplerState DecalSampler : register(s2);

float3 ReconstructWorldPosition(float2 UV, float Depth)
{
	float2 NdcXY = float2(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f);
	float4 ClipPosition = float4(NdcXY, Depth, 1.0f);
	float4 WorldPosition = mul(ClipPosition, InverseViewProjection);
	return WorldPosition.xyz / max(WorldPosition.w, 1.0e-6f);
}

float4 main(VSOutput Input) : SV_Target
{
	float4 SceneColor = SceneColorTexture.Sample(SceneColorSampler, Input.UV);
	float Depth = DepthTexture.Sample(DepthSampler, Input.UV).r;
	if (Depth >= 0.999999f)
	{
		return SceneColor;
	}

	float3 WorldPosition = ReconstructWorldPosition(Input.UV, Depth);
	float ViewDepth = mul(float4(WorldPosition, 1.0f), DecalView).x;
	return ApplyBaseColorDecals(WorldPosition, ViewDepth, Input.Position.xy, SceneColor, DecalSampler);
}
