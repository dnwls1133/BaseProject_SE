#include "stdafx.h"
#include "RandomLevelMap.h"
#include <algorithm>
#include <cmath>
#include <queue>

int RandomLevelMap::Tile(int x, int y) const
{
    return x >= 0 && y >= 0 && x < Width && y < Height ? tiles[y * Width + x] : 0;
}

bool RandomLevelMap::Walkable(int x, int y) const
{
    return Tile(x, y) != 0;
}

bool RandomLevelMap::CanStand(float x, float y, float radius) const
{
    for (int cy = static_cast<int>(std::floor(y - radius));
         cy <= static_cast<int>(std::floor(y + radius)); ++cy)
    {
        for (int cx = static_cast<int>(std::floor(x - radius));
             cx <= static_cast<int>(std::floor(x + radius)); ++cx)
        {
            if (!Walkable(cx, cy))
            {
                return false;
            }
        }
    }
    return true;
}

bool RandomLevelMap::CanTravel(float ax, float ay, float bx, float by) const
{
    int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(bx - ax, by - ay) * 8)));
    for (int i = 0; i <= steps; ++i)
    {
        float t = i / static_cast<float>(steps);
        if (!CanStand(ax + (bx - ax) * t, ay + (by - ay) * t))
        {
            return false;
        }
    }
    return true;
}

bool RandomLevelMap::Visible(float ax, float ay, float bx, float by) const
{
    float distance = std::hypot(bx - ax, by - ay);
    int steps = std::max(1, static_cast<int>(std::ceil(distance * 8)));
    for (int i = 0; i <= steps; ++i)
    {
        float t = i / static_cast<float>(steps);
        if (!Walkable(static_cast<int>(std::floor(ax + (bx - ax) * t)),
                      static_cast<int>(std::floor(ay + (by - ay) * t))))
        {
            return false;
        }
    }
    return true;
}

std::array<int, RandomLevelMap::Width * RandomLevelMap::Height> RandomLevelMap::Distances(
    int x, int y) const
{
    std::array<int, Width * Height> distance;
    distance.fill(-1);
    if (!Walkable(x, y))
    {
        return distance;
    }

    std::queue<Cell> pending;
    pending.push({x, y});
    distance[y * Width + x] = 0;
    constexpr int directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!pending.empty())
    {
        Cell cell = pending.front();
        pending.pop();
        for (const auto& direction : directions)
        {
            int nx = cell.x + direction[0], ny = cell.y + direction[1];
            if (Walkable(nx, ny) && distance[ny * Width + nx] < 0)
            {
                distance[ny * Width + nx] = distance[cell.y * Width + cell.x] + 1;
                pending.push({nx, ny});
            }
        }
    }
    return distance;
}

void RandomLevelMap::Carve(int x, int y, int radius)
{
    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            int nx = x + dx, ny = y + dy;
            if (nx > 0 && ny > 0 && nx < Width - 1 && ny < Height - 1)
            {
                tiles[ny * Width + nx] = 1;
            }
        }
    }
}

void RandomLevelMap::Connect(Cell a, Cell b, bool horizontalFirst)
{
    // Three-cell-wide corridors keep a finite-radius actor from corner trapping.
    auto horizontal = [&]()
    {
        while (a.x != b.x)
        {
            Carve(a.x, a.y, 1);
            a.x += a.x < b.x ? 1 : -1;
        }
    };
    auto vertical = [&]()
    {
        while (a.y != b.y)
        {
            Carve(a.x, a.y, 1);
            a.y += a.y < b.y ? 1 : -1;
        }
    };
    if (horizontalFirst)
    {
        horizontal();
        vertical();
    }
    else
    {
        vertical();
        horizontal();
    }
    Carve(b.x, b.y, 1);
}

void RandomLevelMap::Generate(std::uint32_t value)
{
    seed = value;
    std::mt19937 random(seed);
    tiles.fill(0);
    rooms.clear();
    reachable.clear();

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            Cell center{6 + column * 11 + static_cast<int>(random() % 3) - 1,
                        6 + row * 11 + static_cast<int>(random() % 3) - 1};
            rooms.push_back(center);
            Carve(center.x, center.y, 2 + random() % 3);
        }
    }

    // Every room is attached to the existing connected component.
    for (std::size_t i = 1; i < rooms.size(); ++i)
    {
        std::size_t nearest = 0;
        int best = 1000;
        for (std::size_t j = 0; j < i; ++j)
        {
            int distance = std::abs(rooms[i].x - rooms[j].x) + std::abs(rooms[i].y - rooms[j].y);
            if (distance < best)
            {
                nearest = j;
                best = distance;
            }
        }
        Connect(rooms[i], rooms[nearest], random() % 2 == 0);
    }
    for (int i = 0; i < 3; ++i)
    {
        Connect(rooms[random() % rooms.size()], rooms[random() % rooms.size()], random() % 2 == 0);
    }

    // Runtime invariant: repair any disconnected floor before placing gameplay objects.
    auto distances = Distances(rooms.front().x, rooms.front().y);
    for (int y = 1; y < Height - 1; ++y)
    {
        for (int x = 1; x < Width - 1; ++x)
        {
            if (Walkable(x, y) && distances[y * Width + x] < 0)
            {
                Connect({x, y}, rooms.front(), true);
                distances = Distances(rooms.front().x, rooms.front().y);
            }
        }
    }

    for (int y = 1; y < Height - 1; ++y)
    {
        for (int x = 1; x < Width - 1; ++x)
        {
            if (distances[y * Width + x] >= 0)
            {
                reachable.push_back({x, y});
                // Water is shallow and walkable. It cannot sever the route graph.
                if (random() % 19 == 0)
                {
                    tiles[y * Width + x] = 2;
                }
            }
        }
    }
}
