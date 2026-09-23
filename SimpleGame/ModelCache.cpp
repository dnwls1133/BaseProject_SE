#include "stdafx.h"
#include "ModelCache.h"
#include "ContentData.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <utility>

namespace
{
constexpr std::uint32_t CacheVersion = 4;
constexpr std::uint32_t CacheMagic = 0x48424D43;

std::uint32_t HashBytes(const void* data, std::size_t count)
{
    auto bytes = static_cast<const unsigned char*>(data);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < count; ++i)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}
} // namespace

const std::string& ModelCache::Status() const
{
    return status;
}

void ModelCache::Initialize(const std::string& directory)
{
    if (initialized)
    {
        return;
    }
    names.clear();
    rigs.clear();
    recipes.clear();
    sourcePaths.clear();
    std::string catalogPath = ContentData::Path(directory, "models.catalog");
    std::string catalog = ContentData::Read(catalogPath);
    sourceHash = HashBytes(catalog.data(), catalog.size());
    ContentData::Rows(
        catalogPath, catalog,
        [&](const std::string& command, ContentData::Row& row)
        {
            ContentData::Require(command == "model", "model record required");
            std::string name = row.Word(), rig = row.Word(), relative = row.Word();
            ContentData::Require(std::find(names.begin(), names.end(), name) == names.end(),
                                 "duplicate model: " + name);
            ContentData::Require(rig == "static" || rig == "pilgrim" || rig == "creature",
                                 "unknown model rig");
            std::string path = ContentData::Path(directory, relative);
            std::string recipe = ContentData::Read(path);
            sourceHash = (sourceHash ^ HashBytes(recipe.data(), recipe.size())) * 16777619u;
            names.push_back(name);
            rigs.push_back(rig);
            recipes.push_back(std::move(recipe));
            sourcePaths.push_back(path);
            ContentData::Require(names.size() <= 256, "too many models");
        });
    const char* required[] = {"Pilgrim",      "Infected", "Brute",  "Pillar",   "Roots",
                              "Brazier",      "Shrine",   "Potion", "Fragment", "Crawler",
                              "BrokenWall",   "Arch",     "Statue", "Grave",    "Cart",
                              "FungalColony", "Debris",   "Corpse", "Fence"};
    ContentData::Require(names.size() >= static_cast<std::size_t>(ModelId::Count),
                         catalogPath + ": missing core models");
    for (std::size_t i = 0; i < static_cast<std::size_t>(ModelId::Count); ++i)
    {
        ContentData::Require(names[i] == required[i], catalogPath + ": preserve core model order");
    }
    Profile::Event("model_catalog", "source_hash=" + std::to_string(sourceHash) +
                                        " model_count=" + std::to_string(names.size()));
    std::string folder = ContentData::Path(directory, "Cache");
    std::string path = ContentData::Path(folder, "level1_models.bin");
    if (Read(path))
    {
        Profile::Add(Profile::Metric::CacheHit);
        Profile::Event("model_cache", "hit");
        status = "외부 모델 캐시 불러옴";
    }
    else
    {
        Profile::Add(Profile::Metric::CacheMiss);
        Profile::Event("model_cache", "miss_rebuild");
        Build();
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        bool saved = !error && Write(path);
        status =
            saved ? "외부 모델 로딩 및 캐시 갱신 완료" : "외부 모델 로딩 완료 · 캐시 저장 실패";
    }
    // Pack static models once in the renderer's layout after either cache load or build.
    staticMeshes.assign(models.size(), {});
    for (std::size_t index = 0; index < models.size(); ++index)
    {
        if (rigs[index] != "static")
        {
            continue;
        }
        auto& mesh = staticMeshes[index];
        mesh.reserve(models[index].size());
        for (const auto& vertex : models[index])
        {
            mesh.push_back({vertex.x, vertex.y, vertex.r, vertex.g, vertex.b, vertex.a, 0, 0,
                            static_cast<float>(vertex.material)});
        }
    }
    recipes.clear();
    initialized = true;
}

ModelId ModelCache::Find(const std::string& name) const
{
    auto found = std::find(names.begin(), names.end(), name);
    ContentData::Require(found != names.end(), "unknown model: " + name);
    return static_cast<ModelId>(found - names.begin());
}

bool ModelCache::Read(const std::string& path)
{
    PROFILE_SCOPE(CacheRead);
    static_assert(std::is_trivially_copyable<Vertex>::value, "Cache records must be plain data");
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::uint32_t header[5] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != CacheMagic || header[1] != CacheVersion ||
        header[2] != names.size() || header[3] != sizeof(Vertex) || header[4] != sourceHash)
    {
        return false;
    }

    std::vector<std::vector<Vertex>> loaded(names.size());
    for (auto& model : loaded)
    {
        std::uint32_t count = 0, checksum = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        file.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
        if (!file || count == 0 || count > 60000 || count % 3 != 0)
        {
            return false;
        }

        model.resize(count);
        file.read(reinterpret_cast<char*>(model.data()), count * sizeof(Vertex));
        if (!file || HashBytes(model.data(), count * sizeof(Vertex)) != checksum)
        {
            return false;
        }

        for (const auto& vertex : model)
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.r) ||
                !std::isfinite(vertex.g) || !std::isfinite(vertex.b) || !std::isfinite(vertex.a) ||
                std::abs(vertex.x) > 150 || vertex.y < -160 || vertex.y > 60 || vertex.r < 0 ||
                vertex.r > 4 || vertex.g < 0 || vertex.g > 4 || vertex.b < 0 || vertex.b > 4 ||
                vertex.a < 0 || vertex.a > 1 || (vertex.material > 5 && vertex.material != 9) ||
                vertex.joint > 6)
            {
                return false;
            }
        }
    }

    if (file.peek() != std::ifstream::traits_type::eof())
    {
        return false;
    }

    models = std::move(loaded);
    return true;
}

bool ModelCache::Write(const std::string& path) const
{
    PROFILE_SCOPE(CacheWrite);
    std::string temporary = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    std::uint32_t header[5] = {CacheMagic, CacheVersion, static_cast<std::uint32_t>(models.size()),
                               sizeof(Vertex), sourceHash};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (const auto& model : models)
    {
        auto count = static_cast<std::uint32_t>(model.size());
        std::uint32_t checksum = HashBytes(model.data(), model.size() * sizeof(Vertex));
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        file.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
        file.write(reinterpret_cast<const char*>(model.data()), model.size() * sizeof(Vertex));
    }
    file.flush();
    bool written = file.good();
    file.close();

    std::filesystem::path tempPath(temporary), finalPath(path);
    bool committed = written && MoveFileExW(tempPath.c_str(), finalPath.c_str(),
                                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!committed)
    {
        std::error_code error;
        std::filesystem::remove(tempPath, error);
    }
    return committed;
}

void ModelCache::Triangle(ModelId model, Point a, Point b, Point c, Color color, Material material,
                          std::uint32_t joint)
{
    auto& output = models[static_cast<std::size_t>(model)];
    for (Point p : {a, b, c})
    {
        output.push_back({p.x, p.y, color.r, color.g, color.b, color.a,
                          static_cast<std::uint32_t>(material), joint});
    }
}

void ModelCache::Quad(ModelId model, Point a, Point b, Point c, Point d, Color color,
                      Material material, std::uint32_t joint)
{
    Triangle(model, a, b, c, color, material, joint);
    Triangle(model, a, c, d, color, material, joint);
}

void ModelCache::Ellipse(ModelId model, float x, float y, float rx, float ry, Color color,
                         std::uint32_t joint)
{
    for (int i = 0; i < 32; ++i)
    {
        float a = i * 6.2831853f / 32, b = (i + 1) * 6.2831853f / 32;
        Triangle(model, {x, y}, {x + std::cos(a) * rx, y + std::sin(a) * ry},
                 {x + std::cos(b) * rx, y + std::sin(b) * ry}, color, Material::Plain, joint);
    }
}

void ModelCache::Build()
{
    PROFILE_SCOPE(CacheBuild);
    models.assign(names.size(), {});
    for (std::size_t index = 0; index < recipes.size(); ++index)
    {
        ModelId id = static_cast<ModelId>(index);
        ContentData::Rows(
            sourcePaths[index], recipes[index],
            [&](const std::string& command, ContentData::Row& row)
            {
                unsigned material = static_cast<unsigned>(row.Integer(0, 9));
                unsigned joint = static_cast<unsigned>(row.Integer(0, 6));
                ContentData::Require(material <= 5 || material == 9, "invalid model material");
                float red = row.Number(0, 3), green = row.Number(0, 3), blue = row.Number(0, 3);
                Color color(red, green, blue, row.Number(0, 1));
                auto point = [&]()
                {
                    float x = row.Number(-300, 300), y = row.Number(-300, 300);
                    return Point{x, y};
                };
                if (command == "triangle" || command == "quad")
                {
                    Point a = point(), b = point(), c = point();
                    if (command == "quad")
                    {
                        Point d = point();
                        Quad(id, a, b, c, d, color, static_cast<Material>(material), joint);
                    }
                    else
                    {
                        Triangle(id, a, b, c, color, static_cast<Material>(material), joint);
                    }
                }
                else if (command == "ellipse")
                {
                    Point center = point();
                    float rx = row.Number(.1f, 100), ry = row.Number(.1f, 100);
                    ContentData::Require(material == 0, "ellipse material must be plain");
                    Ellipse(id, center.x, center.y, rx, ry, color, joint);
                }
                else if (command == "segment")
                {
                    Point a = point(), b = point();
                    float width = row.Number(.1f, 30);
                    Segment(id, a, b, width, color, joint, static_cast<Material>(material));
                }
                else
                {
                    ContentData::Require(false, "unknown model primitive: " + command);
                }
                ContentData::Require(models[index].size() <= 60000, "model vertex budget exceeded");
            });
        ContentData::Require(!models[index].empty(), sourcePaths[index] + ": empty model");
        for (const auto& vertex : models[index])
        {
            ContentData::Require(std::abs(vertex.x) <= 150 && vertex.y >= -160 && vertex.y <= 60,
                                 sourcePaths[index] + ": model exceeds supported actor bounds");
        }
    }
}

void ModelCache::Segment(ModelId id, Point a, Point b, float width, Color color,
                         std::uint32_t joint, Material material)
{
    float length = std::hypot(b.x - a.x, b.y - a.y);
    if (length < .001f)
    {
        return;
    }
    float nx = -(b.y - a.y) / length, ny = (b.x - a.x) / length;
    auto strip = [&](float w, Color tint)
    {
        Quad(id, {a.x + nx * w, a.y + ny * w}, {b.x + nx * w * .72f, b.y + ny * w * .72f},
             {b.x - nx * w * .72f, b.y - ny * w * .72f}, {a.x - nx * w, a.y - ny * w}, tint,
             material, joint);
    };
    strip(width + 1.2f, Color(.075f, .066f, .058f));
    strip(width, color);
    Quad(id, {a.x + nx * width * .65f, a.y + ny * width * .65f},
         {b.x + nx * width * .45f, b.y + ny * width * .45f}, {b.x, b.y}, {a.x, a.y},
         Color(color.r * 1.2f, color.g * 1.17f, color.b * 1.13f), material, joint);
}

void ModelCache::Draw(Renderer& renderer, ModelId id, Point position, float phase, float movement,
                      float facing, float flash, float attack, float scale, float opacity,
                      float collapse, float idleTime) const
{
    std::size_t index = static_cast<std::size_t>(id);
    if (index >= models.size())
    {
        return;
    }
    PROFILE_SCOPE(ModelDraw);
    Profile::Scope staticScope(Profile::Metric::StaticModelDraw, rigs[index] == "static");
    const auto& model = models[index];
    Profile::ModelScope modelScope(names[index], model.size(), rigs[index] == "static");
    Profile::Add(Profile::Metric::ModelCalls);
    Profile::Add(Profile::Metric::ModelVertices, double(model.size()));
    if (rigs[index] == "static")
    {
        Profile::Add(Profile::Metric::StaticVertices, double(model.size()));
    }
    if (rigs[index] == "static" && movement == 0 && attack == 0 && flash == 0 && collapse == 0)
    {
        Profile::Add(Profile::Metric::StaticFastPath);
        renderer.AppendMesh(staticMeshes[index], position, facing * scale, scale, opacity);
        renderer.SetMaterial(Material::Plain);
        return;
    }
    bool creature = rigs[index] == "creature";
    float swing = std::sin(phase) * movement;
    float strike = std::sin(attack * 3.14159265f);
    bool pilgrim = rigs[index] == "pilgrim";
    float breath = pilgrim    ? std::sin(idleTime * 2) * .007f
                   : creature ? std::sin(phase * .43f) * .012f
                              : 0;
    float angles[7] = {0,
                       swing * .16f,
                       -swing * .16f,
                       -swing * .13f,
                       swing * .13f - strike * .48f,
                       swing * .025f,
                       breath * 2};
    Point pivots[7] = {{0, 0}, {-7, -28}, {7, -28}, {-12, -50}, {12, -50}, {0, -43}, {0, -60}};
    if (pilgrim)
    {
        pivots[1] = {-7, -32};
        pivots[2] = {7, -32};
        pivots[3] = {-13, -53};
        pivots[4] = {12, -53};
        pivots[5] = {0, -57};
        pivots[6] = {0, -61};
        angles[3] = -swing * .10f + std::sin(idleTime * 1.5f) * .016f;
        angles[4] = swing * .12f - strike * .95f;
        angles[5] = std::sin(phase - .65f) * movement * .065f + std::sin(idleTime * 1.4f) * .025f;
        angles[6] = breath * 1.5f;
    }
    float cosine[7], sine[7];
    for (int i = 0; i < 7; ++i)
    {
        cosine[i] = std::cos(angles[i]);
        sine[i] = std::sin(angles[i]);
    }
    auto transform = [&](const Vertex& vertex)
    {
        unsigned joint = vertex.joint;
        Point pivot = pivots[joint];
        float localX = vertex.x - pivot.x, localY = vertex.y - pivot.y;
        float x = pivot.x + localX * cosine[joint] - localY * sine[joint];
        float y = pivot.y + localX * sine[joint] + localY * cosine[joint];
        y *= 1 + breath;
        y -= std::abs(swing) * 1.4f;
        if (pilgrim)
        {
            // A delayed flutter grows toward the loose hem, keeping the shoulders anchored.
            float hem = std::clamp((vertex.y + 55) / 45.f, 0.f, 1.f);
            if (joint == 5)
            {
                x += std::sin(idleTime * 2.4f + vertex.y * .07f) * hem * (1 + movement * 2);
            }
            x += strike * 2;
        }
        x += flash * 3;
        x -= vertex.y * collapse * .40f;
        y = y * (1 - collapse * .85f) + collapse * 5;
        return Point{position.x + x * facing * scale, position.y + y * scale};
    };
    for (std::size_t i = 0; i + 2 < model.size(); i += 3)
    {
        const auto& vertex = model[i];
        renderer.SetMaterial(static_cast<Material>(vertex.material));
        float darken = 1 - collapse * .48f;
        Color color(vertex.r * darken + flash * .6f, vertex.g * darken + flash * .4f,
                    vertex.b * darken + flash * .35f, vertex.a * opacity);
        renderer.Triangle(transform(vertex), transform(model[i + 1]), transform(model[i + 2]),
                          color);
    }
    renderer.SetMaterial(Material::Plain);
}
