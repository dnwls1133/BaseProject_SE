#pragma once
#include "WorldRenderer3D.h"
#include <string>

struct Pose3D;

// Prepared GLB assets share immutable vertices, UVs and animation palettes across all Actors.
class ResourceLibrary3D
{
  public:
    struct Placement
    {
        std::shared_ptr<Geometry3D> geometry;
        Mat4 transform;
    };

    void Load(const std::string& root);
    int Find(const std::string& name) const;
    int PropModel(const std::string& id, int fallback) const;

    std::shared_ptr<SurfaceSet3D> Surfaces() const
    {
        return surfaces;
    }

    void Bounds(int id, Vec3& minimum, Vec3& maximum) const;
    std::vector<Placement> Place(int id, Vec3 position, float yaw, float scale) const;
    void Draw(Renderer& renderer, int id, Vec3 position, float yaw, float scale, const Pose3D& pose,
              Color tint) const;

  private:
    struct Clip
    {
        std::string name;
        std::uint32_t start = 0, frames = 1;
        float duration = 0;
    };

    struct Asset
    {
        std::string id;
        std::shared_ptr<Geometry3D> geometry;
        std::vector<Clip> clips;
        int bones = 1;
        float walkDistance = 1.3f, runDistance = 2.5f;
    };

    struct Part
    {
        int asset = -1;
        Mat4 transform;
    };

    struct Prefab
    {
        std::string name, frozenClip;
        float frozenTime = 0;
        Mat4 root;
        std::vector<Part> parts;
    };

    std::unordered_map<std::string, int> propBindings;
    std::vector<Asset> assets;
    std::vector<Prefab> prefabs;
    std::shared_ptr<SurfaceSet3D> surfaces;
    std::string loadedRoot;
    Asset ReadAsset(const std::string& path, const std::string& id) const;
    Vec3 Animation(const Asset& asset, const Prefab& prefab, const Pose3D& pose, float scale) const;
};
