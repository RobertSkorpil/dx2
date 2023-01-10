struct vs_input
{
    float4 position : SV_Position;
    float4 normal : NORMAL;
    float4 tex : TEXCOORD;
};

struct vs_output
{
	float4 position : SV_POSITION;
    float4 normal : NORMAL;
    float4 tex : TEXCOORD;
};

cbuffer constants : register(b0)
{
    float4x4 camera;
    float4 light;
    float ff;
}