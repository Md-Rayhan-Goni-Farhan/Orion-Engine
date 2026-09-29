$input v_normal, v_worldPos, v_texcoord0, v_tangent, v_bitangent

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor,  0);
SAMPLER2D(s_texNormal, 1);
uniform vec4 u_baseColor;
uniform vec4 u_lightDir[4];
uniform vec4 u_lightColor[4];

void main()
{
    vec4 texSample  = texture2D(s_texColor,  v_texcoord0);
    vec3 albedo     = texSample.rgb * u_baseColor.rgb;

    vec3 normalSample = texture2D(s_texNormal, v_texcoord0).rgb;
    normalSample      = normalSample * 2.0 - 1.0;

    vec3 N = normalize(v_normal);
    vec3 T = normalize(v_tangent);
    vec3 B = normalize(v_bitangent);
    vec3 normal = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * N);

    vec3 lighting = vec3(0.15, 0.15, 0.15);

    for (int i = 0; i < 4; ++i)
    {
        float enabled = u_lightColor[i].w;
        vec3  ldir    = normalize(u_lightDir[i].xyz);
        float diff    = max(dot(normal, -ldir), 0.0);
        float intens  = u_lightDir[i].w;
        lighting += u_lightColor[i].rgb * diff * intens * enabled;
    }

    float alpha = texSample.a * u_baseColor.a;
    gl_FragColor = vec4(albedo * lighting, alpha);
}