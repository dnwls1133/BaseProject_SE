#pragma once
#include "Renderer.h"
#include <cstdint>
#include <string>
#include <vector>

enum class ModelId : std::uint32_t
{
    Pilgrim,
    Infected,
    Brute,
    Pillar,
    Roots,
    Brazier,
    Shrine,
    Potion,
    Fragment,
    Crawler,
    BrokenWall,
    Arch,
    Statue,
    Grave,
    Cart,
    FungalColony,
    Debris,
    Corpse,
    Fence,
    Count
};

// Baked, renderer-independent triangle data. No tessellation on a warm cache load.
class ModelCache
{
  public:
    void Initialize(const std::string& dataDirectory);
    void Draw(Renderer& renderer, ModelId model, Point position, float phase = 0,
              float movement = 0, float facing = 1, float flash = 0, float attack = 0,
              float scale = 1, float opacity = 1, float collapse = 0, float idleTime = 0) const;
    const std::string& Status() const;

  private:
    struct Vertex
    {
        float x, y, r, g, b, a;
        std::uint32_t material;
        std::uint32_t joint;
    };

    std::vector<std::vector<Vertex>> models;
    std::string status;
    bool initialized = false;

    bool Read(const std::string& path);
    bool Write(const std::string& path) const;
    void Build();
    void BuildPilgrim();
    void BuildCreatures();
    void BuildEnvironment();
    void Segment(ModelId model, Point a, Point b, float width, Color color, std::uint32_t joint = 0,
                 Material material = Material::Plain);
    void Triangle(ModelId model, Point a, Point b, Point c, Color color,
                  Material material = Material::Plain, std::uint32_t joint = 0);
    void Quad(ModelId model, Point a, Point b, Point c, Point d, Color color,
              Material material = Material::Plain, std::uint32_t joint = 0);
    void Ellipse(ModelId model, float x, float y, float rx, float ry, Color color,
                 std::uint32_t joint = 0);
};
