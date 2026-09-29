$input a_indices, a_weight, a_normal, a_position, a_tangent, a_texcoord0
$output v_normal, v_worldPos, v_texcoord0, v_tangent, v_bitangent
#include <bgfx_shader.sh>

SAMPLER2D(s_boneMatrices, 2);

mat4 getBoneMatrix(int index)
{
    float u0 = (float(index * 4 + 0) + 0.5) / float(textureSize(s_boneMatrices, 0).x);
    float u1 = (float(index * 4 + 1) + 0.5) / float(textureSize(s_boneMatrices, 0).x);
    float u2 = (float(index * 4 + 2) + 0.5) / float(textureSize(s_boneMatrices, 0).x);
    float u3 = (float(index * 4 + 3) + 0.5) / float(textureSize(s_boneMatrices, 0).x);
    vec4 c0 = texture2DLod(s_boneMatrices, vec2(u0, 0.5), 0.0);
    vec4 c1 = texture2DLod(s_boneMatrices, vec2(u1, 0.5), 0.0);
    vec4 c2 = texture2DLod(s_boneMatrices, vec2(u2, 0.5), 0.0);
    vec4 c3 = texture2DLod(s_boneMatrices, vec2(u3, 0.5), 0.0);
    return mat4(c0, c1, c2, c3);
}

void main()
{
    ivec4 ji = ivec4(a_indices);
    vec4  w  = a_weight;

    mat4 skin = getBoneMatrix(ji.x) * w.x
              + getBoneMatrix(ji.y) * w.y
              + getBoneMatrix(ji.z) * w.z
              + getBoneMatrix(ji.w) * w.w;

    vec4 worldPos = mul(skin, vec4(a_position, 1.0));
    v_worldPos    = worldPos.xyz;
    v_normal      = mul((float3x3)skin, a_normal);
    v_tangent     = mul((float3x3)skin, a_tangent.xyz);
    v_bitangent   = cross(v_normal, v_tangent) * a_tangent.w;
    v_texcoord0   = a_texcoord0;
    gl_Position   = mul(u_viewProj, worldPos);
}