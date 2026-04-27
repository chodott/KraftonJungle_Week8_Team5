Texture2D<float> ShadowDepthAtlas : register(t0);
SamplerState ShadowDebugSampler   : register(s0);

cbuffer ShadowAtlasPreviewCB : register(b0)
{
	float Exposure;
	float Padding0;
	float Padding1;
	float Padding2;
};

struct PSInput
{
	float4 Position : SV_Position;
	float2 UV       : TEXCOORD0;
};

float4 main(PSInput Input) : SV_Target
{
	float depth = ShadowDepthAtlas.SampleLevel(ShadowDebugSampler, Input.UV, 0.0f);
	float visibleDepth = saturate((1.0f - depth) * Exposure);
	return float4(visibleDepth, 0.0f, 0.0f, 1.0f);
}
