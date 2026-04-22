Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BloomThresholdParams : register(b0)
{
	float Threshold;
	float Knee;
	float2 Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 GlobalID : SV_DispatchThreadID)
{
	uint width, height;
	OutputTex.GetDimensions(width, height);
	if (GlobalID.x >= width || GlobalID.y >= height)
		return;

	float3 color = InputTex[GlobalID.xy].rgb;
	float brightness = max(color.r, max(color.g, color.b));

    // Soft knee threshold
	float rq = clamp(brightness - Threshold + Knee, 0.0f, 2.0f * Knee);
	rq = (rq * rq) / (4.0f * Knee + 1e-5f);
	float weight = max(rq, brightness - Threshold) / max(brightness, 1e-5f);

	OutputTex[GlobalID.xy] = float4(color * weight, 1.0f);
}