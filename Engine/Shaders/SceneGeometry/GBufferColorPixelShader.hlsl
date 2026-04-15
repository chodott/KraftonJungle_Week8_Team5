#include "../MeshVertexCommon.hlsli"

cbuffer MaterialData : register(b2)
{
	float4 BaseColor;
};

struct GBUFFER_OUTPUT
{
	float4 GBufferA : SV_Target0;
	float4 GBufferB : SV_Target1;
	float4 GBufferC : SV_Target2;
};

GBUFFER_OUTPUT main(VS_OUTPUT Input)
{
	GBUFFER_OUTPUT Output;

	float3 Normal = normalize(Input.Normal);
	if (all(abs(Normal) < 1.0e-5f))
	{
		Normal = float3(0.0f, 0.0f, 1.0f);
	}

	Output.GBufferA = float4(Input.Color.rgb * BaseColor.rgb, BaseColor.a);
	Output.GBufferB = float4(Normal, 1.0f);
	Output.GBufferC = float4(0.0f, 0.0f, 0.0f, 0.0f);
	return Output;
}
