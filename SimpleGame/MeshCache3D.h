#pragma once
#include "WorldRenderer3D.h"
#include <string>

class ResourceLibrary3D;

struct Pose3D
{
    float phase = 0, movement = 0, attack = 0, collapse = 0, time = 0, flash = 0, speed = 0;
    float distance = -1;
    int attackStyle = 0;
    int activity = 0; // 0 idle, 1 work, 2 carry, 3 treat, 4 converse, 5 sleep.
};

class MeshCache3D
{
  public:
    struct PlacedPart
    {
        std::shared_ptr<Geometry3D> geometry;
        Mat4 transform;
        Color color{1, 1, 1};
    };

    std::vector<PlacedPart> Place(int id, Vec3 position, float yaw, float scale) const;
    void DrawPlaced(Renderer& renderer, const std::vector<PlacedPart>& parts, Color tint) const;
    void Load(const std::string& root);
    int Find(const std::string& name) const;
    int PropModel(const std::string& id, int fallback) const;
    void Bounds(int id, Vec3& minimum, Vec3& maximum) const;
    void Draw(Renderer& renderer, int id, Vec3 position, float yaw, float scale,
              const Pose3D& pose = {}, Color tint = Color(1, 1, 1)) const;

    std::shared_ptr<SurfaceSet3D> Surfaces() const;

  private:
    std::shared_ptr<ResourceLibrary3D> resources;
};
