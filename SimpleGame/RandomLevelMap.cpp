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

RandomLevelMap::Region RandomLevelMap::RegionAt(int x, int y) const
{
    Region result = Region::Refuge;
    int nearest = Width * Width + Height * Height;
    for (const auto& district : districts)
    {
        int dx = x - district.center.x, dy = y - district.center.y;
        int distance = dx * dx + dy * dy;
        if (distance < nearest)
        {
            nearest = distance;
            result = district.region;
        }
    }
    return result;
}

const char* RandomLevelMap::RegionName(int x, int y) const
{
    switch (RegionAt(x, y))
    {
    case Region::Refuge:
        return "꺼져가는 성소";
    case Region::Processional:
        return "부서진 순례길";
    case Region::Ossuary:
        return "이름 없는 매장지";
    case Region::Flooded:
        return "성혈이 고인 수로";
    case Region::Cathedral:
        return "무너진 대성당";
    case Region::Garden:
        return "우화의 정원";
    }
    return "성혈의 사냥터";
}

void RandomLevelMap::Generate(std::uint32_t value)
{
    seed = value;
    std::mt19937 random(seed);
    tiles.fill(0);
    rooms.clear();
    districts.clear();
    reachable.clear();
    const Region themes[] = {Region::Refuge,  Region::Processional, Region::Ossuary,
                             Region::Flooded, Region::Cathedral,    Region::Garden,
                             Region::Ossuary, Region::Garden,       Region::Flooded};

    // Recognizable districts keep their roles; proportions and connecting streets vary per run.
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            Cell center{8 + column * 16 + static_cast<int>(random() % 3) - 1,
                        8 + row * 16 + static_cast<int>(random() % 3) - 1};
            int rx = 4 + random() % 3, ry = 4 + random() % 3;
            Region region = themes[row * 3 + column];
            if (region == Region::Processional)
            {
                rx = 6;
                ry = 3 + random() % 2;
            }
            else if (region == Region::Cathedral)
            {
                rx = ry = 6;
            }
            rooms.push_back(center);
            districts.push_back({center, rx, ry, region});
            for (int dy = -ry; dy <= ry; ++dy)
            {
                for (int dx = -rx; dx <= rx; ++dx)
                {
                    bool floor = std::abs(dx) + std::abs(dy) <= rx + ry - 2;
                    if (region == Region::Garden)
                    {
                        float nx = dx / static_cast<float>(rx), ny = dy / static_cast<float>(ry);
                        floor = nx * nx + ny * ny <= 1.08f;
                    }
                    else if (region == Region::Cathedral)
                    {
                        floor = std::abs(dx) <= 2 || std::abs(dy) <= 2 ||
                                std::abs(dx) + std::abs(dy) <= 8;
                    }
                    if (floor)
                    {
                        Carve(center.x + dx, center.y + dy, 0);
                    }
                }
            }
        }
    }

    // A connected backbone plus cross streets gives loops without sealed rooms.
    for (std::size_t i = 1; i < rooms.size(); ++i)
    {
        std::size_t parent = i % 3 == 0 ? i - 3 : i - 1;
        Connect(rooms[parent], rooms[i], random() % 2 == 0);
    }
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 1; column < 3; ++column)
        {
            if (column == 2 || random() % 2 == 0)
            {
                Connect(rooms[row * 3 + column], rooms[(row + 1) * 3 + column], random() % 2 == 0);
            }
        }
    }

    for (const auto& district : districts)
    {
        if (district.region != Region::Ossuary && district.region != Region::Cathedral)
        {
            continue;
        }
        for (int dy : {-3, 3})
        {
            for (int dx : {-3, 3})
            {
                int x = district.center.x + dx, y = district.center.y + dy;
                tiles[y * Width + x] = 0;
            }
        }
    }

    // Shallow pools follow continuous shapes; all water remains traversable.
    for (const auto& district : districts)
    {
        if (district.region != Region::Flooded && district.region != Region::Garden)
        {
            continue;
        }
        for (int y = district.center.y - district.radiusY;
             y <= district.center.y + district.radiusY; ++y)
        {
            for (int x = district.center.x - district.radiusX;
                 x <= district.center.x + district.radiusX; ++x)
            {
                float dx = (x - district.center.x + 1.f) / (district.radiusX * .85f);
                float dy = (y - district.center.y) / (district.radiusY * .7f);
                float edge = 1.f + .13f * std::sin(x * 1.7f + y * .8f);
                if (Walkable(x, y) && dx * dx + dy * dy < edge &&
                    (district.region == Region::Flooded || x < district.center.x))
                {
                    tiles[y * Width + x] = 2;
                }
            }
        }
    }

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
            }
        }
    }
}
