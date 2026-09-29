$input a_position
$output v_worldPos

#include <bgfx_shader.sh>

void main()
{
    vec4 worldPos = mul(u_model[0], vec4(a_position.x, 0.0, a_position.y, 1.0));
    v_worldPos = worldPos.xyz;
    gl_Position = mul(u_viewProj, worldPos);
}