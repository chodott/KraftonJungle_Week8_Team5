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
	float3 Position; // World Space
	float Intensity;
	float Range;
	float3 Padding;
};

struct FSpotLightInfo
{
	float4 Color;
	float3 Position; // World Space
	float Intensity;
	float3 Direction;
	float Range;
	float InnerCutoff; // cos(내부 각도)
	float OuterCutoff; // cos(외부 각도)
	float2 Padding;
};

cbuffer Lighting : register(b4)
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	FPointLightInfo PointLights[NUM_POINT_LIGHT];
	FSpotLightInfo SpotLights[NUM_SPOT_LIGHT];
};

// ─────────────────────────────────────────
// 헬퍼 함수
// ─────────────────────────────────────────
// 감쇠 (Point / Spot)
float CalculateAttenuation(float distance, float range)
{
    // 거리가 range를 넘으면 0, 이내면 역제곱 감쇠
	float attenuation = 1.0f / (1.0f + (distance * distance));
	float rangeFactor = saturate(1.0f - (distance / range));
	return attenuation * rangeFactor * rangeFactor;
}

#if HAS_NORMAL_MAP
float3 GetNormalFromMap(float3 vertexNormal, float3 tangent,
                        float3 bitangent, float2 uv)
{
    // [0,1] 샘플링 → [-1,1] 언패킹
    float3 tangentNormal = NormalMap.Sample(Sampler, uv).rgb * 2.0f - 1.0f;

    // TBN 행렬 (Tangent Space → World Space)
    float3 T = normalize(tangent);
    float3 B = normalize(bitangent);
    float3 N = normalize(vertexNormal);
    float3x3 TBN = float3x3(T, B, N);

    return normalize(mul(tangentNormal, TBN));
}
#endif

// ─────────────────────────────────────────
// 조명 계산 함수
// ─────────────────────────────────────────
// Ambient Light
float4 CalculateAmbientLight(FAmbientLightInfo info)
{
	return info.Color * info.Intensity;
}

// Directional Light
float4 CalculateDirectionalLight(FDirectionalLightInfo info,
                                 float3 worldPos, float3 N, float3 V)
{
	float3 L = normalize(-info.Direction); // 광원 방향 반전 (표면→광원)
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
	
	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);

	float4 diffuse = info.Color * info.Intensity * diff;
	float4 specular = info.Color * info.Intensity * spec;

	return diffuse + specular;
}

// Point Light
float4 CalculatePointLight(FPointLightInfo info,
                           float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.Position - worldPos;
	float distance = length(toLight);

    // 범위 밖이면 기여 없음
	if (distance > info.Range)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
	
	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.Range);

	float4 diffuse = info.Color * info.Intensity * diff * attenuation;
	float4 specular = info.Color * info.Intensity * spec * attenuation;

	return diffuse + specular;
}

// Spot Light
float4 CalculateSpotLight(FSpotLightInfo info,
                          float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.Position - worldPos;
	float distance = length(toLight);

	if (distance > info.Range)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

    // 원뿔 각도 체크
	float theta = dot(L, normalize(-info.Direction));
	float intensity = saturate(
        (theta - info.OuterCutoff) / (info.InnerCutoff - info.OuterCutoff)
    );

    // 원뿔 밖이면 기여 없음
	if (intensity <= 0.0f)
		return float4(0, 0, 0, 0);

	float diff = max(0.0f, dot(N, L));
	
	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.Range);

	float4 diffuse = info.Color * info.Intensity * diff * attenuation * intensity;
	float4 specular = info.Color * info.Intensity * spec * attenuation * intensity;

	return diffuse + specular;
}

#endif