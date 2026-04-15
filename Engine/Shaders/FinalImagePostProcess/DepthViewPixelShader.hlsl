
Texture2D		DepthTexture : register(t0);
SamplerState	DepthSampler : register(s0);

cbuffer VisualizationCB : register(b0)
{
	float NearZ;
	float FarZ;
	uint bOrthographic;
	float Padding;
};

struct PSInput
{
	float4 Position : SV_Position;
	float2 UV		: TEXCOORD0;
};

float LinearizePerspecitveDepth(float DeviceDepth, float NearPlane, float FarPlane)
{
	return (NearPlane * FarPlane) / max(FarPlane - DeviceDepth * (FarPlane - NearPlane), 1e-6f);
}

float LinearizeOrthographicDepth(float DeviceDepth, float NearPlane, float FarPlane)
{
	return lerp(NearPlane, FarPlane, DeviceDepth);
}

float4 main(PSInput Input) : SV_Target
{
	float DeviceDepth = DepthTexture.Sample(DepthSampler, Input.UV).r;

	if (DeviceDepth >= 0.999999f)
	{
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}

	float LinearDepth = (bOrthographic != 0) ?
        LinearizeOrthographicDepth(DeviceDepth, NearZ, FarZ) :
        LinearizePerspecitveDepth(DeviceDepth, NearZ, FarZ);

	float VizNear = max(NearZ, 1e-4f);
	float VizFar = max(FarZ, VizNear + 1e-4f);
	float SafeDepth = clamp(LinearDepth, VizNear, VizFar);

	float LogNear = log2(VizNear);
	float LogFar = log2(VizFar);
	float LogDepth = log2(SafeDepth);

	float Normalized = saturate((LogDepth - LogNear) / max(LogFar - LogNear, 1e-6f));
	float Visual = frac(Normalized * 10);

	return float4(Visual.xxx, 1.0f);
}