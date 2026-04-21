Texture2D<float4> SceneColor : register(t0);
Texture2D<float4> BloomTex : register(t1);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BloomCompositeParams : register(b0)
{
	float BloomIntensity;
	float Exposure;
	float2 Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 GlobalID : SV_DispatchThreadID)
{
	uint width, height;
	OutputTex.GetDimensions(width, height);
	if (GlobalID.x >= width || GlobalID.y >= height)
		return;

	float3 scene = SceneColor[GlobalID.xy].rgb;
	float3 bloom = BloomTex[GlobalID.xy].rgb;

	float3 combined = scene + bloom * BloomIntensity;
	float3 final = combined * Exposure;

	OutputTex[GlobalID.xy] = float4(final, 1.0f);
}