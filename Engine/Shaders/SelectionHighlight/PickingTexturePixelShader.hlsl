#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

uint main(VS_OUTPUT Input) : SV_Target
{
	const float Alpha = Texture.Sample(Sampler, Input.UV).a;
	clip(Alpha - 0.333f);
	return ObjectUUID;
}
