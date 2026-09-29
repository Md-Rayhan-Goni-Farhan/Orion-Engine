#pragma once
#include <string>
#include <vector>
#include <array>
#include <bx/math.h>

struct TransformComponent {
    bx::Vec3 position = { 0.0f, 0.0f, 0.0f };
    bx::Vec3 rotation = { 0.0f, 0.0f, 0.0f };
    bx::Vec3 scale    = { 1.0f, 1.0f, 1.0f };
};
struct NameComponent { std::string name = "Entity"; };
struct MeshComponent  { std::vector<uint32_t> meshHandles; };

struct LightComponent {
    bx::Vec3 direction = { 0.0f, -1.0f, 0.0f };
    bx::Vec3 color     = { 1.0f,  1.0f, 1.0f };
    float    intensity = 1.0f;
    bool     enabled   = true;
};

// One per skinned character entity.  Stores the live pose and GPU resources.
struct SkeletonComponent
{
    uint32_t jointCount = 0;

    // inverseBindMatrices copied from SkeletonData at load time.
    std::vector<std::array<float, 16>> inverseBindMatrices;

    // Parent joint index for each joint (-1 = root).
    std::vector<int> parentIndex;

    // Current pose: local quaternion rotation per joint (xyzw).
    // Initialised to the rest rotation from the GLB.
    std::vector<std::array<float, 4>> localRotation;

    // Current pose: local translation per joint.
    std::vector<std::array<float, 3>> localTranslation;

    // Current pose: local scale per joint.
    std::vector<std::array<float, 3>> localScale;

    // World-space skinning matrices computed each frame by traversing
    // the hierarchy.  Size = jointCount, each is a column-major 4x4 float.
    std::vector<std::array<float, 16>> skinningMatrices;

    // GPU texture that carries the skinning matrices to the vertex shader.
    // Format RGBA32F, size (jointCount*4) x 1 texels (4 texels per matrix row).
    bgfx::TextureHandle boneTexture = BGFX_INVALID_HANDLE;

    // Human-readable joint names for the UI panel.
    std::vector<std::string> jointNames;

    // Which joint is currently selected in the pose editor (-1 = none).
    int selectedBone = -1;

    bool valid() const { return jointCount > 0 && bgfx::isValid(boneTexture); }
};