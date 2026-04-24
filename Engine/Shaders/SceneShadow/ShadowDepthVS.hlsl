cbuffer ObjectData : register(b0)
{
	float4x4 WorldMatrix;
};

cbuffer ShadowPassData : register(b1) 
{
	float4x4 LightViewProj;
	float4 ShadowParams;
};
struct VSInput
{
	float3 PositionOS : POSITION;

};
float4 main(float3 PositionOS : POSITION) : SV_POSITION
{
	// local to world
	float4 PositionWS = mul(float4(PositionOS, 1.0f), WorldMatrix);
    
	// world space to light space
	return mul(PositionWS, LightViewProj);
}