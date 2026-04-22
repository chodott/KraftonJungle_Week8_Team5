Texture2D<float4> SceneTexture : register(t0);
SamplerState SceneSampler : register(s0);

cbuffer ToneMappingConstants : register(b0)
{
	float Exposure;
	float ShoulderStrength; // Hable A
	float LinearWhite;      // Hable W / Reinhard 화이트 포인트
	float HableB;

	float HableC;
	float HableD;
	float HableE;
	float HableF;

	float AcesA;
	float AcesB;
	float AcesC;
	float AcesD;

	float AcesE;
	float Pad1;
	float Pad2;
	float Pad3;
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
	return saturate((x * (AcesA * x + AcesB)) / (x * (AcesC * x + AcesD) + AcesE));
}

#elif defined(TONEMAPPING_HABLE)

// Uncharted 2 filmic.
float3 HablePartial(float3 x)
{
	const float A = ShoulderStrength;
	const float B = HableB;
	const float C = HableC;
	const float D = HableD;
	const float E = HableE;
	const float F = HableF;
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
	return saturate((x * (AcesA * x + AcesB)) / (x * (AcesC * x + AcesD) + AcesE));
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
