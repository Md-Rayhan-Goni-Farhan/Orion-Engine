#pragma once
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <vector>
#include <string>

struct MeshGeometry
{
    bgfx::VertexBufferHandle vbh       = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  ibh       = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle      texture   = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle      normalMap = BGFX_INVALID_HANDLE;
    uint32_t indexCount   = 0;
    bool     hasAlpha     = false;
    bool     hasSkin      = false;
    float    baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bx::Vec3 boundsMin = {  1e30f,  1e30f,  1e30f };
    bx::Vec3 boundsMax = { -1e30f, -1e30f, -1e30f };

    // Bone influence data — only populated when hasSkin is true.
    // joints: 4 uint16 per vertex (flat array, vertex i uses indices i*4 .. i*4+3).
    // weights: 4 floats per vertex, same layout.
    std::vector<uint16_t> joints;
    std::vector<float>    weights;
};

// Skeleton data loaded once per GLB skin and stored in the registry
// alongside the mesh primitives.  All joints for this skin live here.
struct SkeletonData
{
    // Number of joints in this skin
    uint32_t jointCount = 0;

    // inverseBindMatrices[j] is the 4x4 column-major matrix that undoes
    // joint j's rest-pose transform.  Size = jointCount.
    std::vector<std::array<float, 16>> inverseBindMatrices;

    // Parent joint index for each joint (-1 = root).
    std::vector<int> parentIndex;

    // Rest-pose local TRS stored as separate arrays so we can reset the pose.
    std::vector<std::array<float, 3>> restTranslation;
    std::vector<std::array<float, 4>> restRotation;   // quaternion xyzw
    std::vector<std::array<float, 3>> restScale;

    // Human-readable joint names (used in the Skeleton panel UI).
    std::vector<std::string> jointNames;

    // Which node index (in cgltf terms) each joint corresponds to.
    // We need this to correctly look up parent relationships.
    std::vector<int> nodeIndices;

    bool valid() const { return jointCount > 0; }
};

struct Camera
{
    bx::Vec3 eye    = { 0.0f, 1.5f, -4.0f };
    bx::Vec3 target = { 0.0f, 0.0f,  0.0f };
    bx::Vec3 up     = { 0.0f, 1.0f,  0.0f };
    float fov    = 60.0f;
    float aspect = 1280.0f / 720.0f;
    float nearZ  = 0.1f;
    float farZ   = 1000.0f;
};

class MeshRenderer
{
public:
    bool init();
    void shutdown();
    std::vector<MeshGeometry> loadGltf(const std::string& path);
    SkeletonData              loadSkeleton(const std::string& path);
    void beginFrame(const Camera& cam, uint16_t width, uint16_t height,
                    float* outView, float* outProj);
    void drawMesh(const MeshGeometry& mesh, const float* modelMtx, bool transparent);
    void drawSkinnedMesh(const MeshGeometry& mesh, const float* modelMtx,
                         bool transparent, bgfx::TextureHandle boneTexture);
    bgfx::UniformHandle getLightDirUniform()   const { return m_lightDirUniform;   }
    bgfx::UniformHandle getLightColorUniform() const { return m_lightColorUniform; }
private:
    bgfx::TextureHandle uploadTexture(const uint8_t* data, int size);

    bgfx::ProgramHandle  m_program           = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle  m_skinnedProgram    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle  m_texSampler        = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle  m_normalSampler     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle  m_baseColorUniform  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle  m_lightDirUniform   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle  m_lightColorUniform = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle  m_whiteTexture      = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle  m_flatNormal        = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle  m_boneSampler       = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout   m_layout;
    bgfx::VertexLayout   m_skinnedLayout;
};