$input v_worldPos

#include <bgfx_shader.sh>

void main()
{
    vec2 coord = v_worldPos.xz;

    vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float gridLine = min(grid.x, grid.y);
    float gridAlpha = 1.0 - min(gridLine, 1.0);

    vec2 gridCoarse = abs(fract(coord * 0.1 - 0.5) - 0.5) / fwidth(coord * 0.1);
    float coarseLine = min(gridCoarse.x, gridCoarse.y);
    float coarseAlpha = (1.0 - min(coarseLine, 1.0)) * 0.6;

    float alpha = max(gridAlpha * 0.4, coarseAlpha);

    float dist = length(v_worldPos.xz);
    float fade = 1.0 - smoothstep(15.0, 40.0, dist);
    alpha *= fade;

    if (alpha < 0.01)
        discard;

    gl_FragColor = vec4(0.4, 0.45, 0.55, alpha);
}