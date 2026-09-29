#pragma once
#include <bgfx/bgfx.h>
#include <bx/math.h>

class GridRenderer
{
public:
    bool init();
    void shutdown();
    void draw(const float* viewMtx, const float* projMtx);

private:
    bgfx::ProgramHandle      m_program = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_vbh     = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ibh     = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout       m_layout;
};