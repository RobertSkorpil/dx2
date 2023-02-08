#include "common.hlsli"

float4 main(vs_input input) : SV_POSITION
{
    return mul(light_mat, input.position);
}