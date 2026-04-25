Texture2DArray<float>  ShadowDepthArray   : register(t0);
Texture2DArray<float2> ShadowMomentsArray : register(t1);
SamplerState ShadowDebugSampler           : register(s0);

cbuffer ShadowDebugCB : register(b0)
{
	uint DebugMode;
	uint SliceIndex;
	float NearZ;
	float FarZ;

	uint bOrthographic;
	float Exposure;
	float Padding0;
	float Padding1;
};

struct PSInput
{
	float4 Position : SV_Position;
	float2 UV       : TEXCOORD0;
};

float LinearizePerspectiveDepth(float deviceDepth, float nearZ, float farZ)
{
	return (nearZ * farZ) / max(farZ - deviceDepth * (farZ - nearZ), 1.0e-6f);
}

float LinearizeOrthographicDepth(float deviceDepth, float nearZ, float farZ)
{
	return lerp(nearZ, farZ, deviceDepth);
}

float VisualizeDepth(float deviceDepth)
{
	if (deviceDepth >= 0.999999f)
	{
		return 0.0f;
	}

	float safeNear = max(NearZ, 1.0e-4f);
	float safeFar  = max(FarZ, safeNear + 1.0e-4f);

	float linearDepth = bOrthographic != 0
		? LinearizeOrthographicDepth(deviceDepth, safeNear, safeFar)
		: LinearizePerspectiveDepth(deviceDepth, safeNear, safeFar);

	float logNear = log2(safeNear);
	float logFar  = log2(safeFar);
	float logZ    = log2(clamp(linearDepth, safeNear, safeFar));

	return saturate((logZ - logNear) / max(logFar - logNear, 1.0e-6f));
}

float4 main(PSInput Input) : SV_Target
{
	float3 uvw = float3(Input.UV, (float)SliceIndex);

	if (DebugMode == 1u)
	{
		float depth = ShadowDepthArray.SampleLevel(ShadowDebugSampler, uvw, 0.0f);
		float v = VisualizeDepth(depth);
		return float4(v.xxx, 1.0f);
	}

	if (DebugMode == 2u)
	{
		float2 moments = ShadowMomentsArray.SampleLevel(ShadowDebugSampler, uvw, 0.0f);
		float v = moments.x;
		return float4(v.xxx, 1.0f);
	}

	if (DebugMode == 3u)
	{
		float2 moments = ShadowMomentsArray.SampleLevel(ShadowDebugSampler, uvw, 0.0f);
		float variance = max(moments.y - moments.x * moments.x, 0.0f);
		float v = saturate(variance * Exposure);
		return float4(v.xxx, 1.0f);
	}

	return float4(0.0f, 0.0f, 0.0f, 1.0f);
}