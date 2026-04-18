#include "../FrameCommon.hlsli"
#include "../ObjectCommon.hlsli"
#include "../MeshVertexCommon.hlsli"
#include "../ShaderCommon.hlsli"

Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

#if HAS_NORMAL_MAP
Texture2D NormalMap : register(t1);
#endif

/*cbuffer MaterialData : register(b2)
{
	float4 EmissiveColor;
	float Shininess;
	float3 MaterialPadding;
};
*/

// Diffuse 텍스처 색상
#define TextureColor (Texture.Sample(Sampler, Input.UV))

// 자체 발광
#define Emissive (EmissiveColor)

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
	FAmbientLightInfo		Ambient;
	FDirectionalLightInfo	Directional;
	FPointLightInfo			PointLights[NUM_POINT_LIGHT];
	FSpotLightInfo			SpotLights[NUM_SPOT_LIGHT];
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


VS_OUTPUT Uber_VS(VS_INPUT Input)
{
	VS_OUTPUT Output;
	
	float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
	float4 ViewPos = mul(WorldPos, View);
	Output.WorldPosition = WorldPos.xyz;
	Output.Position = mul(ViewPos, Projection);
	Output.UV = Input.UV;
	Output.Color = Input.Color;
	
	Output.Normal = normalize(mul(Input.Normal, (float3x3) WorldInvTranspose));
	
	// ── Normal Map 여부에 따라 Tangent 계산 ── 
#if HAS_NORMAL_MAP
    Output.Tangent   = normalize(mul(Input.Tangent, (float3x3)World));
    Output.Bitangent = cross(Output.Normal, Output.Tangent);
#else
	Output.Tangent = float3(0, 0, 0);
	Output.Bitangent = float3(0, 0, 0);
#endif
	
	// ── Gouraud: VS에서 Blinn-Phong으로 모든 광원 계산 ──
#if LIGHTING_MODEL_GOURAUD
	float3 N = Output.Normal;
	float3 cameraPos = float3(
        View._41, // View 행렬의 이동 성분
        View._42,
        View._43
    );
	float3 V = normalize(cameraPos - Input.WorldPosition);

    float4 lighting = CalculateAmbientLight(Ambient);
    lighting += CalculateDirectionalLight(Directional, Output.WorldPosition, N, V);

    [unroll]
    for (int i = 0; i < NUM_POINT_LIGHT; ++i)
        lighting += CalculatePointLight(PointLights[i], Output.WorldPosition, N, V);

    [unroll]
    for (int j = 0; j < NUM_SPOT_LIGHT; ++j)
        lighting += CalculateSpotLight(SpotLights[j], Output.WorldPosition, N, V);

    Output.Color *= lighting;

#elif LIGHTING_MODEL_LAMBERT

#elif LIGHTING_MODEL_PHONG

#endif
	return Output;
}

float4 Uber_PS(VS_OUTPUT Input) : SV_TARGET
{
	float4 finalPixel = TextureColor;
	
	// test
	float4 EmessiveColor = float4(10, 5, 0, 1);
	finalPixel += EmessiveColor;
	
	    // ── 법선 결정 ──
#if HAS_NORMAL_MAP
    float3 N = GetNormalFromMap(Input.Normal, Input.Tangent, Input.Bitangent, Input.UV);
#else
	float3 N = normalize(Input.Normal);
#endif
	
	// ── V 벡터: 시점 방향 (카메라 위치는 FrameData에서 가져와야 하나
    //    현재 FrameData에 CameraPos가 없으므로 View 역행렬로 추출) ──
	float3 cameraPos = float3(
        View._41, // View 행렬의 이동 성분
        View._42,
        View._43
    );
	float3 V = normalize(cameraPos - Input.WorldPosition);
	
#if LIGHTING_MODEL_GOURAUD
	finalPixel *= Input.Color;
	
#elif LIGHTING_MODEL_LAMBERT
    // Diffuse만 (Specular 없음, V 벡터 불필요)
    float4 lighting = CalculateAmbientLight(Ambient);

    float3 L_dir = normalize(-Directional.Direction);
    float  diff  = max(0.0f, dot(N, L_dir));
    lighting += Directional.Color * Directional.Intensity * diff;
	
	[unroll]
    for (int i = 0; i < NUM_POINT_LIGHT; ++i)
    {
        float3 toLight    = PointLights[i].Position - Input.WorldPosition;
        float  distance   = length(toLight);
        if (distance < PointLights[i].Range)
        {
            float3 L      = normalize(toLight);
            float  diff_p = max(0.0f, dot(N, L));
            float  atten  = CalculateAttenuation(distance, PointLights[i].Range);
            lighting += PointLights[i].Color * PointLights[i].Intensity * diff_p * atten;
        }
    }

    [unroll]
    for (int j = 0; j < NUM_SPOT_LIGHT; ++j)
    {
        float3 toLight  = SpotLights[j].Position - Input.WorldPosition;
        float  distance = length(toLight);
        if (distance < SpotLights[j].Range)
        {
            float3 L         = normalize(toLight);
            float  theta     = dot(L, normalize(-SpotLights[j].Direction));
            float  intensity = saturate(
                (theta - SpotLights[j].OuterCutoff) /
                (SpotLights[j].InnerCutoff - SpotLights[j].OuterCutoff)
            );
            float diff_s = max(0.0f, dot(N, L));
            float atten  = CalculateAttenuation(distance, SpotLights[j].Range);
            lighting += SpotLights[j].Color * SpotLights[j].Intensity
                        * diff_s * atten * intensity;
        }
    }

    finalPixel *= lighting;
	
#elif LIGHTING_MODEL_PHONG
    // Diffuse + Specular (Blinn-Phong)
    float4 lighting = CalculateAmbientLight(Ambient);
    lighting += CalculateDirectionalLight(Directional, Input.WorldPosition, N, V);

    [unroll]
    for (int i = 0; i < NUM_POINT_LIGHT; ++i)
        lighting += CalculatePointLight(PointLights[i], Input.WorldPosition, N, V);

    [unroll]
    for (int j = 0; j < NUM_SPOT_LIGHT; ++j)
        lighting += CalculateSpotLight(SpotLights[j], Input.WorldPosition, N, V);

    finalPixel *= lighting;
	
#endif
	return finalPixel;
}