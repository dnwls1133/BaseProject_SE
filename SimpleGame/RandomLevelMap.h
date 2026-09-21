#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <vector>

class RandomLevelMap
{
  public:
    static constexpr int Width = 36;
    static constexpr int Height = 36;

    struct Cell
    {
        int x, y;
    };

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
