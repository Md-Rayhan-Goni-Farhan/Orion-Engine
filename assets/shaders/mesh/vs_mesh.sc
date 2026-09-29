$input a_normal, a_position, a_tangent, a_texcoord0
$output v_normal, v_worldPos, v_texcoord0, v_tangent, v_bitangent

#include <bgfx_shader.sh>

void main()
{
    vec4 worldPos    = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos       = worldPos.xyz;
    v_normal         = mul((float3x3)u_model[0], a_normal);
    v_tangent        = mul((float3x3)u_model[0], a_tangent.xyz);
    v_bitangent      = cross(v_normal, v_tangent) * a_tangent.w;
    v_texcoord0      = a_texcoord0;
    gl_Position      = mul(u_viewProj, worldPos);
}