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

float4 project3(float4 v)
{
    return float4(v.x, v.y, v.z, 0);
}

vs_output main(vs_input input)
{
    float4x4 P =
    {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 1, 0,
    };
    
    vs_output v;
    v.position = mul(camera, mul(object, input.position));
//    v.screen_position = mul(world, mul(object, input.position));
//    v.screen_position /= v.screen_position.w;
    
    v.light_position = mul(light_mat, mul(object, input.position));
    v.normal = normalize(mul(world, project3(mul(object, input.normal))));
    v.tex = input.tex;
    v.color = input.color;
    v.type = input.type;
	return v;
}