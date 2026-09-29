#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "components.h"

using Entity = uint32_t;
static constexpr Entity INVALID_ENTITY = UINT32_MAX;

class World
{
public:
    Entity createEntity()
    {
        Entity id = m_nextId++;
        m_entities.push_back(id);
        return id;
    }

    void destroyEntity(Entity e)
    {
        m_transforms.erase(e);
        m_names.erase(e);
        m_meshes.erase(e);
        m_lights.erase(e);
        m_skeletons.erase(e);
        for (auto it = m_entities.begin(); it != m_entities.end(); ++it)
        {
            if (*it == e) { m_entities.erase(it); break; }
        }
    }

    // ── Transform ─────────────────────────────────────────────────────────────
    TransformComponent& addTransform(Entity e)  { return m_transforms[e]; }
    TransformComponent* getTransform(Entity e)
    {
        auto it = m_transforms.find(e);
        return it != m_transforms.end() ? &it->second : nullptr;
    }

    // ── Name ──────────────────────────────────────────────────────────────────
    NameComponent& addName(Entity e, const std::string& name = "Entity")
    {
        m_names[e].name = name;
        return m_names[e];
    }
    NameComponent* getName(Entity e)
    {
        auto it = m_names.find(e);
        return it != m_names.end() ? &it->second : nullptr;
    }

    // ── Mesh ──────────────────────────────────────────────────────────────────
    MeshComponent& addMesh(Entity e)    { return m_meshes[e]; }
    MeshComponent* getMesh(Entity e)
    {
        auto it = m_meshes.find(e);
        return it != m_meshes.end() ? &it->second : nullptr;
    }

    // ── Light ─────────────────────────────────────────────────────────────────
        LightComponent& addLight(Entity e)  { return m_lights[e]; }
    LightComponent* getLight(Entity e)
    {
        auto it = m_lights.find(e);
        return it != m_lights.end() ? &it->second : nullptr;
    }

    SkeletonComponent& addSkeleton(Entity e)  { return m_skeletons[e]; }
    SkeletonComponent* getSkeleton(Entity e)
    {
        auto it = m_skeletons.find(e);
        return it != m_skeletons.end() ? &it->second : nullptr;
    }

    const std::vector<Entity>& entities() const { return m_entities; }

private:
    uint32_t                                   m_nextId    = 0;
    std::vector<Entity>                        m_entities;
    std::unordered_map<Entity, TransformComponent>  m_transforms;
    std::unordered_map<Entity, NameComponent>       m_names;
    std::unordered_map<Entity, MeshComponent>       m_meshes;
    std::unordered_map<Entity, LightComponent>      m_lights;
    std::unordered_map<Entity, SkeletonComponent>   m_skeletons;
};