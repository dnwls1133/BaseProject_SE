#include "stdafx.h"
#include "ModelCache.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <utility>

namespace
{
constexpr std::uint32_t CacheVersion = 1;
constexpr std::uint32_t ModelCount = static_cast<std::uint32_t>(ModelId::Count);
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

    std::string folder = directory + "Cache";
    std::string path = folder + "/level1_models.bin";
    if (Read(path))
    {
        status = "모델 캐시 불러옴";
        initialized = true;
        return;
    }

    Build();
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    bool saved = !error && Write(path);
    status = saved ? "모델 생성 및 캐시 저장 완료" : "캐시 저장 실패 · 현재 실행에서는 메모리 사용";
    initialized = true;
}

bool ModelCache::Read(const std::string& path)
{
    static_assert(std::is_trivially_copyable<Vertex>::value, "Cache records must be plain data");
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    std::uint32_t header[4] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != CacheMagic || header[1] != CacheVersion || header[2] != ModelCount ||
        header[3] != sizeof(Vertex))
    {
        return false;
    }

    std::vector<std::vector<Vertex>> loaded(ModelCount);
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
                std::abs(vertex.x) > 300 || std::abs(vertex.y) > 300 || vertex.r < 0 ||
                vertex.r > 4 || vertex.g < 0 || vertex.g > 4 || vertex.b < 0 || vertex.b > 4 ||
                vertex.a < 0 || vertex.a > 1 || vertex.material > 5 || vertex.joint > 5)
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
    std::string temporary = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    std::uint32_t header[4] = {CacheMagic, CacheVersion, ModelCount, sizeof(Vertex)};
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
    models.assign(ModelCount, {});
    const Color bone(.59f, .56f, .41f), iron(.30f, .33f, .31f), blood(.60f, .04f, .09f);

    for (ModelId id : {ModelId::Pilgrim, ModelId::Infected, ModelId::Brute})
    {
        bool player = id == ModelId::Pilgrim;
        bool brute = id == ModelId::Brute;
        float breadth = brute ? 1.35f : 1.f;
        Color robe = player ? Color(.21f, .28f, .28f) : Color(.31f, .21f, .19f);
        for (int side : {-1, 1})
        {
            float x = side * 6.f;
            std::uint32_t joint = side < 0 ? 1 : 2;
            Quad(id, {x - 3, -19}, {x + 3, -19}, {x + 4, -3}, {x - 4, -3}, Color(.20f, .18f, .14f),
                 Material::Plain, joint);
            Ellipse(id, x + 1, -2, 6, 3, iron, joint);
        }
        Quad(id, {-10 * breadth, -43}, {11 * breadth, -42}, {14 * breadth, -14},
             {-14 * breadth, -12}, robe, Material::Cloth, 5);
        Triangle(id, {-9, -40}, {-18, -12}, {-4, -18}, Color(.12f, .16f, .16f), Material::Cloth, 5);
        Quad(id, {-12, -24}, {12, -24}, {12, -20}, {-12, -20}, iron);
        Ellipse(id, 0, -51, player ? 9.f : 8.f, 10, player ? iron : bone);
        if (player)
        {
            Triangle(id, {-10, -53}, {0, -65}, {10, -52}, robe);
            Ellipse(id, 2, -48, 5, 6, bone);
            Quad(id, {-3, -51}, {8, -51}, {8, -48}, {-3, -48}, Color(.04f, .06f, .06f));
        }
        for (int side : {-1, 1})
        {
            float x = side * 12.f * breadth;
            Quad(id, {x - 3, -38}, {x + 3, -38}, {x + side * 5 + 3, -21}, {x + side * 5 - 3, -21},
                 bone, Material::Plain, side < 0 ? 3 : 4);
        }
        if (player)
        {
            Quad(id, {17, -27}, {20, -27}, {24, -2}, {21, -2}, Color(.62f, .64f, .56f),
                 Material::Plain, 4);
            Quad(id, {13, -24}, {24, -24}, {24, -21}, {13, -21}, bone, Material::Plain, 4);
            Quad(id, {-19, -20}, {-13, -20}, {-13, -10}, {-19, -10}, blood, Material::Plain, 3);
        }
        else
        {
            for (int i = 0; i < (brute ? 9 : 5); ++i)
            {
                float x = -14 + i * 4.f;
                Triangle(id, {x, -31}, {x + 8, -58 - (i % 3) * 9.f}, {x + 3, -28}, bone);
                Ellipse(id, x + 5, -39 - i % 2 * 6.f, 2, 3, blood);
            }
        }
    }

    Quad(ModelId::Pillar, {-17, 0}, {17, 0}, {13, -63}, {-13, -56}, Color(.32f, .35f, .30f),
         Material::Stone);
    Quad(ModelId::Pillar, {-13, -56}, {13, -63}, {21, -52}, {-3, -45}, Color(.45f, .45f, .37f),
         Material::Stone);
    Quad(ModelId::Pillar, {-23, 2}, {23, 2}, {18, -9}, {-18, -9}, iron, Material::Stone);

    for (int i = 0; i < 7; ++i)
    {
        float x = -22 + i * 7.f;
        Triangle(ModelId::Roots, {0, 0}, {x + 3, -20 - i % 3 * 12.f}, {x, -2}, bone);
        Ellipse(ModelId::Roots, x + 3, -20 - i % 3 * 12.f, 3, 5, blood);
    }

    Quad(ModelId::Brazier, {-6, 0}, {6, 0}, {4, -27}, {-4, -27}, iron);
    Ellipse(ModelId::Brazier, 0, -29, 18, 8, Color(.36f, .26f, .15f));
    Ellipse(ModelId::Brazier, 0, -31, 13, 4, Color(.11f, .035f, .03f));

    Quad(ModelId::Shrine, {-28, 0}, {28, 0}, {24, -21}, {-24, -21}, iron, Material::Stone);
    Quad(ModelId::Shrine, {-24, -21}, {24, -21}, {15, -36}, {-15, -36}, bone, Material::Stone);
    Quad(ModelId::Shrine, {-2, -27}, {2, -27}, {2, -61}, {-2, -61}, blood);
    Quad(ModelId::Shrine, {-13, -53}, {13, -53}, {13, -49}, {-13, -49}, blood);

    Ellipse(ModelId::Potion, 0, -9, 7, 9, blood);
    Quad(ModelId::Potion, {-4, -22}, {4, -22}, {4, -15}, {-4, -15}, bone);
    Quad(ModelId::Potion, {-3, -15}, {-1, -15}, {-1, -5}, {-3, -5}, Color(.96f, .54f, .48f));
    Quad(ModelId::Fragment, {0, -21}, {9, -10}, {0, 0}, {-7, -9}, Color(.63f, .57f, .31f));
    Triangle(ModelId::Fragment, {0, -21}, {0, 0}, {-7, -9}, Color(.31f, .30f, .19f));
}

void ModelCache::Draw(Renderer& renderer, ModelId id, Point position, float phase, float movement,
                      float facing, float flash, float attack) const
{
    std::size_t index = static_cast<std::size_t>(id);
    if (index >= models.size())
    {
        return;
    }
    const auto& model = models[index];
    auto transform = [&](const Vertex& vertex)
    {
        float x = vertex.x;
        float y = vertex.y;
        float swing = std::sin(phase) * movement;
        if (vertex.joint == 1 || vertex.joint == 2)
        {
            float sign = vertex.joint == 1 ? 1.f : -1.f;
            x += swing * 2 * sign;
            y += swing * 3 * sign;
        }
        else if (vertex.joint == 3 || vertex.joint == 4)
        {
            y += swing * (vertex.joint == 3 ? -4 : 4);
            if (vertex.joint == 4)
            {
                x += std::sin(attack * 3.14159265f) * 15;
                y -= std::sin(attack * 3.14159265f) * 12;
            }
        }
        else if (vertex.joint == 5)
        {
            x += std::sin(phase * .5f) * movement * 2;
        }
        y -= std::abs(swing) * 1.4f;
        return Point{position.x + x * facing, position.y + y};
    };

    for (std::size_t i = 0; i + 2 < model.size(); i += 3)
    {
        const auto& vertex = model[i];
        renderer.SetMaterial(static_cast<Material>(vertex.material));
        Color color(vertex.r + flash * .6f, vertex.g + flash * .4f, vertex.b + flash * .35f,
                    vertex.a);
        renderer.Triangle(transform(vertex), transform(model[i + 1]), transform(model[i + 2]),
                          color);
    }
    renderer.SetMaterial(Material::Plain);
}
