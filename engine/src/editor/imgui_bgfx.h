#pragma once
#include <bgfx/bgfx.h>
#include <imgui.h>

bool imguiBgfxInit(int viewId, bgfx::TextureHandle* fontTexture);
void imguiBgfxShutdown();
void imguiBgfxRender(ImDrawData* drawData);