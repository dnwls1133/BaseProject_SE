#pragma once
#include "LevelData.h"
#include <cstdint>
#include <random>
#include <vector>

class RandomLevelMap
{
  public:
    int Width = 80;
    int Height = 64;

    struct Cell
    {
        int x, y;
    };

    enum class Region
    {
        Refuge,
        Processional,
        Ossuary,
        Flooded,
        Cathedral,
        Garden
    };

    struct District : RegionDefinition
    {
        Cell center;
        Region region;
    };

    std::vector<District> districts;
    std::vector<PropPlacement> placements;
    const District& DistrictAt(int x, int y) const;
    Region RegionAt(int x, int y) const;
    const char* RegionName(int x, int y) const;

    std::vector<unsigned char> tiles;
    std::vector<Cell> reachable;
    std::uint32_t seed = 0;

    void Generate(std::uint32_t value, const LevelData& definition);
    bool Walkable(int x, int y) const;
    bool CanStand(float x, float y, float radius = .23f) const;
    bool CanTravel(float ax, float ay, float bx, float by) const;
    bool Visible(float ax, float ay, float bx, float by) const;
    std::vector<int> Distances(int x, int y) const;
    int Tile(int x, int y) const;

  private:
    void Paint(int x, int y, int radius, unsigned char tile);
};
