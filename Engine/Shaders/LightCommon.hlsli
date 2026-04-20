#ifndef LIGHT_COMMON_HLSLI
#define LIGHT_COMMON_HLSLI

#define LIGHT_CLASS_POINT 0
#define LIGHT_CLASS_SPOT 1
#define LIGHT_CLASS_RECT 2
#define LIGHT_CLASS_TUBE 3
#define LIGHT_CLASS_CUSTOM 4

#define CULL_SHAPE_SPHERE 0
#define CULL_SHAPE_CONE 1
#define CULL_SHAPE_OBB 2
#define CULL_SHAPE_CAPSULE 3
#define CULL_SHAPE_CUSTOM 4

#define MAX_LOCAL_LIGHTS  256
#define LIGHT_MASK_WORD_COUNT (MAX_LOCAL_LIGHTS / 32)

struct FAmbientLightInfo
{
	float4 ColorIntensity;
};

struct FDirectionalLightInfo
{
	float4 ColorIntensity;
	float4 DirectionEtc;
};

cbuffer Lighting : register(b4)
{
	FAmbientLightInfo Ambient;
	FDirectionalLightInfo Directional;
	uint AmbientEnabled;
	uint DirectionalLightCount;
	uint LightingPad0;
	uint LightingPad1;
};

struct FLocalLightGPU
{
	float4 ColorIntensity;
	float4 PositionRange;
	float4 DirectionType;
	float4 AngleParams;
	
	float4 Axis0Extent;
	float4 Axis1Extent;
	float4 Axis2Extent;
	
	uint Flags;
	uint ShadowIndex;
	uint CookieIndex;
	uint IESIndex;
};

struct FLightCullProxyGPU
{
	float4 CullCenterRadius;
	
	float4 PositionRange;
	float4 DirectionType;
	float4 AngleParams;
	
	float4 Axis0Extent;
	float4 Axis1Extent;
	float4 Axis2Extent;
	
	uint Flags;
	uint LightIndex;
	uint CullShapeType;
	uint Reserved;
};

cbuffer LightClusterGlobals : register(b8)
{
	float4x4 ClusterView;
	float4x4 ClusterProjection;
	float4x4 ClusterInverseProjection;
	float4x4 ClusterInverseView;
	
	float4 ClusterCameraPosition;
	float4 ScreenParams;
	
	uint ClusterCountX;
	uint ClusterCountY;
	uint ClusterCountZ;
	uint LocalLightCount;
	
	uint DirectionalLightCount2;
	uint LightMaskWordCount;
	uint LightingEnabled;
	uint ClusterPad0;
	
	float NearZ;
	float FarZ;
	float LogZScale;
	float LogZBias;
};

StructuredBuffer<uint>           ClusterLightMasks : register(t10);
StructuredBuffer<FLocalLightGPU> LocalLights       : register(t11);
StructuredBuffer<uint>           ObjectLightMasks  : register(t12);

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
	return float4(info.ColorIntensity.xyz * info.ColorIntensity.w, 1.0f);
}

// Directional Light
float4 CalculateDirectionalLight(FDirectionalLightInfo info,
                                 float3 worldPos, float3 N, float3 V)
{
	float3 L = normalize(-info.DirectionEtc.xyz); // 광원 방향 반전 (표면→광원)
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
	
	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);

	float4 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff;
	float4 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec;

	return diffuse + specular;
}

// Point Light
float4 CalculatePointLight(FLocalLightGPU info,
                           float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.PositionRange.xyz - worldPos;
	float distance = length(toLight);

    // 범위 밖이면 기여 없음
	if (distance > info.PositionRange.w)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

	float diff = max(0.0f, dot(N, L));
	
	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.PositionRange.w);

	float4 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff * attenuation;
	float4 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec * attenuation;

	return diffuse + specular;
}

// Spot Light
float4 CalculateSpotLight(FLocalLightGPU info,
                          float3 worldPos, float3 N, float3 V)
{
	float3 toLight = info.PositionRange.xyz - worldPos;
	float distance = length(toLight);

	if (distance > info.PositionRange.w)
		return float4(0, 0, 0, 0);

	float3 L = normalize(toLight);
	float3 H = normalize(L + V);

    // 원뿔 각도 체크
	float theta = dot(L, normalize(-info.DirectionType.xyz));
	float innerCutoff = info.AngleParams.x;
	float outerCutoff = info.AngleParams.y;
	float intensity = saturate(
        (theta - outerCutoff) / (innerCutoff - outerCutoff)
    );

    // 원뿔 밖이면 기여 없음
	if (intensity <= 0.0f)
		return float4(0, 0, 0, 0);

	float diff = max(0.0f, dot(N, L));
	
	float Shininess = 32.0f; // test
	float spec = pow(max(0.0f, dot(N, H)), Shininess);
	float attenuation = CalculateAttenuation(distance, info.PositionRange.w);

	float4 diffuse = info.ColorIntensity.xyz * info.ColorIntensity.w * diff * attenuation * intensity;
	float4 specular = info.ColorIntensity.xyz * info.ColorIntensity.w * spec * attenuation * intensity;

	return diffuse + specular;
}

float4 ComputeLocalLight(FLocalLightGPU light, float3 worldPos, float3 N, float3 V)
{
	uint lightClass = (uint)light.DirectionType.w;
	
	switch (lightClass)
	{
	case LIGHT_CLASS_POINT: return CalculatePointLight(light, worldPos, N, V);
	case LIGHT_CLASS_SPOT: return CalculateSpotLight(light, worldPos, N, V);
	default: return 0.0f.xxxx;
	}
}

uint ComputeZSlice(float viewZ)
{
	float z = max(viewZ, NearZ);
	float sliceF = log(z) * LogZScale + LogZBias;
	return clamp((uint)floor(sliceF), 0, ClusterCountZ - 1);
}

uint ComputeClusterIndex(float4 svPosition, float3 worldPos)
{
	uint2 pixel = uint2(svPosition.xy);
	uint tileX = min(pixel.x / 16u, ClusterCountX - 1u);
	uint tileY = min(pixel.y / 16u, ClusterCountY - 1u);
	
	float3 viewPos = mul(float4(worldPos, 1.0f), ClusterView).xyz;
	float viewZ = max(-viewPos.z, NearZ);
	uint zSlice = ComputeZSlice(viewZ);
	
	return zSlice * (ClusterCountX * ClusterCountY) + tileY * ClusterCountX + tileX;
}

float4 ComputeObjectLocalLighting(float3 worldPos, float3 N, float3 V)
{
	float4 lighting = 0.0f.xxxx;
	
	[loop]
	for (uint w = 0; w < LIGHT_MASK_WORD_COUNT; ++w)
	{
		uint bits = ObjectLightMasks[LocalLightMaskOffset + w];
		while (bits != 0u)
		{
			uint bit = firstbitlow(bits);
			uint lightIndex = w * 32u + bit;
			
			if (lightIndex < LocalLightCount)
			{
				lighting += ComputeLocalLight(LocalLights[lightIndex], worldPos, N, V);
			}
			
			bits &= (bits - 1u);
		}
	}
	return lighting;
}
float4 ComputeClusteredLocalLighting(float4 svPosition, float3 worldPos, float3 N, float3 V)
{
	if (LightingEnabled == 0 || LocalLightCount == 0)
		return 0.0f.xxxx;

	uint clusterIndex = ComputeClusterIndex(svPosition, worldPos);
	uint baseWord = clusterIndex * LIGHT_MASK_WORD_COUNT;

	float4 lighting = 0.0f.xxxx;

	[loop]
	for (uint w = 0; w < LIGHT_MASK_WORD_COUNT; ++w)
	{
		uint bits = ClusterLightMasks[baseWord + w];

		while (bits != 0u)
		{
			uint bit = firstbitlow(bits);
			uint lightIndex = w * 32u + bit;

			if (lightIndex < LocalLightCount)
			{
				lighting += ComputeLocalLight(LocalLights[lightIndex], worldPos, N, V);
			}

			bits &= (bits - 1u);
		}
	}

	return lighting;
}
#endif