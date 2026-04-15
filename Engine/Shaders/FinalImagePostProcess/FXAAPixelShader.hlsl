cbuffer FXAAData : register(b0)
{
	float2 InvScreenSize;
	float2 Pad;
};

struct VSOutput
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
};

Texture2D SceneColor : register(t0);
SamplerState LinearSampler : register(s0);

float Luminance(float3 c)
{
	// return dot(c, float3(0.299f, 0.587f, 0.114f));
	return c.y * (0.587/0.299) + c.x;
}

float4 main(VSOutput Input) : SV_Target
{
	float2 UV = Input.UV;
	float2 Off = InvScreenSize;

	float3 rgbNW = SceneColor.SampleLevel(LinearSampler, UV + float2(-1, -1) * Off, 0).rgb;
	float3 rgbNE = SceneColor.SampleLevel(LinearSampler, UV + float2( 1, -1) * Off, 0).rgb;
	float3 rgbSW = SceneColor.SampleLevel(LinearSampler, UV + float2(-1,  1) * Off, 0).rgb;
	float3 rgbSE = SceneColor.SampleLevel(LinearSampler, UV + float2( 1,  1) * Off, 0).rgb;
	float3 rgbM  = SceneColor.SampleLevel(LinearSampler, UV, 0).rgb;

	float lumaNW = Luminance(rgbNW);
	float lumaNE = Luminance(rgbNE);
	float lumaSW = Luminance(rgbSW);
	float lumaSE = Luminance(rgbSE);
	float lumaM  = Luminance(rgbM);

	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	float lumaRange = lumaMax - lumaMin;

	// 엣지가 없으면 그대로 반환
	if (lumaRange < max(0.0312f, lumaMax * 0.125f))
	{
		return float4(rgbM, 1.0f);
	}

	float2 dir = float2(
		-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
		 ((lumaNW + lumaSW) - (lumaNE + lumaSE)));

	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25f * 0.125f), 0.0078125f);
	float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, -8.0f, 8.0f) * Off;

	float3 rgbA = 0.5f * (
		SceneColor.SampleLevel(LinearSampler, UV + dir * (1.0f / 3.0f - 0.5f), 0).rgb +
		SceneColor.SampleLevel(LinearSampler, UV + dir * (2.0f / 3.0f - 0.5f), 0).rgb);

	float3 rgbB = rgbA * 0.5f + 0.25f * (
		SceneColor.SampleLevel(LinearSampler, UV + dir * -0.5f, 0).rgb +
		SceneColor.SampleLevel(LinearSampler, UV + dir *  0.5f, 0).rgb);

	float lumaB = Luminance(rgbB);
	if (lumaB < lumaMin || lumaB > lumaMax)
		return float4(rgbA, 1.0f);

	return float4(rgbB, 1.0f);
}
