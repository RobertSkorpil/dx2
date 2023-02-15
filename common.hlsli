struct vs_input
{
    float4 position : SV_Position;
    float4 normal : NORMAL;
    float4 tex : TEXCOORD;
    float4 color : COLOR;
    uint type : TYPE;
};

struct vs_output
{
    float4 position : SV_POSITION;
    float4 screen_position : SCREEN_POSITION;
    float4 light_position : LIGHT_POSITION;
    float4 normal : NORMAL;
    float4 tex : TEXCOORD;
    float4 color : COLOR;
    uint type : TYPE;
};

cbuffer constants : register(b0)
{
    float4x4 object;
    float4x4 camera;
    float4x4 world;
    float4x4 light_mat;
    float3 light;
    int material;
}