#include "common.hlsli"

struct ps_output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

ps_output main(vs_output v, bool front : SV_IsFrontFace)
{
    ps_output output;

    double zr = 0, zi = 0;
    
    output.color = float4(0, 0, 0, 1);
    for (int i = 0; i < 256; ++i)
    {
        zr = zr * zr + -zi * zi + (v.position.x - 200) / 150;
        zi = 2 * zr * zi + (v.position.y - 200) / 150;
        if(zr * zr + zi * zi > 15)
        {
            output.color.x = i / 256.0;
            output.color.y = (i % 50) / 50.0;
            output.color.z = (i % 30) / 30.0;
            break;
        }
    }
    
    output.color.a = 1;
    output.depth = v.position.z;

    return output;
}