#include "stdafx.h"
#include "Profiler.h"
#include "RandomLevelMap.h"
#include "ContentData.h"
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
    PROFILE_SCOPE(Collision);
    Profile::Add(Profile::Metric::CollisionCalls);
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
    PROFILE_SCOPE(Visibility);
    Profile::Add(Profile::Metric::VisibilityCalls);
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

std::vector<int> RandomLevelMap::Distances(int x, int y) const
{
    PROFILE_SCOPE(Navigation);
    Profile::Add(Profile::Metric::NavigationBuilds);
    std::vector<int> distance(Width * Height, -1);
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
        Profile::Add(Profile::Metric::NavigationVisited);
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

void RandomLevelMap::Paint(int x, int y, int radius, unsigned char tile)
{
    for (int dy = -radius; dy <= radius; ++dy)
    {
        for (int dx = -radius; dx <= radius; ++dx)
        {
            int nx = x + dx, ny = y + dy;
            if (nx > 0 && ny > 0 && nx < Width - 1 && ny < Height - 1)
            {
                tiles[ny * Width + nx] = tile;
            }
        }
    }
}

const RandomLevelMap::District& RandomLevelMap::DistrictAt(int x, int y) const
{
    const District* result = &districts.front();
    int nearest = Width * Width + Height * Height;
    for (const auto& district : districts)
    {
        int dx = x - district.center.x, dy = y - district.center.y;
        int distance = dx * dx + dy * dy;
        if (distance < nearest)
        {
            nearest = distance;
            result = &district;
        }
    }
    return *result;
}

RandomLevelMap::Region RandomLevelMap::RegionAt(int x, int y) const
{
    return DistrictAt(x, y).region;
}

const char* RandomLevelMap::RegionName(int x, int y) const
{
    return DistrictAt(x, y).name.c_str();
}

void RandomLevelMap::Generate(std::uint32_t value, const LevelData& definition)
{
    PROFILE_SCOPE(TerrainGenerate);
    seed = value;
    Width = definition.width;
    Height = definition.height;
    std::mt19937 random(seed);
    tiles.assign(Width * Height, 0);
    districts.clear();
    reachable.clear();
    placements.clear();
    for (const auto& region : definition.regions)
    {
        District district;
        static_cast<RegionDefinition&>(district) = region;
        district.center = {region.x, region.y};
        district.region = static_cast<Region>(region.style);
        districts.push_back(district);
    }
    // The whole interior is traversable countryside, independent of roads and landmarks.
    for (int y = 1; y < Height - 1; ++y)
    {
        for (int x = 1; x < Width - 1; ++x)
        {
            tiles[y * Width + x] = 3;
        }
    }
    for (const auto& patch : definition.patches)
    {
        for (int y = std::max(1, patch.y - patch.ry); y <= std::min(Height - 2, patch.y + patch.ry);
             ++y)
        {
            for (int x = std::max(1, patch.x - patch.rx);
                 x <= std::min(Width - 2, patch.x + patch.rx); ++x)
            {
                float dx = (x - patch.x) / static_cast<float>(patch.rx);
                float dy = (y - patch.y) / static_cast<float>(patch.ry);
                float edge = patch.tile == 1 ? 1.f : 1.f + .12f * std::sin(x * 1.7f + y * .8f);
                if (dx * dx + dy * dy <= edge)
                {
                    tiles[y * Width + x] = static_cast<unsigned char>(patch.tile);
                }
            }
        }
    }
    // Roads paint routes through terrain; they are never the only traversable strips.
    for (const auto& road : definition.roads)
    {
        int steps = std::max(std::abs(road.bx - road.ax), std::abs(road.by - road.ay));
        for (int i = 0; i <= steps; ++i)
        {
            float t = steps > 0 ? i / static_cast<float>(steps) : 0;
            int x = static_cast<int>(std::round(road.ax + (road.bx - road.ax) * t));
            int y = static_cast<int>(std::round(road.ay + (road.by - road.ay) * t));
            Paint(x, y, road.radius, 4);
        }
    }
    Paint(definition.startX, definition.startY, 2, 1);
    auto place = [&](const PropPlacement& prop)
    {
        placements.push_back(prop);
        if (definition.Prop(prop.id).blocking)
        {
            tiles[static_cast<int>(prop.y) * Width + static_cast<int>(prop.x)] = 0;
        }
    };
    for (const auto& prop : definition.placements)
    {
        place(prop);
    }
    for (const auto& scatter : definition.scatter)
    {
        int added = 0;
        for (int attempt = 0; attempt < scatter.count * 30 && added < scatter.count; ++attempt)
        {
            int x = scatter.x + static_cast<int>(random() % (scatter.rx * 2 + 1)) - scatter.rx;
            int y = scatter.y + static_cast<int>(random() % (scatter.ry * 2 + 1)) - scatter.ry;
            if (x < 2 || y < 2 || x >= Width - 2 || y >= Height - 2 ||
                (Tile(x, y) != 3 && Tile(x, y) != 0) ||
                std::hypot(static_cast<float>(x - definition.startX),
                           static_cast<float>(y - definition.startY)) < 5)
            {
                continue;
            }
            bool free = std::all_of(placements.begin(), placements.end(),
                                    [&](const PropPlacement& other)
                                    {
                                        return std::hypot(other.x - x - .5f, other.y - y - .5f) >=
                                               scatter.spacing;
                                    });
            if (!free)
            {
                continue;
            }
            place({scatter.id, x + .5f, y + .5f, .75f + random() % 5 * .1f,
                   random() % 2 ? 1.f : -1.f});
            ++added;
        }
    }
    ContentData::Require(CanStand(definition.startX + .5f, definition.startY + .5f),
                         "world spawn is blocked");
    auto distance = Distances(definition.startX, definition.startY);
    for (int y = 1; y < Height - 1; ++y)
    {
        for (int x = 1; x < Width - 1; ++x)
        {
            if (distance[y * Width + x] >= 0)
            {
                reachable.push_back({x, y});
            }
            else if (Walkable(x, y))
            {
                // Remove isolated pockets without carving through a blocking landmark.
                tiles[y * Width + x] = 0;
            }
        }
    }
    for (const auto& prop : placements)
    {
        if (definition.Prop(prop.id).role == "shrine")
        {
            bool accessible = false;
            for (const auto& cell : reachable)
            {
                if (std::hypot(cell.x + .5f - prop.x, cell.y + .5f - prop.y) <= 2 &&
                    Visible(cell.x + .5f, cell.y + .5f, prop.x, prop.y))
                {
                    accessible = true;
                    break;
                }
            }
            ContentData::Require(accessible, "world shrine is inaccessible");
        }
    }
}
