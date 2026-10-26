#include "stdafx.h"
#include "Terrain3D.h"
#include "RandomLevelMap.h"
#include <algorithm>

float Terrain3D::Height(float x, float z)
{
    // Small continuous slopes preserve the existing connected navigation grid.
    return std::sin(x * .085f) * .30f + std::cos(z * .071f) * .24f;
}

void Terrain3D::Build(const RandomLevelMap& map, const std::shared_ptr<SurfaceSet3D>& surfaces)
{
    PROFILE_SCOPE(CacheBuild);
    chunks.clear();
    for (int cz = 0; cz < map.Height; cz += 16)
    {
        for (int cx = 0; cx < map.Width; cx += 16)
        {
            auto mesh = std::make_shared<Geometry3D>();
            mesh->surfaces = surfaces;
            mesh->minimum = {float(cx), -.65f, float(cz)};
            mesh->maximum = {float(cx + 16), .65f, float(cz + 16)};
            for (int z = cz; z < std::min(cz + 16, map.Height); ++z)
            {
                for (int x = cx; x < std::min(cx + 16, map.Width); ++x)
                {
                    int tile = map.Tile(x, z);
                    const auto& region = map.DistrictAt(x, z);
                    Color c = tile == 2 ? region.water : region.floor;
                    if (tile == 0)
                    {
                        c = Color(.13f, .16f, .14f);
                    }
                    if (tile == 4)
                    {
                        c = Color(c.r * .8f, c.g * .7f, c.b * .6f);
                    }
                    const unsigned hash =
                        (unsigned(x) * 73856093u) ^ (unsigned(z) * 19349663u) ^ map.seed;
                    float variation = .87f + float(hash % 19) * .01f;
                    float kind = tile == 2 ? 7.f : tile == 1 ? 5.f : 2.f;
                    auto p = [](float x, float z)
                    {
                        return Vec3{x, Height(x, z), z};
                    };
                    Vec3 a = p(float(x), float(z)), b = p(x + 1.f, float(z)),
                         d = p(float(x), z + 1.f), e = p(x + 1.f, z + 1.f);
                    auto tri = [&](Vec3 p0, Vec3 p1, Vec3 p2)
                    {
                        Vec3 n = Normalize(Cross(p1 - p0, p2 - p0));
                        for (Vec3 pos : {p0, p1, p2})
                        {
                            Vertex3D vertex{
                                pos, n, c.r * variation, c.g * variation, c.b * variation, kind};
                            if (surfaces && tile != 2)
                            {
                                const std::string surface = tile == 1           ? "SootStone"
                                                            : region.style == 5 ? "ContaminatedSoil"
                                                                                : "GraveSoil";
                                vertex.uvLayer = {pos.x * .5f, pos.z * .5f,
                                                  float(surfaces->Find(surface))};
                                // PNG supplies the authored albedo, retain only restrained
                                // variation.
                                vertex.r = vertex.g = vertex.b = variation;
                            }
                            mesh->vertices.push_back(vertex);
                        }
                    };
                    tri(a, d, b);
                    tri(b, d, e);
                }
            }
            chunks.push_back(std::move(mesh));
        }
    }
}

void Terrain3D::Draw(Renderer& r) const
{
    PROFILE_SCOPE(Ground);
    for (const auto& chunk : chunks)
    {
        if (r.Camera().Visible(chunk->minimum, chunk->maximum, 240))
        {
            r.Submit3D(chunk);
        }
    }
}
