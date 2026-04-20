#ifndef LIGHT_COMMON_HLSLI
#define LIGHT_COMMON_HLSLI

#define NUM_POINT_LIGHT 4
#define NUM_SPOT_LIGHT 4

struct FAmbientLightInfo
{
	float4 Color;
	float Intensity;
	float3 Padding;
};

struct FDirectionalLightInfo
{
	float4 Color;
	float3 Direction;
	float Intensity;
};

struct FPointLightInfo
{
	float4 Color;
	float3 Position; // 월드 공간
	float Intensity;
	float Range;
	float FalloffExponent;
	float2 Padding;
};

struct FSpotLightInfo
{
	float4 Color;
	float3 Position; // 월드 공간
	float Intensity;
	float3 Direction;
	float Range;
	float InnerCutoff; // cos(내부 각도)
	float OuterCutoff; // cos(외부 각도)
	float FalloffExponent;
	float Padding;
};

cbuffer Lighting : register(b4)
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	FPointLightInfo PointLights[NUM_POINT_LIGHT];
	FSpotLightInfo SpotLights[NUM_SPOT_LIGHT];
};

//	감쇠 계산 (Point / Spot)
float CalculateAttenuation(float distance, float range, float falloffExponent)
{
	float safeRange = max(range, 1.0e-4f);
	float distanceSq = max(distance * distance, 1.0f);
	float normalizedDistance = saturate(distance / safeRange);

	//	UE와 유사한 부드러운 반경 페이드:
	// - 거리 감쇠는 inverse-square 형태를 유지
	// - 반경 경계에서는 부드러운 마스크로 감쇠
	float rangeMask = pow(saturate(1.0f - pow(normalizedDistance, 4.0f)), 2.0f);
	float attenuation = rangeMask / distanceSq;

	//	FalloffExponent는 유지하되, 기본값이 과도해지지 않도록 보정
	float exponent = (falloffExponent > 0.0f) ? falloffExponent : 0.01f;
	float artistMask = pow(saturate(1.0f - normalizedDistance), exponent);

	return attenuation * artistMask;
}

#if HAS_NORMAL_MAP
float3 GetNormalFromMap(float3 vertexNormal, float3 tangent,
						float3 bitangent, float2 uv)
{
	float3 tangentNormal = NormalMap.Sample(Sampler, uv).rgb * 2.0f - 1.0f;

	float3 T = normalize(tangent);
	float3 B = normalize(bitangent);
	float3 N = normalize(vertexNormal);
	float3x3 TBN = float3x3(T, B, N);

	return normalize(mul(tangentNormal, TBN));
}
#endif

float4 CalculateAmbientLight(FAmbientLightInfo info)
{
	return info.Color * info.Intensity;
}

float4 CalculateDirectionalLight(FDirectionalLightInfo info,
								 float3 worldPos, float3 N, float3 V)
{
	float3 L = normalize(-info.Direction);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));

	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);

	float4 diffuse = info.Color * info.Intensity * diff;
	float4 specular = info.Color * info.Intensity * spec;

	return diffuse + specular;
}

float4 CalculatePointLight(FPointLightInfo info,
						   float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.Position - worldPos;
	float distance = length(toLight);
	if (distance > info.Range)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));

	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.Range, info.FalloffExponent);

	float4 diffuse = info.Color * info.Intensity * diff * attenuation;
	float4 specular = info.Color * info.Intensity * spec * attenuation;

	return diffuse + specular;
}

float4 CalculateSpotLight(FSpotLightInfo info,
						  float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.Position - worldPos;
	float distance = length(toLight);
	if (distance > info.Range)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float theta = dot(L, normalize(-info.Direction));
	float intensity = saturate(
		(theta - info.OuterCutoff) /
		(info.InnerCutoff - info.OuterCutoff)
	);
	if (intensity <= 0.0f)
		return float4(0, 0, 0, 0);

	float diff = max(0.0f, dot(N, L));

	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.Range, info.FalloffExponent);

	float4 diffuse = info.Color * info.Intensity * diff * attenuation * intensity;
	float4 specular = info.Color * info.Intensity * spec * attenuation * intensity;

	return diffuse + specular;
}

#endif
