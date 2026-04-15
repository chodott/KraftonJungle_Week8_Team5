#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

float4 main(VS_OUTPUT Input) : SV_Target
{
	const float Alpha = Texture.Sample(Sampler, Input.UV).a;
	clip(Alpha - 0.333f);
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
