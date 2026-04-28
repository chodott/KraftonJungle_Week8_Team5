struct ESMMomentPSInput
{
    float4 Position : SV_POSITION;
};

cbuffer ESMConstants : register(b9)
{
	float Exponent;
	float3 Pad;
};

float main(ESMMomentPSInput Input) : SV_Target0
{
    float depth = saturate(Input.Position.z);
    
    float esm = exp(Exponent * depth);
    
    return esm;
}