#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include "imgui_bgfx.h"
#include "ImGuizmo.h"
#include "../world/world.h"
#include "mesh_renderer.h"
#include "grid_renderer.h"
#include "light_renderer.h"

static std::vector<std::string> scanMeshFolder(const std::string& folderPath)
{
    std::vector<std::string> results;
    std::string pattern = folderPath + "\\*.glb";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return results;
    do { results.push_back(fd.cFileName); } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return results;
}

static std::string stripExtension(const std::string& filename)
{
    size_t dot = filename.rfind('.');
    if (dot != std::string::npos) return filename.substr(0, dot);
    return filename;
}

static std::string openSaveDialog()
{
    char buf[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Scene Files\0*.scene\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = "scene";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameA(&ofn)) return std::string(buf);
    return "";
}

static std::string openLoadDialog()
{
    char buf[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Scene Files\0*.scene\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) return std::string(buf);
    return "";
}

static std::string jsonEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

static bool rayAabb(
    const bx::Vec3& ro, const bx::Vec3& rd,
    const bx::Vec3& bmin, const bx::Vec3& bmax,
    float& tHit)
{
    float tmin = 0.0f;
    float tmax = 1e30f;
    for (int i = 0; i < 3; ++i)
    {
        float o  = (&ro.x)[i];
        float d  = (&rd.x)[i];
        float mn = (&bmin.x)[i];
        float mx = (&bmax.x)[i];
        if (bx::abs(d) < 1e-8f) {
            if (o < mn || o > mx) return false;
        } else {
            float invD = 1.0f / d;
            float t1 = (mn - o) * invD;
            float t2 = (mx - o) * invD;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tmin = bx::max(tmin, t1);
            tmax = bx::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    return tHit >= 0.0f;
}

static void buildPickRay(
    float vpX, float vpY,
    float vpW, float vpH,
    const float* viewMtx, const float* projMtx,
    float* outOrigin, float* outDir)
{
    float ndcX =  (vpX / vpW) * 2.0f - 1.0f;
    float ndcY = 1.0f - (vpY / vpH) * 2.0f;
    float m00 = projMtx[0];
    float m11 = projMtx[5];
    float rvx = ndcX / m00;
    float rvy = ndcY / m11;
    float rvz = 1.0f;

    float invView[16];
    invView[0]  = viewMtx[0]; invView[4]  = viewMtx[1]; invView[8]  = viewMtx[2];  invView[12] = 0.0f;
    invView[1]  = viewMtx[4]; invView[5]  = viewMtx[5]; invView[9]  = viewMtx[6];  invView[13] = 0.0f;
    invView[2]  = viewMtx[8]; invView[6]  = viewMtx[9]; invView[10] = viewMtx[10]; invView[14] = 0.0f;
    invView[3]  = 0.0f;       invView[7]  = 0.0f;       invView[11] = 0.0f;        invView[15] = 1.0f;
    invView[12] = -(viewMtx[0]*viewMtx[12] + viewMtx[1]*viewMtx[13] + viewMtx[2]*viewMtx[14]);
    invView[13] = -(viewMtx[4]*viewMtx[12] + viewMtx[5]*viewMtx[13] + viewMtx[6]*viewMtx[14]);
    invView[14] = -(viewMtx[8]*viewMtx[12] + viewMtx[9]*viewMtx[13] + viewMtx[10]*viewMtx[14]);

    float rwx = rvx*invView[0] + rvy*invView[4] + rvz*invView[8];
    float rwy = rvx*invView[1] + rvy*invView[5] + rvz*invView[9];
    float rwz = rvx*invView[2] + rvy*invView[6] + rvz*invView[10];
    float len = bx::sqrt(rwx*rwx + rwy*rwy + rwz*rwz);
    outDir[0] = rwx/len; outDir[1] = rwy/len; outDir[2] = rwz/len;
    outOrigin[0] = invView[12]; outOrigin[1] = invView[13]; outOrigin[2] = invView[14];
}

int main(int argc, char* argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return -1;
    // Normal resizable window with title bar, minimize, maximize, and close buttons.
    SDL_Window* window = SDL_CreateWindow("Orion Engine", 1920, 1080, SDL_WINDOW_RESIZABLE);
    if (!window) return -1;
    SDL_ShowWindow(window);
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    void* nativeWindow = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);

    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.resolution.width = 1920; init.resolution.height = 1080;
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.platformData.nwh = nativeWindow; init.platformData.ndt = nullptr;
    init.debug = true; init.profile = false;
    bgfx::renderFrame();
    if (!bgfx::init(init)) return -1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOther(window);
    if (!imguiBgfxInit(255, nullptr)) return -1;

    MeshRenderer meshRenderer;
    if (!meshRenderer.init()) return -1;
    GridRenderer gridRenderer;
    if (!gridRenderer.init()) return -1;
    LightRenderer lightRenderer;
    if (!lightRenderer.init()) return -1;

    const std::string meshFolder = "E:/GameEngine/assets/meshes";
    Camera cam;

    std::vector<std::vector<MeshGeometry>> meshRegistry;
    std::vector<std::string>               meshRegistryNames;
    std::vector<float>                     meshRegistryScale;
    std::vector<SkeletonData>              skeletonRegistry;

    auto getOrLoadMesh = [&](const std::string& filename) -> uint32_t {
        for (uint32_t i = 0; i < (uint32_t)meshRegistryNames.size(); ++i)
            if (meshRegistryNames[i] == filename) return i;
        std::vector<MeshGeometry> geos = meshRenderer.loadGltf(meshFolder + "/" + filename);
        bx::Vec3 combinedMin = {  1e30f,  1e30f,  1e30f };
        bx::Vec3 combinedMax = { -1e30f, -1e30f, -1e30f };
        for (const MeshGeometry& g : geos) {
            if (g.boundsMin.x < combinedMin.x) combinedMin.x = g.boundsMin.x;
            if (g.boundsMin.y < combinedMin.y) combinedMin.y = g.boundsMin.y;
            if (g.boundsMin.z < combinedMin.z) combinedMin.z = g.boundsMin.z;
            if (g.boundsMax.x > combinedMax.x) combinedMax.x = g.boundsMax.x;
            if (g.boundsMax.y > combinedMax.y) combinedMax.y = g.boundsMax.y;
            if (g.boundsMax.z > combinedMax.z) combinedMax.z = g.boundsMax.z;
        }
        float dx = combinedMax.x - combinedMin.x;
        float dy = combinedMax.y - combinedMin.y;
        float dz = combinedMax.z - combinedMin.z;
        float diagonal = bx::sqrt(dx*dx + dy*dy + dz*dz);
        float autoScale = (diagonal > 2.0f) ? (2.0f / diagonal) : 1.0f;
        meshRegistry.push_back(std::move(geos));
        meshRegistryNames.push_back(filename);
        meshRegistryScale.push_back(autoScale);
        skeletonRegistry.push_back(meshRenderer.loadSkeleton(meshFolder + "/" + filename));
        return (uint32_t)(meshRegistry.size() - 1);
    };

    struct RenderRes { uint16_t w, h; const char* label; };
    static const RenderRes kRenderPresets[] = {
        {  800,  450, "Current (800x450)"  },
        {  960,  540, "540p  (960x540)"    },
        { 1280,  720, "720p  (1280x720)"   },
        { 1600,  900, "900p  (1600x900)"   },
        { 1920, 1080, "1080p (1920x1080)"  },
    };
    int selectedRenderPreset = 4;
    uint16_t kViewportW = kRenderPresets[4].w;
    uint16_t kViewportH = kRenderPresets[4].h;

    auto createViewportFB = [&](uint16_t w, uint16_t h,
                                bgfx::TextureHandle& color,
                                bgfx::TextureHandle& depth,
                                bgfx::FrameBufferHandle& fb)
    {
        if (bgfx::isValid(fb))    bgfx::destroy(fb);
        if (bgfx::isValid(color)) bgfx::destroy(color);
        if (bgfx::isValid(depth)) bgfx::destroy(depth);
        color = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_RT);
        depth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
        bgfx::TextureHandle att[] = { color, depth };
        fb = bgfx::createFrameBuffer(2, att, false);
    };

    bgfx::TextureHandle viewportColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle viewportDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle viewportFB = BGFX_INVALID_HANDLE;
    createViewportFB(kViewportW, kViewportH, viewportColor, viewportDepth, viewportFB);

    // ── Editor meshes (not shown in Asset Browser) ────────────────────────────
    std::vector<MeshGeometry> editorLightGeos =
        meshRenderer.loadGltf("E:/GameEngine/assets/meshes/editor/light bulb.glb");
    float editorLightScale = 1.0f;
    {
        bx::Vec3 cmin = {  1e30f,  1e30f,  1e30f };
        bx::Vec3 cmax = { -1e30f, -1e30f, -1e30f };
        for (const MeshGeometry& g : editorLightGeos) {
            if (g.boundsMin.x < cmin.x) cmin.x = g.boundsMin.x;
            if (g.boundsMin.y < cmin.y) cmin.y = g.boundsMin.y;
            if (g.boundsMin.z < cmin.z) cmin.z = g.boundsMin.z;
            if (g.boundsMax.x > cmax.x) cmax.x = g.boundsMax.x;
            if (g.boundsMax.y > cmax.y) cmax.y = g.boundsMax.y;
            if (g.boundsMax.z > cmax.z) cmax.z = g.boundsMax.z;
        }
        float dx = cmax.x - cmin.x, dy = cmax.y - cmin.y, dz = cmax.z - cmin.z;
        float diag = bx::sqrt(dx*dx + dy*dy + dz*dz);
        if (diag > 0.5f) editorLightScale = 0.5f / diag;
    }

    World world;

    Entity cameraEnt = world.createEntity();
    world.addName(cameraEnt, "Camera");
    world.addTransform(cameraEnt).position = { 0.0f, 0.0f, -5.0f };

    // ── Default 4-light studio rig ────────────────────────────────────────────
    {
        Entity e = world.createEntity();
        world.addName(e, "Key Light");
        world.addTransform(e).position = { -2.0f, 3.0f, -2.0f };
        LightComponent& lc = world.addLight(e);
        lc.direction = { 0.5f, -1.0f, 0.5f };
        lc.color     = { 1.0f,  0.95f, 0.85f };
        lc.intensity = 1.2f;
        lc.enabled   = true;
    }
    {
        Entity e = world.createEntity();
        world.addName(e, "Fill Light");
        world.addTransform(e).position = { 2.5f, 1.5f, -1.0f };
        LightComponent& lc = world.addLight(e);
        lc.direction = { -0.6f, -0.5f, 0.4f };
        lc.color     = { 0.7f,  0.8f,  1.0f };
        lc.intensity = 0.5f;
        lc.enabled   = true;
    }
    {
        Entity e = world.createEntity();
        world.addName(e, "Rim Light");
        world.addTransform(e).position = { 0.0f, 3.0f, 3.0f };
        LightComponent& lc = world.addLight(e);
        lc.direction = { 0.0f, -0.4f, -1.0f };
        lc.color     = { 0.9f,  0.9f,  1.0f };
        lc.intensity = 0.8f;
        lc.enabled   = true;
    }
    {
        Entity e = world.createEntity();
        world.addName(e, "Bottom Fill");
        world.addTransform(e).position = { 0.0f, -2.0f, 0.0f };
        LightComponent& lc = world.addLight(e);
        lc.direction = { 0.0f, 1.0f, 0.0f };
        lc.color     = { 0.6f, 0.6f, 0.7f };
        lc.intensity = 0.2f;
        lc.enabled   = true;
    }

    // Builds a SkeletonComponent from loaded SkeletonData and attaches it to the entity.
    // Creates the RGBA32F GPU bone texture used by the vertex shader.
    auto attachSkeleton = [&](Entity e, uint32_t registryHandle)
    {
        if (registryHandle >= (uint32_t)skeletonRegistry.size()) return;
        const SkeletonData& sd = skeletonRegistry[registryHandle];
        if (!sd.valid()) return;

        SkeletonComponent& sc = world.addSkeleton(e);
        sc.jointCount = sd.jointCount;
        sc.inverseBindMatrices = sd.inverseBindMatrices;
        sc.parentIndex         = sd.parentIndex;
        sc.jointNames          = sd.jointNames;

        sc.localRotation.resize(sd.jointCount);
        sc.localTranslation.resize(sd.jointCount);
        sc.localScale.resize(sd.jointCount);
        for (uint32_t j = 0; j < sd.jointCount; ++j)
        {
            sc.localRotation[j]    = sd.restRotation[j];
            sc.localTranslation[j] = sd.restTranslation[j];
            sc.localScale[j]       = sd.restScale[j];
        }

        sc.skinningMatrices.resize(sd.jointCount);
        static const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        for (uint32_t j = 0; j < sd.jointCount; ++j)
            for (int k = 0; k < 16; ++k)
                sc.skinningMatrices[j][k] = ident[k];

        uint32_t texW = sd.jointCount * 4;
        sc.boneTexture = bgfx::createTexture2D(
            (uint16_t)texW, 1, false, 1,
            bgfx::TextureFormat::RGBA32F,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);

        printf("Skeleton attached to entity %u: %u joints, texture %ux1\n",
               e, sd.jointCount, texW);
    };

    Entity selectedEntity = INVALID_ENTITY;
    ImGuizmo::OPERATION gizmoMode = ImGuizmo::TRANSLATE;

    float camYaw = 0.0f, camPitch = 20.0f, camDistance = 4.0f;
    float camTargetX = 0.0f, camTargetY = 0.0f, camTargetZ = 0.0f;
    bool rightMouseHeld = false, leftMouseHeld = false;
    bool viewportHovered = false, leftDragMoved = false;
    float viewportScreenX = 0.0f, viewportScreenY = 0.0f;
    float viewportDisplayW = 800.0f, viewportDisplayH = 450.0f;

    const uint32_t kDoubleClickMs = 400;

    // ── Save scene ────────────────────────────────────────────────────────────
    auto saveScene = [&](const std::string& path)
    {
        std::ofstream f(path);
        if (!f.is_open()) return;

        f << "{\n";
        f << "  \"camera\": {\n";
        f << "    \"yaw\": "      << camYaw      << ",\n";
        f << "    \"pitch\": "    << camPitch    << ",\n";
        f << "    \"distance\": " << camDistance << ",\n";
        f << "    \"targetX\": "  << camTargetX  << ",\n";
        f << "    \"targetY\": "  << camTargetY  << ",\n";
        f << "    \"targetZ\": "  << camTargetZ  << "\n";
        f << "  },\n";

        f << "  \"entities\": [\n";
        const auto& ents = world.entities();
        for (size_t i = 0; i < ents.size(); ++i)
        {
            Entity e = ents[i];
            NameComponent* nc = world.getName(e);
            TransformComponent* tc = world.getTransform(e);
            MeshComponent* mc = world.getMesh(e);

            f << "    {\n";
            f << "      \"name\": \"" << jsonEscape(nc ? nc->name : "Entity") << "\",\n";

            if (tc) {
                f << "      \"position\": [" << tc->position.x << "," << tc->position.y << "," << tc->position.z << "],\n";
                f << "      \"rotation\": [" << tc->rotation.x << "," << tc->rotation.y << "," << tc->rotation.z << "],\n";
                f << "      \"scale\": ["    << tc->scale.x    << "," << tc->scale.y    << "," << tc->scale.z    << "],\n";
            }

            if (mc && !mc->meshHandles.empty() && mc->meshHandles[0] < (uint32_t)meshRegistryNames.size())
                f << "      \"mesh\": \"" << jsonEscape(meshRegistryNames[mc->meshHandles[0]]) << "\",\n";
            else
                f << "      \"mesh\": \"\",\n";

            LightComponent* lc = world.getLight(e);
            if (lc) {
                f << "      \"lightDir\": ["   << lc->direction.x << "," << lc->direction.y << "," << lc->direction.z << "],\n";
                f << "      \"lightColor\": [" << lc->color.x     << "," << lc->color.y     << "," << lc->color.z     << "],\n";
                f << "      \"lightIntensity\": " << lc->intensity << ",\n";
                f << "      \"lightEnabled\": "   << (lc->enabled ? 1 : 0) << ",\n";
            }

            f << "      \"_end\": 0\n";
            f << "    }";
            if (i + 1 < ents.size()) f << ",";
            f << "\n";
        }
        f << "  ]\n";
        f << "}\n";
    };

    // ── Load scene ────────────────────────────────────────────────────────────
    auto loadScene = [&](const std::string& path)
    {
        std::ifstream f(path);
        if (!f.is_open()) return;
        std::string src((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

        auto findFloat = [&](const std::string& key, float& out) {
            std::string search = "\"" + key + "\": ";
            size_t pos = src.find(search);
            if (pos == std::string::npos) return;
            pos += search.size();
            out = std::stof(src.substr(pos, 32));
        };

        auto findString = [&](const std::string& text, size_t from, std::string& out) -> size_t {
            size_t q1 = text.find('"', from);
            if (q1 == std::string::npos) return std::string::npos;
            size_t q2 = text.find('"', q1 + 1);
            if (q2 == std::string::npos) return std::string::npos;
            out = text.substr(q1 + 1, q2 - q1 - 1);
            return q2 + 1;
        };

        auto findArray3 = [&](const std::string& key, size_t from, float& x, float& y, float& z) -> size_t {
            std::string search = "\"" + key + "\": [";
            size_t pos = src.find(search, from);
            if (pos == std::string::npos) return from;
            pos += search.size();
            size_t end = src.find(']', pos);
            if (end == std::string::npos) return from;
            std::string arr = src.substr(pos, end - pos);
            std::istringstream ss(arr);
            char comma;
            ss >> x >> comma >> y >> comma >> z;
            return end + 1;
        };

        findFloat("yaw",      camYaw);
        findFloat("pitch",    camPitch);
        findFloat("distance", camDistance);
        findFloat("targetX",  camTargetX);
        findFloat("targetY",  camTargetY);
        findFloat("targetZ",  camTargetZ);

        for (auto& geoVec : meshRegistry)
            for (auto& geo : geoVec) {
                if (bgfx::isValid(geo.normalMap)) bgfx::destroy(geo.normalMap);
                if (bgfx::isValid(geo.texture))   bgfx::destroy(geo.texture);
                if (bgfx::isValid(geo.vbh))       bgfx::destroy(geo.vbh);
                if (bgfx::isValid(geo.ibh))       bgfx::destroy(geo.ibh);
            }

        meshRegistry.clear();
        meshRegistryNames.clear();
        meshRegistryScale.clear();

        std::vector<Entity> toDestroy = world.entities();
        for (Entity e : toDestroy) world.destroyEntity(e);
        selectedEntity = INVALID_ENTITY;

        size_t entStart = src.find("\"entities\":");
        if (entStart == std::string::npos) return;
        size_t pos = src.find('[', entStart) + 1;

        while (true)
        {
            size_t objStart = src.find('{', pos);
            if (objStart == std::string::npos) break;
            size_t objEnd = src.find('}', objStart);
            if (objEnd == std::string::npos) break;

            std::string obj = src.substr(objStart + 1, objEnd - objStart - 1);

            std::string name = "Entity";
            std::string meshFile = "";
            {
                size_t npos = obj.find("\"name\":");
                if (npos != std::string::npos) {
                    size_t cur = npos + 7;
                    findString(obj, cur, name);
                }
            }

            float px = 0, py = 0, pz = 0;
            float rx = 0, ry = 0, rz = 0;
            float sx = 1, sy = 1, sz = 1;
            auto parseArr3 = [&](const std::string& key, float& x, float& y, float& z) {
                std::string search = "\"" + key + "\": [";
                size_t p = obj.find(search);
                if (p == std::string::npos) return;
                p += search.size();
                size_t e2 = obj.find(']', p);
                if (e2 == std::string::npos) return;
                std::string arr = obj.substr(p, e2 - p);
                std::istringstream ss(arr);
                char comma;
                ss >> x >> comma >> y >> comma >> z;
            };
            parseArr3("position", px, py, pz);
            parseArr3("rotation", rx, ry, rz);
            parseArr3("scale",    sx, sy, sz);

            {
                size_t mpos = obj.find("\"mesh\":");
                if (mpos != std::string::npos) {
                    size_t cur = mpos + 7;
                    findString(obj, cur, meshFile);
                }
            }

            Entity e = world.createEntity();
            world.addName(e, name);
            TransformComponent& tc = world.addTransform(e);
            tc.position = { px, py, pz };
            tc.rotation = { rx, ry, rz };
            tc.scale    = { sx, sy, sz };

            if (!meshFile.empty()) {
                uint32_t handle = getOrLoadMesh(meshFile);
                world.addMesh(e).meshHandles.push_back(handle);
            }

            {
                float ldx = 0.0f, ldy = -1.0f, ldz = 0.0f;
                float lcr = 1.0f, lcg = 1.0f, lcb = 1.0f;
                float lint = 1.0f;

                size_t ldPos = obj.find("\"lightDir\":");
                if (ldPos != std::string::npos) {
                    parseArr3("lightDir",   ldx, ldy, ldz);
                    parseArr3("lightColor", lcr, lcg, lcb);

                    size_t intPos = obj.find("\"lightIntensity\":");
                    if (intPos != std::string::npos) {
                        size_t valStart = intPos + 17;
                        lint = std::stof(obj.substr(valStart, 32));
                    }

                    bool lenabled = true;
                    size_t enPos = obj.find("\"lightEnabled\":");
                    if (enPos != std::string::npos) {
                        size_t valStart = enPos + 15;
                        while (valStart < obj.size() && (obj[valStart] == ' ' || obj[valStart] == '\n' || obj[valStart] == '\r')) ++valStart;
                        lenabled = (obj[valStart] != '0');
                    }

                    LightComponent& lc = world.addLight(e);
                    lc.direction = { ldx, ldy, ldz };
                    lc.color     = { lcr, lcg, lcb };
                    lc.intensity = lint;
                    lc.enabled   = lenabled;
                }
            }

            pos = objEnd + 1;
            size_t nextBrace  = src.find('{', pos);
            size_t closingArr = src.find(']', pos);
            if (closingArr != std::string::npos &&
                (nextBrace == std::string::npos || closingArr < nextBrace))
                break;
        }
    };

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a2eff, 1.0f, 0);
    bgfx::setViewMode(0, bgfx::ViewMode::Default);
    bgfx::setViewRect(0, 0, 0, kViewportW, kViewportH);
    bgfx::setViewFrameBuffer(0, viewportFB);
    bgfx::setViewRect(255, 0, 0, bgfx::BackbufferRatio::Equal);
    bgfx::setViewMode(255, bgfx::ViewMode::Sequential);

    int windowW = 1920, windowH = 1080;
    float viewMtx[16], projMtx[16];

    bool running = true;
    SDL_Event event;
    while (running)
    {
        {
            float yawRad   = bx::toRad(camYaw);
            float pitchRad = bx::toRad(camPitch);
            cam.target = { camTargetX, camTargetY, camTargetZ };
            cam.eye.x = camTargetX + camDistance * bx::cos(pitchRad) * bx::sin(yawRad);
            cam.eye.y = camTargetY + camDistance * bx::sin(pitchRad);
            cam.eye.z = camTargetZ + camDistance * bx::cos(pitchRad) * bx::cos(yawRad);
        }
        meshRenderer.beginFrame(cam, kViewportW, kViewportH, viewMtx, projMtx);

        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;

            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            {
                windowW = event.window.data1;
                windowH = event.window.data2;
                uint32_t resetW = (uint32_t)bx::max(windowW, (int)kViewportW);
                uint32_t resetH = (uint32_t)bx::max(windowH, (int)kViewportH);
                bgfx::reset(resetW, resetH, BGFX_RESET_VSYNC);
                bgfx::setViewRect(255, 0, 0, (uint16_t)windowW, (uint16_t)windowH);
                bgfx::setViewFrameBuffer(0, viewportFB);
                bgfx::setViewRect(0, 0, 0, kViewportW, kViewportH);
                bgfx::touch(0);
                bgfx::frame();
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
                if (viewportHovered) rightMouseHeld = true;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
                rightMouseHeld = false;

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (viewportHovered && !ImGuizmo::IsOver())
                {
                    float mx = (float)event.button.x;
                    float my = (float)event.button.y;
                    float vpX = mx - viewportScreenX;
                    float vpY = my - viewportScreenY;

                    float rayOrigin[3], rayDir[3];
                    buildPickRay(vpX, vpY, viewportDisplayW, viewportDisplayH,
                                 viewMtx, projMtx, rayOrigin, rayDir);

                    Entity hitEntity = INVALID_ENTITY;
                    float  hitT      = 1e30f;

                    for (Entity e : world.entities())
                    {
                        MeshComponent* mc = world.getMesh(e);
                        if (!mc || mc->meshHandles.empty()) continue;
                        TransformComponent* tc = world.getTransform(e);
                        if (!tc) continue;

                        SkeletonComponent* pickSc = world.getSkeleton(e);
                        bool isSkinned = (pickSc && pickSc->valid());

                        if (isSkinned)
                        {
                            // Skinned meshes: world-space sphere test centred on
                            // the entity position. Radius is max(scale) * 0.6 which
                            // tightly matches a human-proportioned character's body
                            // width without creating a large dead zone around them.
                            float maxScale = bx::max(bx::max(tc->scale.x, tc->scale.y), tc->scale.z);
                            float radius   = maxScale * 0.6f;

                            float ocx = tc->position.x - rayOrigin[0];
                            float ocy = tc->position.y - rayOrigin[1];
                            float ocz = tc->position.z - rayOrigin[2];
                            float tca = ocx*rayDir[0] + ocy*rayDir[1] + ocz*rayDir[2];
                            if (tca < 0.0f) continue;
                            float d2 = (ocx*ocx + ocy*ocy + ocz*ocz) - tca*tca;
                            if (d2 > radius*radius) continue;

                            float distToEnt = bx::sqrt(ocx*ocx + ocy*ocy + ocz*ocz);
                            if (distToEnt < hitT) { hitT = distToEnt; hitEntity = e; }
                        }
                        else
                        {
                            // Static meshes: standard ray-AABB test in local space.
                            float trans[16], rotX[16], rotY[16], rotZ[16], rot[16], scl[16], model[16];
                            bx::mtxTranslate(trans, tc->position.x, tc->position.y, tc->position.z);
                            bx::mtxRotateX(rotX, bx::toRad(tc->rotation.x));
                            bx::mtxRotateY(rotY, bx::toRad(tc->rotation.y));
                            bx::mtxRotateZ(rotZ, bx::toRad(tc->rotation.z));
                            bx::mtxMul(rot, rotX, rotY);
                            bx::mtxMul(rot, rot, rotZ);
                            bx::mtxScale(scl, tc->scale.x, tc->scale.y, tc->scale.z);
                            bx::mtxMul(model, scl, rot);
                            bx::mtxMul(model, model, trans);

                            for (uint32_t rh : mc->meshHandles)
                            {
                                if (rh >= (uint32_t)meshRegistry.size()) continue;
                                float invModel[16];
                                bx::mtxInverse(invModel, model);
                                for (const MeshGeometry& geo : meshRegistry[rh])
                                {
                                    float ro4[4] = {
                                        invModel[0]*rayOrigin[0] + invModel[4]*rayOrigin[1] + invModel[8]*rayOrigin[2]  + invModel[12],
                                        invModel[1]*rayOrigin[0] + invModel[5]*rayOrigin[1] + invModel[9]*rayOrigin[2]  + invModel[13],
                                        invModel[2]*rayOrigin[0] + invModel[6]*rayOrigin[1] + invModel[10]*rayOrigin[2] + invModel[14],
                                        invModel[3]*rayOrigin[0] + invModel[7]*rayOrigin[1] + invModel[11]*rayOrigin[2] + invModel[15]
                                    };
                                    bx::Vec3 roLocal = { ro4[0]/ro4[3], ro4[1]/ro4[3], ro4[2]/ro4[3] };
                                    bx::Vec3 rdLocal = {
                                        invModel[0]*rayDir[0] + invModel[4]*rayDir[1] + invModel[8]*rayDir[2],
                                        invModel[1]*rayDir[0] + invModel[5]*rayDir[1] + invModel[9]*rayDir[2],
                                        invModel[2]*rayDir[0] + invModel[6]*rayDir[1] + invModel[10]*rayDir[2]
                                    };
                                    float tHit = 0.0f;
                                    if (rayAabb(roLocal, rdLocal, geo.boundsMin, geo.boundsMax, tHit))
                                    {
                                        float hitLocalX = roLocal.x + rdLocal.x * tHit;
                                        float hitLocalY = roLocal.y + rdLocal.y * tHit;
                                        float hitLocalZ = roLocal.z + rdLocal.z * tHit;
                                        float hw[4] = {
                                            model[0]*hitLocalX + model[4]*hitLocalY + model[8]*hitLocalZ  + model[12],
                                            model[1]*hitLocalX + model[5]*hitLocalY + model[9]*hitLocalZ  + model[13],
                                            model[2]*hitLocalX + model[6]*hitLocalY + model[10]*hitLocalZ + model[14],
                                            model[3]*hitLocalX + model[7]*hitLocalY + model[11]*hitLocalZ + model[15]
                                        };
                                        float wx = hw[0]/hw[3] - cam.eye.x;
                                        float wy = hw[1]/hw[3] - cam.eye.y;
                                        float wz = hw[2]/hw[3] - cam.eye.z;
                                        float worldDist = bx::sqrt(wx*wx + wy*wy + wz*wz);
                                        if (worldDist < hitT) { hitT = worldDist; hitEntity = e; }
                                    }
                                }
                            }
                        }
                    }

                    // Also test light entities using their bulb geometry.
                    if (!editorLightGeos.empty())
                    {
                        for (Entity e : world.entities())
                        {
                            LightComponent* lc = world.getLight(e);
                            if (!lc) continue;
                            TransformComponent* tc = world.getTransform(e);
                            if (!tc) continue;

                            bx::Vec3 localZ2 = bx::normalize(lc->direction);
                            bx::Vec3 wup2 = { 0.0f, 1.0f, 0.0f };
                            if (bx::abs(bx::dot(localZ2, wup2)) > 0.99f) wup2 = { 1.0f, 0.0f, 0.0f };
                            bx::Vec3 localX2 = bx::normalize(bx::cross(wup2, localZ2));
                            bx::Vec3 localY2 = bx::cross(localZ2, localX2);
                            float s2 = editorLightScale;
                            float pickModel[16];
                            pickModel[ 0] = localX2.x*s2; pickModel[ 1] = localX2.y*s2; pickModel[ 2] = localX2.z*s2; pickModel[ 3] = 0.0f;
                            pickModel[ 4] = localY2.x*s2; pickModel[ 5] = localY2.y*s2; pickModel[ 6] = localY2.z*s2; pickModel[ 7] = 0.0f;
                            pickModel[ 8] = localZ2.x*s2; pickModel[ 9] = localZ2.y*s2; pickModel[10] = localZ2.z*s2; pickModel[11] = 0.0f;
                            pickModel[12] = tc->position.x; pickModel[13] = tc->position.y; pickModel[14] = tc->position.z; pickModel[15] = 1.0f;

                            float invPick[16];
                            bx::mtxInverse(invPick, pickModel);

                            for (const MeshGeometry& geo : editorLightGeos)
                            {
                                float ro4[4] = {
                                    invPick[0]*rayOrigin[0] + invPick[4]*rayOrigin[1] + invPick[8]*rayOrigin[2]  + invPick[12],
                                    invPick[1]*rayOrigin[0] + invPick[5]*rayOrigin[1] + invPick[9]*rayOrigin[2]  + invPick[13],
                                    invPick[2]*rayOrigin[0] + invPick[6]*rayOrigin[1] + invPick[10]*rayOrigin[2] + invPick[14],
                                    invPick[3]*rayOrigin[0] + invPick[7]*rayOrigin[1] + invPick[11]*rayOrigin[2] + invPick[15]
                                };
                                bx::Vec3 roL = { ro4[0]/ro4[3], ro4[1]/ro4[3], ro4[2]/ro4[3] };
                                bx::Vec3 rdL = {
                                    invPick[0]*rayDir[0] + invPick[4]*rayDir[1] + invPick[8]*rayDir[2],
                                    invPick[1]*rayDir[0] + invPick[5]*rayDir[1] + invPick[9]*rayDir[2],
                                    invPick[2]*rayDir[0] + invPick[6]*rayDir[1] + invPick[10]*rayDir[2]
                                };
                                float tHit = 0.0f;
                                if (rayAabb(roL, rdL, geo.boundsMin, geo.boundsMax, tHit))
                                {
                                    float hx = tc->position.x - cam.eye.x;
                                    float hy = tc->position.y - cam.eye.y;
                                    float hz = tc->position.z - cam.eye.z;
                                    float dist = bx::sqrt(hx*hx + hy*hy + hz*hz);
                                    if (dist < hitT) { hitT = dist; hitEntity = e; }
                                }
                            }
                        }
                    }

                    selectedEntity = hitEntity;
                    leftMouseHeld  = true;
                    leftDragMoved  = false;
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                leftMouseHeld = false;
                leftDragMoved = false;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                float dx = event.motion.xrel, dy = event.motion.yrel;
                if (leftMouseHeld) leftDragMoved = true;

                if (rightMouseHeld)
                {
                    camYaw   += dx * 0.4f;
                    camPitch += dy * 0.4f;
                    if (camPitch >  89.0f) camPitch =  89.0f;
                    if (camPitch < -89.0f) camPitch = -89.0f;
                }

                if (leftMouseHeld)
                {
                    float camRightX = viewMtx[0], camRightY = viewMtx[4], camRightZ = viewMtx[8];
                    float camUpX    = viewMtx[1], camUpY    = viewMtx[5], camUpZ    = viewMtx[9];

                    if (selectedEntity != INVALID_ENTITY && (world.getMesh(selectedEntity) || world.getLight(selectedEntity)))
                    {
                        TransformComponent* tc = world.getTransform(selectedEntity);
                        if (tc)
                        {
                            float moveSpeed = camDistance * 0.003f;
                            tc->position.x += (camRightX * dx - camUpX * dy) * moveSpeed;
                            tc->position.y += (camRightY * dx - camUpY * dy) * moveSpeed;
                            tc->position.z += (camRightZ * dx - camUpZ * dy) * moveSpeed;
                        }
                    }
                    else
                    {
                        float panSpeed = camDistance * 0.002f;
                        camTargetX -= (camRightX * dx - camUpX * dy) * panSpeed;
                        camTargetY -= (camRightY * dx - camUpY * dy) * panSpeed;
                        camTargetZ -= (camRightZ * dx - camUpZ * dy) * panSpeed;
                    }
                }
            }

            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_DELETE && selectedEntity != INVALID_ENTITY)
                {
                    NameComponent* nc = world.getName(selectedEntity);
                    bool isProtected = nc && (nc->name == "Camera");
                    if (!isProtected)
                    {
                        world.destroyEntity(selectedEntity);
                        selectedEntity = INVALID_ENTITY;
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL && viewportHovered)
            {
                float scrollSpeed = camDistance * 0.1f;
                if (scrollSpeed < 0.01f) scrollSpeed = 0.01f;
                camDistance -= event.wheel.y * scrollSpeed;
                if (camDistance < 0.01f) camDistance = 0.01f;
            }
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGuiWindowFlags dockFlags =
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("DockSpace", nullptr, dockFlags);
        ImGui::PopStyleVar();
        ImGui::DockSpace(ImGui::GetID("MainDockSpace"));
        ImGui::End();

        // ── File panel ───────────────────────────────────────────────────────
        ImGui::Begin("File");
        if (ImGui::Button("Save Scene", ImVec2(-1, 0)))
        {
            std::string path = openSaveDialog();
            if (!path.empty()) saveScene(path);
        }
        ImGui::Spacing();
        if (ImGui::Button("Load Scene", ImVec2(-1, 0)))
        {
            std::string path = openLoadDialog();
            if (!path.empty()) loadScene(path);
        }
        ImGui::End();

        // ── Scene Hierarchy ──────────────────────────────────────────────────
        ImGui::Begin("Scene Hierarchy");
        for (Entity e : world.entities()) {
            NameComponent* nc = world.getName(e);
            const char* label = nc ? nc->name.c_str() : "Unnamed";
            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            if (e == selectedEntity) flags |= ImGuiTreeNodeFlags_Selected;
            ImGui::TreeNodeEx((void*)(uintptr_t)e, flags, "%s", label);
            if (ImGui::IsItemClicked()) selectedEntity = e;
        }
        ImGui::End();

        // ── Properties ───────────────────────────────────────────────────────
        ImGui::Begin("Properties");
        if (selectedEntity != INVALID_ENTITY) {
            NameComponent* nc = world.getName(selectedEntity);
            if (nc) {
                char buf[128];
                strncpy_s(buf, nc->name.c_str(), sizeof(buf) - 1);
                if (ImGui::InputText("Name", buf, sizeof(buf))) nc->name = buf;
            }
            TransformComponent* tc = world.getTransform(selectedEntity);
            if (tc) {
                ImGui::SeparatorText("Transform");
                ImGui::DragFloat3("Position", &tc->position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &tc->rotation.x, 0.5f);
                ImGui::DragFloat3("Scale",    &tc->scale.x,    0.1f);
            }
            MeshComponent* mc = world.getMesh(selectedEntity);
            if (mc && !mc->meshHandles.empty()) {
                ImGui::SeparatorText("Mesh");
                uint32_t rh = mc->meshHandles[0];
                if (rh < (uint32_t)meshRegistryNames.size())
                    ImGui::Text("Source: %s (%zu primitives)",
                        meshRegistryNames[rh].c_str(),
                        meshRegistry[rh].size());
                else
                    ImGui::Text("No mesh assigned");
            }
        } else {
            ImGui::Text("Select an entity");
        }
        if (selectedEntity != INVALID_ENTITY) {
            NameComponent* nc = world.getName(selectedEntity);
            bool isProtected = nc && (nc->name == "Camera");
            if (!isProtected) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
                    world.destroyEntity(selectedEntity);
                    selectedEntity = INVALID_ENTITY;
                }
                ImGui::PopStyleColor(3);
            }
        }
        ImGui::End();

        // ── Resolution ───────────────────────────────────────────────────────
        ImGui::Begin("Resolution");
        ImGui::Text("Viewport Render Quality");
        ImGui::Separator();
        ImGui::Spacing();
        for (int i = 0; i < 5; i++) {
            bool isSelected = (i == selectedRenderPreset);
            if (isSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            if (ImGui::Selectable(kRenderPresets[i].label, isSelected)) {
                if (i != selectedRenderPreset) {
                    selectedRenderPreset = i;
                    kViewportW = kRenderPresets[i].w;
                    kViewportH = kRenderPresets[i].h;
                    createViewportFB(kViewportW, kViewportH, viewportColor, viewportDepth, viewportFB);
                    uint32_t resetW = (uint32_t)bx::max(windowW, (int)kViewportW);
                    uint32_t resetH = (uint32_t)bx::max(windowH, (int)kViewportH);
                    bgfx::reset(resetW, resetH, BGFX_RESET_VSYNC);
                    bgfx::setViewRect(255, 0, 0, (uint16_t)windowW, (uint16_t)windowH);
                    bgfx::setViewRect(0, 0, 0, kViewportW, kViewportH);
                    bgfx::setViewFrameBuffer(0, viewportFB);
                    bgfx::touch(0);
                    bgfx::frame();
                }
            }
            if (isSelected) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::Text("Window: %dx%d", windowW, windowH);
        ImGui::Text("Render: %dx%d", kViewportW, kViewportH);
        ImGui::End();

        // ── Lighting Panel ────────────────────────────────────────────────────
        ImGui::Begin("Lighting");
        for (Entity e : world.entities())
        {
            LightComponent* lc = world.getLight(e);
            if (!lc) continue;
            NameComponent* nc2 = world.getName(e);

            ImGui::PushID((int)e);
            if (e == selectedEntity)
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f,0.45f,0.7f,1.0f));

            bool open = ImGui::CollapsingHeader(nc2 ? nc2->name.c_str() : "Light",
                                                ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::IsItemClicked()) selectedEntity = e;

            if (e == selectedEntity) ImGui::PopStyleColor();

            if (open)
            {
                if (nc2)
                {
                    char buf[128];
                    strncpy_s(buf, nc2->name.c_str(), sizeof(buf)-1);
                    if (ImGui::InputText("Name##l", buf, sizeof(buf))) nc2->name = buf;
                }
                ImGui::Checkbox("Enabled", &lc->enabled);
                ImGui::ColorEdit3("Color",     &lc->color.x);
                ImGui::DragFloat("Intensity",  &lc->intensity, 0.05f, 0.0f, 5.0f);
                ImGui::DragFloat3("Direction", &lc->direction.x, 0.01f);

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f,0.1f,0.1f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f,0.2f,0.2f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f,0.3f,0.3f,1.0f));
                if (ImGui::Button("Delete##l", ImVec2(-1,0)))
                {
                    world.destroyEntity(e);
                    if (selectedEntity == e) selectedEntity = INVALID_ENTITY;
                    ImGui::PopStyleColor(3);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopStyleColor(3);
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (ImGui::Button("Add Light", ImVec2(-1,0)))
        {
            int lightCount = 0;
            for (Entity e : world.entities()) if (world.getLight(e)) ++lightCount;
            if (lightCount < 4)
            {
                Entity e = world.createEntity();
                world.addName(e, "Light");
                world.addTransform(e).position = { 0.0f, 2.0f, 0.0f };
                world.addLight(e);
                selectedEntity = e;
            }
        }
        ImGui::End();

        // ── Skeleton Panel ────────────────────────────────────────────────────
        ImGui::Begin("Skeleton");
        {
            SkeletonComponent* panelSc = (selectedEntity != INVALID_ENTITY)
                                       ? world.getSkeleton(selectedEntity) : nullptr;
            if (!panelSc || !panelSc->valid())
            {
                ImGui::TextDisabled("Select a skinned entity to pose it.");
            }
            else
            {
                ImGui::Text("%u joints", panelSc->jointCount);
                ImGui::Separator();

                if (ImGui::Button("Reset Pose", ImVec2(-1, 0)))
                {
                    MeshComponent* mc = world.getMesh(selectedEntity);
                    if (mc && !mc->meshHandles.empty())
                    {
                        uint32_t rh = mc->meshHandles[0];
                        if (rh < (uint32_t)skeletonRegistry.size())
                        {
                            const SkeletonData& sd = skeletonRegistry[rh];
                            for (uint32_t j = 0; j < panelSc->jointCount; ++j)
                            {
                                panelSc->localRotation[j]    = sd.restRotation[j];
                                panelSc->localTranslation[j] = sd.restTranslation[j];
                                panelSc->localScale[j]       = sd.restScale[j];
                            }
                        }
                    }
                    panelSc->selectedBone = -1;
                }

                ImGui::Spacing();

                ImGui::BeginChild("BoneList", ImVec2(0, 0), false);
                for (uint32_t j = 0; j < panelSc->jointCount; ++j)
                {
                    bool isSelected = (panelSc->selectedBone == (int)j);
                    ImGui::PushID((int)j);
                    if (isSelected)
                        ImGui::PushStyleColor(ImGuiCol_Header,
                                              ImVec4(0.25f, 0.45f, 0.7f, 1.0f));

                    int depth = 0;
                    int par = panelSc->parentIndex[j];
                    while (par >= 0 && depth < 20)
                    {
                        ++depth;
                        par = panelSc->parentIndex[par];
                    }
                    ImGui::Indent((float)(depth * 10));

                    if (ImGui::Selectable(panelSc->jointNames[j].c_str(), isSelected))
                        panelSc->selectedBone = isSelected ? -1 : (int)j;

                    ImGui::Unindent((float)(depth * 10));

                    if (isSelected) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();

        // ── Asset Browser ─────────────────────────────────────────────────────
        ImGui::Begin("Asset Browser");
        ImGui::Text("Meshes");
        ImGui::Separator();
        std::vector<std::string> meshFiles = scanMeshFolder(meshFolder);
        static std::string assetLastClickedName = "";
        static uint32_t    assetLastClickTime   = 0;
        for (const std::string& filename : meshFiles) {
            bool isLoaded = false;
            for (const auto& rn : meshRegistryNames)
                if (rn == filename) { isLoaded = true; break; }
            if (isLoaded) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
            bool clicked = ImGui::Selectable(filename.c_str(), false);
            if (isLoaded) ImGui::PopStyleColor();
            if (clicked) {
                uint32_t now = SDL_GetTicks();
                bool isDoubleClick = (assetLastClickedName == filename) &&
                                     (now - assetLastClickTime < kDoubleClickMs);
                if (isDoubleClick) {
                    uint32_t handle = getOrLoadMesh(filename);
                    uint32_t meshCount = 0;
                    for (Entity ex : world.entities())
                        if (world.getMesh(ex)) meshCount++;
                    Entity newEnt = world.createEntity();
                    world.addName(newEnt, stripExtension(filename));
                    TransformComponent& tc = world.addTransform(newEnt);
                    float spawnScale = (handle < (uint32_t)meshRegistryScale.size())
                                       ? meshRegistryScale[handle] : 1.0f;
                    tc.position = { (float)meshCount * 1.5f, 0.0f, 0.0f };
                    tc.rotation = { 0.0f, 0.0f, 0.0f };
                    tc.scale    = { spawnScale, spawnScale, spawnScale };
                    MeshComponent& mc2 = world.addMesh(newEnt);
                    mc2.meshHandles.push_back(handle);
                    attachSkeleton(newEnt, handle);
                    selectedEntity = newEnt;
                    assetLastClickTime = 0;
                } else {
                    assetLastClickedName = filename;
                    assetLastClickTime   = now;
                }
            }
        }
        ImGui::End();

        // ── Controls Panel ────────────────────────────────────────────────────
        ImGui::Begin("Controls");
        ImGui::SeparatorText("Viewport Navigation");
        ImGui::BulletText("Right-click + drag        Rotate camera");
        ImGui::BulletText("Scroll wheel              Zoom in / out");
        ImGui::BulletText("Left-drag (no selection)  Pan camera");
        ImGui::Spacing();
        ImGui::SeparatorText("Selection");
        ImGui::BulletText("Double-click asset    Spawn model into scene");
        ImGui::BulletText("Left-click model      Select entity");
        ImGui::BulletText("Click in Hierarchy    Select entity");
        ImGui::BulletText("Delete key or button  Delete selected entity");
        ImGui::Spacing();
        ImGui::SeparatorText("Moving Things");
        ImGui::BulletText("Left-drag on model    Move it in screen space");
        ImGui::BulletText("G key                 Switch to Move gizmo");
        ImGui::BulletText("R key                 Switch to Rotate gizmo");
        ImGui::BulletText("S key                 Switch to Scale gizmo");
        ImGui::BulletText("Drag gizmo arrows     Precise move / rotate / scale");
        ImGui::Spacing();
        ImGui::SeparatorText("Skeleton Posing");
        ImGui::BulletText("Select skinned model  Shows bones in Skeleton panel");
        ImGui::BulletText("Click a bone name     Select that bone");
        ImGui::BulletText("Drag bone gizmo       Rotate that bone");
        ImGui::BulletText("Reset Pose button     Returns all bones to rest pose");
        ImGui::Spacing();
        ImGui::SeparatorText("Scene");
        ImGui::BulletText("File > Save Scene     Save everything to a .scene file");
        ImGui::BulletText("File > Load Scene     Load a previously saved scene");
        ImGui::End();

        // ── Viewport ──────────────────────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport");
        ImGui::PopStyleVar();
        viewportHovered = ImGui::IsWindowHovered();
        ImVec2 vpSize = ImGui::GetContentRegionAvail();
        ImGui::Image((ImTextureID)(uintptr_t)viewportColor.idx, vpSize, ImVec2(0, 0), ImVec2(1, 1));
        ImVec2 vpMin = ImGui::GetItemRectMin();
        ImVec2 vpMax = ImGui::GetItemRectMax();
        viewportScreenX  = vpMin.x;
        viewportScreenY  = vpMin.y;
        viewportDisplayW = vpMax.x - vpMin.x;
        viewportDisplayH = vpMax.y - vpMin.y;

        MeshComponent*  gizmoMc = world.getMesh(selectedEntity);
        LightComponent* gizmoLc = world.getLight(selectedEntity);
        if (selectedEntity != INVALID_ENTITY && ((gizmoMc && !gizmoMc->meshHandles.empty()) || gizmoLc)) {
            TransformComponent* tc = world.getTransform(selectedEntity);
            if (tc) {
                float gizmoMatrix[16];
                if (gizmoLc) {
                    bx::Vec3 lz = bx::normalize(gizmoLc->direction);
                    bx::Vec3 wup = { 0.0f, 1.0f, 0.0f };
                    if (bx::abs(bx::dot(lz, wup)) > 0.99f) wup = { 1.0f, 0.0f, 0.0f };
                    bx::Vec3 lx = bx::normalize(bx::cross(wup, lz));
                    bx::Vec3 ly = bx::cross(lz, lx);
                    gizmoMatrix[ 0]=lx.x; gizmoMatrix[ 1]=lx.y; gizmoMatrix[ 2]=lx.z; gizmoMatrix[ 3]=0;
                    gizmoMatrix[ 4]=ly.x; gizmoMatrix[ 5]=ly.y; gizmoMatrix[ 6]=ly.z; gizmoMatrix[ 7]=0;
                    gizmoMatrix[ 8]=lz.x; gizmoMatrix[ 9]=lz.y; gizmoMatrix[10]=lz.z; gizmoMatrix[11]=0;
                    gizmoMatrix[12]=tc->position.x; gizmoMatrix[13]=tc->position.y;
                    gizmoMatrix[14]=tc->position.z; gizmoMatrix[15]=1;
                } else {
                    float trans[16], rotX[16], rotY[16], rotZ[16], rot[16], scl[16];
                    bx::mtxTranslate(trans, tc->position.x, tc->position.y, tc->position.z);
                    bx::mtxRotateX(rotX, bx::toRad(tc->rotation.x));
                    bx::mtxRotateY(rotY, bx::toRad(tc->rotation.y));
                    bx::mtxRotateZ(rotZ, bx::toRad(tc->rotation.z));
                    bx::mtxMul(rot, rotX, rotY);
                    bx::mtxMul(rot, rot, rotZ);
                    bx::mtxScale(scl, tc->scale.x, tc->scale.y, tc->scale.z);
                    bx::mtxMul(gizmoMatrix, scl, rot);
                    bx::mtxMul(gizmoMatrix, gizmoMatrix, trans);
                }

                ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                ImGuizmo::SetRect(viewportScreenX, viewportScreenY, (float)kViewportW, (float)kViewportH);

                if (ImGui::IsKeyPressed(ImGuiKey_G)) gizmoMode = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoMode = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_S)) gizmoMode = ImGuizmo::SCALE;

                ImDrawList* dl = ImGui::GetWindowDrawList();
                const char* modeStr =
                    (gizmoMode == ImGuizmo::TRANSLATE) ? "G: Translate" :
                    (gizmoMode == ImGuizmo::ROTATE)    ? "R: Rotate"    :
                                                         "S: Scale";
                dl->AddText(ImVec2(viewportScreenX + 10, viewportScreenY + 10),
                            IM_COL32(255, 255, 100, 220), modeStr);

                if (ImGuizmo::Manipulate(viewMtx, projMtx, gizmoMode, ImGuizmo::WORLD, gizmoMatrix)) {
                    float newTrans[3], newRot[3], newScale[3];
                    ImGuizmo::DecomposeMatrixToComponents(gizmoMatrix, newTrans, newRot, newScale);
                    tc->position.x = newTrans[0]; tc->position.y = newTrans[1]; tc->position.z = newTrans[2];
                    if (gizmoLc) {
                        gizmoLc->direction.x = gizmoMatrix[8];
                        gizmoLc->direction.y = gizmoMatrix[9];
                        gizmoLc->direction.z = gizmoMatrix[10];
                    } else {
                        tc->rotation.x = newRot[0]; tc->rotation.y = newRot[1]; tc->rotation.z = newRot[2];
                        tc->scale.x = newScale[0];  tc->scale.y = newScale[1];  tc->scale.z = newScale[2];
                    }
                }
            }
        }

        // ── Bone gizmo ────────────────────────────────────────────────────────
        {
            SkeletonComponent* boneSc = (selectedEntity != INVALID_ENTITY)
                                       ? world.getSkeleton(selectedEntity) : nullptr;
            if (boneSc && boneSc->valid() && boneSc->selectedBone >= 0)
            {
                int bi = boneSc->selectedBone;
                TransformComponent* entTc = world.getTransform(selectedEntity);

                float entModel[16];
                if (entTc)
                {
                    float tr[16], rx[16], ry[16], rz[16], rot[16], sc2[16];
                    bx::mtxTranslate(tr, entTc->position.x, entTc->position.y, entTc->position.z);
                    bx::mtxRotateX(rx, bx::toRad(entTc->rotation.x));
                    bx::mtxRotateY(ry, bx::toRad(entTc->rotation.y));
                    bx::mtxRotateZ(rz, bx::toRad(entTc->rotation.z));
                    bx::mtxMul(rot, rx, ry); bx::mtxMul(rot, rot, rz);
                    bx::mtxScale(sc2, entTc->scale.x, entTc->scale.y, entTc->scale.z);
                    bx::mtxMul(entModel, sc2, rot);
                    bx::mtxMul(entModel, entModel, tr);
                }
                else { bx::mtxIdentity(entModel); }

                const float* sm = boneSc->skinningMatrices[bi].data();

                float bonePos[4] = {
                    entModel[0]*sm[12] + entModel[4]*sm[13] + entModel[ 8]*sm[14] + entModel[12],
                    entModel[1]*sm[12] + entModel[5]*sm[13] + entModel[ 9]*sm[14] + entModel[13],
                    entModel[2]*sm[12] + entModel[6]*sm[13] + entModel[10]*sm[14] + entModel[14],
                    1.0f
                };

                float boneGizmo[16];
                bx::mtxIdentity(boneGizmo);
                boneGizmo[12] = bonePos[0];
                boneGizmo[13] = bonePos[1];
                boneGizmo[14] = bonePos[2];

                ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                ImGuizmo::SetRect(viewportScreenX, viewportScreenY,
                                  (float)kViewportW, (float)kViewportH);

                if (ImGuizmo::Manipulate(viewMtx, projMtx,
                                         ImGuizmo::ROTATE, ImGuizmo::LOCAL,
                                         boneGizmo))
                {
                    float dTrans[3], dRot[3], dScale[3];
                    ImGuizmo::DecomposeMatrixToComponents(boneGizmo, dTrans, dRot, dScale);

                    float rx2 = bx::toRad(dRot[0]) * 0.5f;
                    float ry2 = bx::toRad(dRot[1]) * 0.5f;
                    float rz2 = bx::toRad(dRot[2]) * 0.5f;
                    float qx = bx::sin(rx2)*bx::cos(ry2)*bx::cos(rz2) - bx::cos(rx2)*bx::sin(ry2)*bx::sin(rz2);
                    float qy = bx::cos(rx2)*bx::sin(ry2)*bx::cos(rz2) + bx::sin(rx2)*bx::cos(ry2)*bx::sin(rz2);
                    float qz = bx::cos(rx2)*bx::cos(ry2)*bx::sin(rz2) - bx::sin(rx2)*bx::sin(ry2)*bx::cos(rz2);
                    float qw = bx::cos(rx2)*bx::cos(ry2)*bx::cos(rz2) + bx::sin(rx2)*bx::sin(ry2)*bx::sin(rz2);

                    auto& cur = boneSc->localRotation[bi];
                    float cx=cur[0], cy=cur[1], cz=cur[2], cw=cur[3];
                    cur[0] = cw*qx + cx*qw + cy*qz - cz*qy;
                    cur[1] = cw*qy - cx*qz + cy*qw + cz*qx;
                    cur[2] = cw*qz + cx*qy - cy*qx + cz*qw;
                    cur[3] = cw*qw - cx*qx - cy*qy - cz*qz;
                    float len = bx::sqrt(cur[0]*cur[0]+cur[1]*cur[1]+cur[2]*cur[2]+cur[3]*cur[3]);
                    if (len > 1e-6f) { cur[0]/=len; cur[1]/=len; cur[2]/=len; cur[3]/=len; }
                }
            }
        }

        ImGui::End();

        bgfx::touch(0);
        gridRenderer.draw(viewMtx, projMtx);

        // ── Upload light uniforms ─────────────────────────────────────────────
        {
            float lightDirData[4][4]   = {};
            float lightColorData[4][4] = {};
            int slot = 0;
            for (Entity e : world.entities())
            {
                LightComponent* lc = world.getLight(e);
                if (!lc || slot >= 4) continue;

                lightDirData[slot][0] = lc->direction.x;
                lightDirData[slot][1] = lc->direction.y;
                lightDirData[slot][2] = lc->direction.z;
                lightDirData[slot][3] = lc->intensity;

                lightColorData[slot][0] = lc->color.x;
                lightColorData[slot][1] = lc->color.y;
                lightColorData[slot][2] = lc->color.z;
                lightColorData[slot][3] = lc->enabled ? 1.0f : 0.0f;

                ++slot;
            }
            bgfx::setUniform(meshRenderer.getLightDirUniform(),   lightDirData,   4);
            bgfx::setUniform(meshRenderer.getLightColorUniform(), lightColorData, 4);
        }

        // ── Draw light bulb mesh for each light entity ────────────────────────
        if (!editorLightGeos.empty())
        {
            for (Entity e : world.entities())
            {
                LightComponent* lc = world.getLight(e);
                if (!lc) continue;
                TransformComponent* tc = world.getTransform(e);
                if (!tc) continue;

                bx::Vec3 emitDir = bx::normalize(lc->direction);
                bx::Vec3 worldUp = { 0.0f, 1.0f, 0.0f };
                if (bx::abs(bx::dot(emitDir, worldUp)) > 0.99f)
                    worldUp = { 1.0f, 0.0f, 0.0f };
                bx::Vec3 meshY = bx::normalize(bx::cross(emitDir, worldUp));
                bx::Vec3 meshZ = bx::cross(emitDir, meshY);

                float s = editorLightScale;
                float model[16];
                model[ 0] = emitDir.x*s; model[ 1] = emitDir.y*s; model[ 2] = emitDir.z*s; model[ 3] = 0.0f;
                model[ 4] = meshY.x  *s; model[ 5] = meshY.y  *s; model[ 6] = meshY.z  *s; model[ 7] = 0.0f;
                model[ 8] = meshZ.x  *s; model[ 9] = meshZ.y  *s; model[10] = meshZ.z  *s; model[11] = 0.0f;
                model[12] = tc->position.x; model[13] = tc->position.y; model[14] = tc->position.z; model[15] = 1.0f;

                for (const MeshGeometry& geo : editorLightGeos)
                    meshRenderer.drawMesh(geo, model, geo.hasAlpha);
            }
        }

        // ── Draw all mesh entities ────────────────────────────────────────────
        // Skinned meshes always receive an identity model matrix because their
        // entity transform is already baked into the bone texture every frame.
        // Static meshes receive the normal TRS model matrix.
        auto drawEntities = [&](bool transparent)
        {
            for (Entity e : world.entities()) {
                MeshComponent* mc = world.getMesh(e);
                if (!mc || mc->meshHandles.empty()) continue;
                TransformComponent* tc = world.getTransform(e);
                if (!tc) continue;
                float trans[16], rotX[16], rotY[16], rotZ[16], rot[16], scale[16], model[16];
                bx::mtxTranslate(trans, tc->position.x, tc->position.y, tc->position.z);
                bx::mtxRotateX(rotX, bx::toRad(tc->rotation.x));
                bx::mtxRotateY(rotY, bx::toRad(tc->rotation.y));
                bx::mtxRotateZ(rotZ, bx::toRad(tc->rotation.z));
                bx::mtxMul(rot, rotX, rotY);
                bx::mtxMul(rot, rot, rotZ);
                bx::mtxScale(scale, tc->scale.x, tc->scale.y, tc->scale.z);
                bx::mtxMul(model, scale, rot);
                bx::mtxMul(model, model, trans);
                for (uint32_t rh : mc->meshHandles)
                {
                    if (rh >= (uint32_t)meshRegistry.size()) continue;
                    SkeletonComponent* drawSc = world.getSkeleton(e);
                    for (const MeshGeometry& geo : meshRegistry[rh])
                    {
                        if (geo.hasAlpha != transparent) continue;
                        if (geo.hasSkin && drawSc && drawSc->valid())
                        {
                            // Identity matrix — transform is baked into bone texture.
                            static const float identMtx[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                            meshRenderer.drawSkinnedMesh(geo, identMtx, transparent, drawSc->boneTexture);
                        }
                        else
                        {
                            meshRenderer.drawMesh(geo, model, transparent);
                        }
                    }
                }
            }
        };

        // ── Per-frame skeleton update ─────────────────────────────────────────
        for (Entity e : world.entities())
        {
            SkeletonComponent* sc = world.getSkeleton(e);
            if (!sc || !sc->valid()) continue;

            uint32_t jc = sc->jointCount;
            std::vector<std::array<float,16>> worldMats(jc);

            for (uint32_t j = 0; j < jc; ++j)
            {
                const auto& t = sc->localTranslation[j];
                const auto& q = sc->localRotation[j];
                const auto& s = sc->localScale[j];

                float x=q[0], y=q[1], z=q[2], w=q[3];
                float x2=x+x, y2=y+y, z2=z+z;
                float xx=x*x2, xy=x*y2, xz=x*z2;
                float yy=y*y2, yz=y*z2, zz=z*z2;
                float wx=w*x2, wy=w*y2, wz=w*z2;

                float local[16];
                local[ 0]=(1.0f-(yy+zz))*s[0]; local[ 1]=(xy+wz)*s[0];         local[ 2]=(xz-wy)*s[0];         local[ 3]=0;
                local[ 4]=(xy-wz)*s[1];          local[ 5]=(1.0f-(xx+zz))*s[1]; local[ 6]=(yz+wx)*s[1];         local[ 7]=0;
                local[ 8]=(xz+wy)*s[2];          local[ 9]=(yz-wx)*s[2];         local[10]=(1.0f-(xx+yy))*s[2]; local[11]=0;
                local[12]=t[0];                  local[13]=t[1];                  local[14]=t[2];                 local[15]=1;

                int par = sc->parentIndex[j];
                if (par < 0)
                {
                    for (int k=0;k<16;++k) worldMats[j][k]=local[k];
                }
                else
                {
                    const float* a = worldMats[par].data();
                    const float* b = local;
                    float* c = worldMats[j].data();
                    c[ 0]=a[0]*b[0]+a[4]*b[1]+a[ 8]*b[2]+a[12]*b[3];
                    c[ 1]=a[1]*b[0]+a[5]*b[1]+a[ 9]*b[2]+a[13]*b[3];
                    c[ 2]=a[2]*b[0]+a[6]*b[1]+a[10]*b[2]+a[14]*b[3];
                    c[ 3]=a[3]*b[0]+a[7]*b[1]+a[11]*b[2]+a[15]*b[3];
                    c[ 4]=a[0]*b[4]+a[4]*b[5]+a[ 8]*b[6]+a[12]*b[7];
                    c[ 5]=a[1]*b[4]+a[5]*b[5]+a[ 9]*b[6]+a[13]*b[7];
                    c[ 6]=a[2]*b[4]+a[6]*b[5]+a[10]*b[6]+a[14]*b[7];
                    c[ 7]=a[3]*b[4]+a[7]*b[5]+a[11]*b[6]+a[15]*b[7];
                    c[ 8]=a[0]*b[8]+a[4]*b[9]+a[ 8]*b[10]+a[12]*b[11];
                    c[ 9]=a[1]*b[8]+a[5]*b[9]+a[ 9]*b[10]+a[13]*b[11];
                    c[10]=a[2]*b[8]+a[6]*b[9]+a[10]*b[10]+a[14]*b[11];
                    c[11]=a[3]*b[8]+a[7]*b[9]+a[11]*b[10]+a[15]*b[11];
                    c[12]=a[0]*b[12]+a[4]*b[13]+a[ 8]*b[14]+a[12]*b[15];
                    c[13]=a[1]*b[12]+a[5]*b[13]+a[ 9]*b[14]+a[13]*b[15];
                    c[14]=a[2]*b[12]+a[6]*b[13]+a[10]*b[14]+a[14]*b[15];
                    c[15]=a[3]*b[12]+a[7]*b[13]+a[11]*b[14]+a[15]*b[15];
                }

                const float* wm = worldMats[j].data();
                const float* ib = sc->inverseBindMatrices[j].data();
                float* sm = sc->skinningMatrices[j].data();
                sm[ 0]=wm[0]*ib[0]+wm[4]*ib[1]+wm[ 8]*ib[2]+wm[12]*ib[3];
                sm[ 1]=wm[1]*ib[0]+wm[5]*ib[1]+wm[ 9]*ib[2]+wm[13]*ib[3];
                sm[ 2]=wm[2]*ib[0]+wm[6]*ib[1]+wm[10]*ib[2]+wm[14]*ib[3];
                sm[ 3]=wm[3]*ib[0]+wm[7]*ib[1]+wm[11]*ib[2]+wm[15]*ib[3];
                sm[ 4]=wm[0]*ib[4]+wm[4]*ib[5]+wm[ 8]*ib[6]+wm[12]*ib[7];
                sm[ 5]=wm[1]*ib[4]+wm[5]*ib[5]+wm[ 9]*ib[6]+wm[13]*ib[7];
                sm[ 6]=wm[2]*ib[4]+wm[6]*ib[5]+wm[10]*ib[6]+wm[14]*ib[7];
                sm[ 7]=wm[3]*ib[4]+wm[7]*ib[5]+wm[11]*ib[6]+wm[15]*ib[7];
                sm[ 8]=wm[0]*ib[8]+wm[4]*ib[9]+wm[ 8]*ib[10]+wm[12]*ib[11];
                sm[ 9]=wm[1]*ib[8]+wm[5]*ib[9]+wm[ 9]*ib[10]+wm[13]*ib[11];
                sm[10]=wm[2]*ib[8]+wm[6]*ib[9]+wm[10]*ib[10]+wm[14]*ib[11];
                sm[11]=wm[3]*ib[8]+wm[7]*ib[9]+wm[11]*ib[10]+wm[15]*ib[11];
                sm[12]=wm[0]*ib[12]+wm[4]*ib[13]+wm[ 8]*ib[14]+wm[12]*ib[15];
                sm[13]=wm[1]*ib[12]+wm[5]*ib[13]+wm[ 9]*ib[14]+wm[13]*ib[15];
                sm[14]=wm[2]*ib[12]+wm[6]*ib[13]+wm[10]*ib[14]+wm[14]*ib[15];
                sm[15]=wm[3]*ib[12]+wm[7]*ib[13]+wm[11]*ib[14]+wm[15]*ib[15];
            }

            // Build skelModel from this entity's TransformComponent and premultiply
            // every skinning matrix by it so that position/rotation/scale from the
            // Properties panel actually moves the skinned mesh visually. The skinned
            // vertex shader does not read u_model at all — it only reads the bone
            // texture — so if we don't bake the entity transform here it has no effect.
            uint32_t texW = jc * 4;
            std::vector<float> texBuf(texW * 4);

            float skelModel[16];
            {
                TransformComponent* stc = world.getTransform(e);
                if (stc)
                {
                    float str[16], srx[16], sry[16], srz[16], srot[16], ssc[16];
                    bx::mtxTranslate(str, stc->position.x, stc->position.y, stc->position.z);
                    bx::mtxRotateX(srx, bx::toRad(stc->rotation.x));
                    bx::mtxRotateY(sry, bx::toRad(stc->rotation.y));
                    bx::mtxRotateZ(srz, bx::toRad(stc->rotation.z));
                    bx::mtxMul(srot, srx, sry);
                    bx::mtxMul(srot, srot, srz);
                    bx::mtxScale(ssc, stc->scale.x, stc->scale.y, stc->scale.z);
                    bx::mtxMul(skelModel, ssc, srot);
                    bx::mtxMul(skelModel, skelModel, str);
                }
                else
                {
                    bx::mtxIdentity(skelModel);
                }
            }

            for (uint32_t j = 0; j < jc; ++j)
            {
                const float* a = skelModel;
                const float* b = sc->skinningMatrices[j].data();
                float* dst = texBuf.data() + j * 16;
                dst[ 0]=a[0]*b[0]+a[4]*b[1]+a[ 8]*b[2]+a[12]*b[3];
                dst[ 1]=a[1]*b[0]+a[5]*b[1]+a[ 9]*b[2]+a[13]*b[3];
                dst[ 2]=a[2]*b[0]+a[6]*b[1]+a[10]*b[2]+a[14]*b[3];
                dst[ 3]=a[3]*b[0]+a[7]*b[1]+a[11]*b[2]+a[15]*b[3];
                dst[ 4]=a[0]*b[4]+a[4]*b[5]+a[ 8]*b[6]+a[12]*b[7];
                dst[ 5]=a[1]*b[4]+a[5]*b[5]+a[ 9]*b[6]+a[13]*b[7];
                dst[ 6]=a[2]*b[4]+a[6]*b[5]+a[10]*b[6]+a[14]*b[7];
                dst[ 7]=a[3]*b[4]+a[7]*b[5]+a[11]*b[6]+a[15]*b[7];
                dst[ 8]=a[0]*b[8]+a[4]*b[9]+a[ 8]*b[10]+a[12]*b[11];
                dst[ 9]=a[1]*b[8]+a[5]*b[9]+a[ 9]*b[10]+a[13]*b[11];
                dst[10]=a[2]*b[8]+a[6]*b[9]+a[10]*b[10]+a[14]*b[11];
                dst[11]=a[3]*b[8]+a[7]*b[9]+a[11]*b[10]+a[15]*b[11];
                dst[12]=a[0]*b[12]+a[4]*b[13]+a[ 8]*b[14]+a[12]*b[15];
                dst[13]=a[1]*b[12]+a[5]*b[13]+a[ 9]*b[14]+a[13]*b[15];
                dst[14]=a[2]*b[12]+a[6]*b[13]+a[10]*b[14]+a[14]*b[15];
                dst[15]=a[3]*b[12]+a[7]*b[13]+a[11]*b[14]+a[15]*b[15];
            }

            const bgfx::Memory* mem = bgfx::copy(texBuf.data(),
                                                  (uint32_t)(texBuf.size() * sizeof(float)));
            bgfx::updateTexture2D(sc->boneTexture, 0, 0, 0, 0,
                                  (uint16_t)texW, 1, mem);
        }

        drawEntities(false);
        drawEntities(true);

        ImGui::Render();
        bgfx::setViewRect(255, 0, 0, (uint16_t)windowW, (uint16_t)windowH);
        bgfx::setViewMode(255, bgfx::ViewMode::Sequential);
        imguiBgfxRender(ImGui::GetDrawData());
        bgfx::frame();
    }

    for (auto& geo : editorLightGeos) {
        if (bgfx::isValid(geo.normalMap)) bgfx::destroy(geo.normalMap);
        if (bgfx::isValid(geo.texture))   bgfx::destroy(geo.texture);
        if (bgfx::isValid(geo.vbh))       bgfx::destroy(geo.vbh);
        if (bgfx::isValid(geo.ibh))       bgfx::destroy(geo.ibh);
    }
    for (Entity e : world.entities())
    {
        SkeletonComponent* sc = world.getSkeleton(e);
        if (sc && bgfx::isValid(sc->boneTexture))
            bgfx::destroy(sc->boneTexture);
    }
    for (auto& geoVec : meshRegistry)
        for (auto& geo : geoVec) {
            if (bgfx::isValid(geo.normalMap)) bgfx::destroy(geo.normalMap);
            if (bgfx::isValid(geo.texture))   bgfx::destroy(geo.texture);
            if (bgfx::isValid(geo.vbh))       bgfx::destroy(geo.vbh);
            if (bgfx::isValid(geo.ibh))       bgfx::destroy(geo.ibh);
        }

    bgfx::destroy(viewportFB);
    bgfx::destroy(viewportColor);
    bgfx::destroy(viewportDepth);
    gridRenderer.shutdown();
    lightRenderer.shutdown();
    meshRenderer.shutdown();
    imguiBgfxShutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
