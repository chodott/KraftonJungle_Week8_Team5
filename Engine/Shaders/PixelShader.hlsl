#include "ShaderCommon.hlsli"

SamplerState DecalSampler : register(s0);

float4 main(VS_OUTPUT Input) : SV_TARGET
{
	float3 FinalColor = Input.Color.rgb;
	return ApplyBaseColorDecals(Input, float4(FinalColor, 1.0f), DecalSampler);
}
