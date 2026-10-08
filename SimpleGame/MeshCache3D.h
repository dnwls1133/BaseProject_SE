#pragma once
#include "WorldRenderer3D.h"
#include <string>

struct Pose3D
{
    float phase = 0, movement = 0, attack = 0, collapse = 0, time = 0, flash = 0;
    int activity = 0; // 0 idle, 1 work, 2 carry, 3 treat, 4 converse, 5 sleep.
};

class MeshCache3D
{
  public:
    struct PlacedPart
    {
        int primitive = 0, material = 0;
        Mat4 transform;
        Color color{1, 1, 1};
    };

    std::vector<PlacedPart> Place(int id, Vec3 position, float yaw, float scale) const;
    void DrawPlaced(Renderer& renderer, const std::vector<PlacedPart>& parts, Color tint) const;
    void Load(const std::string& root);
    int Find(const std::string& name) const;
    void Bounds(int id, Vec3& minimum, Vec3& maximum) const;
    void Draw(Renderer& renderer, int id, Vec3 position, float yaw, float scale,
              const Pose3D& pose = {}, Color tint = Color(1, 1, 1)) const;

  private:
    struct Part
    {
        int primitive = 0, joint = 0, material = 0;
        Vec3 position, size{1, 1, 1}, rotation;
        Color color{1, 1, 1};
        Mat4 local;
    };

    struct Joint
    {
        int parent = -1;
        Vec3 pivot;
    };

    struct Model
    {
        std::vector<Joint> joints;
        std::vector<Part> parts;
    };

    std::vector<Model> models;
    std::vector<std::string> names;
    std::string loadedRoot;
    std::uint32_t hash = 0;
    bool ReadCache(const std::string& path);
    void WriteCache(const std::string& path) const;
};
