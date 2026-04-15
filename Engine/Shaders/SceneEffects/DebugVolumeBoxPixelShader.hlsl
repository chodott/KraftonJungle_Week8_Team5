struct DEBUG_BOX_PS_INPUT
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
};

float4 main(DEBUG_BOX_PS_INPUT Input) : SV_Target
{
	return float4(Input.Color.rgb, saturate(Input.Color.a) * 0.25f);
}
