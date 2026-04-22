Texture2D<float4> SceneTexture : register(t0);
SamplerState SceneSampler : register(s0);

cbuffer ToneMappingConstants : register(b0)
{
	float Exposure;
	float ShoulderStrength; // Hable A (default 0.22)
	float LinearWhite;      // Hable W / Reinhard 화이트 포인트 (default 11.2)
	float Pad0;
};

struct PSInput
{
	float4 Position : SV_Position;
	float2 UV       : TEXCOORD0;
};

// --- 퍼뮤테이션별 톤매핑 함수 ---

#if defined(TONEMAPPING_ACES)

float3 ApplyTonemap(float3 x)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

#elif defined(TONEMAPPING_HABLE)

// Uncharted 2 filmic. B~F는 표준 기본값 고정.
float3 HablePartial(float3 x)
{
	const float A = ShoulderStrength;
	const float B = 0.22f;
	const float C = 0.10f;
	const float D = 0.20f;
	const float E = 0.01f;
	const float F = 0.30f;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 ApplyTonemap(float3 color)
{
	float3 mapped     = HablePartial(color);
	float3 whiteScale = 1.0f / HablePartial(LinearWhite.xxx);
	return saturate(mapped * whiteScale);
}

#elif defined(TONEMAPPING_REINHARD)

float3 ApplyTonemap(float3 color)
{
	float white2 = LinearWhite * LinearWhite;
	return saturate((color * (1.0f + color / white2)) / (1.0f + color));
}

#elif defined(TONEMAPPING_LINEAR)

float3 ApplyTonemap(float3 color)
{
	return saturate(color);
}

#else // 기본값: ACES

float3 ApplyTonemap(float3 x)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

#endif

// --- sRGB 감마 인코딩 ---
float3 LinearToSRGB(float3 linearColor)
{
	linearColor = saturate(linearColor);
	float3 srgb;
	srgb.r = (linearColor.r <= 0.0031308f) ? (12.92f * linearColor.r) : (1.055f * pow(linearColor.r, 1.0f / 2.4f) - 0.055f);
	srgb.g = (linearColor.g <= 0.0031308f) ? (12.92f * linearColor.g) : (1.055f * pow(linearColor.g, 1.0f / 2.4f) - 0.055f);
	srgb.b = (linearColor.b <= 0.0031308f) ? (12.92f * linearColor.b) : (1.055f * pow(linearColor.b, 1.0f / 2.4f) - 0.055f);
	return saturate(srgb);
}

float4 main(PSInput Input) : SV_Target
{
	float3 hdrColor  = SceneTexture.Sample(SceneSampler, Input.UV).rgb * Exposure;
	float3 ldrLinear = ApplyTonemap(hdrColor);
	return float4(LinearToSRGB(ldrLinear), 1.0f);
}
