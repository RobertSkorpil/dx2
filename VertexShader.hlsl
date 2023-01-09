#include "common.hlsli"

float4 iso(float4 pos)
{
    float alpha = 0.5;
    float beta = 1.2;
	
    float4x4 iso1 = {
        1, 0, 0, 0,
        0, cos(alpha), sin(alpha), 0,
        0, -sin(alpha), cos(alpha), 0,
        0, 0, 0, 1
    };

    float4x4 iso2 = {
        cos(beta), 0, -sin(beta), 0,
        0, 1, 0, 0,
        sin(beta), 0, cos(beta), 0,
        0, 0, 0, 1
    };
    
    float4x4 prj = mul(iso1, iso2);
    return mul(prj, pos);
}

float4 cabinet(float4 pos)
{
    float alpha = 3.14159 / 4;
    float4x4 prj =
    {
        1, 0, 0.5 * cos(alpha), 0,
        0, 1, 0.5 * sin(alpha), 0,
        0, 0, 0.01, 0,
        0, 0, 0, 1
    };
    
    return mul(prj, pos);
}

vs_output main(vs_input input, uint id : SV_VertexID)
{
	vs_output v;
    float s = 0.2;
    float f = 20;
    float n = 1;
    float4x4 prj =
    {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 1, 0
    };

    v.position = mul(camera, input.position);
    v.position = mul(prj, v.position);
    v.normal = input.normal;
    v.color = input.color;
	return v;
}