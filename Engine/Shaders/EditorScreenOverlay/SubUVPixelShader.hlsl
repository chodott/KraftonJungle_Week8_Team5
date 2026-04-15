#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"

cbuffer SubUVConstantBuffer : register(b2)
{
	float2 CellSize;
	float2 UVOffset;
};

Texture2D MainTexture : register(t0);
SamplerState MainSampler : register(s0);

float4 main(VS_OUTPUT Input) : SV_TARGET
{
	float2 FinalUV = Input.UV * CellSize + UVOffset;
	float4 Sampled = MainTexture.Sample(MainSampler, FinalUV);

	clip(Sampled.a - 0.01f);

	return float4(Sampled.rgb, Sampled.a);
}
