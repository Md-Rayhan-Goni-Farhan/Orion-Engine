#pragma once
#include <bgfx/bgfx.h>
#include <bx/math.h>

class LightRenderer
{
public:
    bool init();
    void shutdown();
    void draw(const float* viewMtx, const float* projMtx,
              const bx::Vec3& position, const bx::Vec3& direction,
              const float color[3], bool isSelected);

private:
    bgfx::ProgramHandle m_program      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_colorUniform = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_layout;
};