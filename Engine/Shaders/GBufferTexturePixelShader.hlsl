#include "MeshVertexCommon.hlsli"

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

struct GBUFFER_OUTPUT
{
	float4 GBufferA : SV_Target0;
	float4 GBufferB : SV_Target1;
	float4 GBufferC : SV_Target2;
};

GBUFFER_OUTPUT main(VS_OUTPUT Input)
{
	GBUFFER_OUTPUT Output;

	float4 BaseColor = Texture.Sample(Sampler, Input.UV) * Input.Color;
	float3 Normal = normalize(Input.Normal);
	if (all(abs(Normal) < 1.0e-5f))
	{
		Normal = float3(0.0f, 0.0f, 1.0f);
	}

	Output.GBufferA = float4(BaseColor.rgb, BaseColor.a);
	Output.GBufferB = float4(Normal, 1.0f);
	Output.GBufferC = float4(0.0f, 0.0f, 0.0f, 0.0f);
	return Output;
}
