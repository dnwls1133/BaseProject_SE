#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <vector>

class RandomLevelMap
{
  public:
    static constexpr int Width = 48;
    static constexpr int Height = 48;

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

    struct District
    {
        Cell center;
        int radiusX, radiusY;
        Region region;
    };

    std::vector<District> districts;
    Region RegionAt(int x, int y) const;
    const char* RegionName(int x, int y) const;

    std::array<unsigned char, Width * Height> tiles = {};
    std::vector<Cell> rooms;
    std::vector<Cell> reachable;
    std::uint32_t seed = 0;

    void Generate(std::uint32_t value);
    bool Walkable(int x, int y) const;
    bool CanStand(float x, float y, float radius = .23f) const;
    bool CanTravel(float ax, float ay, float bx, float by) const;
    bool Visible(float ax, float ay, float bx, float by) const;
    std::array<int, Width * Height> Distances(int x, int y) const;
    int Tile(int x, int y) const;

  private:
    void Carve(int x, int y, int radius);
    void Connect(Cell a, Cell b, bool horizontalFirst);
};
