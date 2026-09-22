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
constexpr std::uint32_t CacheVersion = 3;
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

    BuildPilgrim();

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
    BuildCreatures();
    BuildEnvironment();
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

void ModelCache::BuildPilgrim()
{
    const ModelId id = ModelId::Pilgrim;
    const Color outline(.055f, .075f, .08f), cloak(.16f, .24f, .25f);
    const Color fold(.24f, .33f, .33f), clothLight(.37f, .44f, .40f);
    const Color steel(.32f, .39f, .39f), edge(.65f, .69f, .60f);
    const Color leather(.25f, .19f, .14f), brass(.61f, .48f, .28f);
    const Color bone(.67f, .60f, .45f);

    // Rear cloak: a broad shoulder silhouette, separate torn panels and stitched hems.
    Quad(id, {-14, -59}, {13, -57}, {25, -13}, {-25, -10}, outline, Material::Cloth, 5);
    for (int i = 0; i < 5; ++i)
    {
        float top = -13.f + i * 5.5f, hem = -22.f + i * 10;
        float length = -9.f - (i % 2) * 8;
        Quad(id, {top, -56}, {top + 6, -55}, {hem + 8, length - 4}, {hem, length},
             i % 2 ? cloak : fold, Material::Cloth, 5);
        Triangle(id, {top + 1, -53}, {hem + 2, length - 5}, {hem + 5, length - 9},
                 Color(.11f, .18f, .20f), Material::Cloth, 5);
        Segment(id, {hem, length - 3}, {hem + 7, length - 7}, .65f, clothLight, 5);
    }

    // Thighs, wrapped knees and plated boots have independent leg joints.
    for (int side : {-1, 1})
    {
        unsigned joint = side < 0 ? 1 : 2;
        float x = side * 7.f;
        Segment(id, {x, -32}, {x * 1.15f, -17}, 4.2f, leather, joint, Material::Cloth);
        Ellipse(id, x * 1.15f, -19, 5, 4.5f, outline, joint);
        Ellipse(id, x * 1.15f - 1, -20, 3.5f, 3, steel, joint);
        Segment(id, {x * 1.15f, -15}, {x * 1.3f, -4}, 3.5f, steel, joint);
        Segment(id, {x * 1.15f - 2, -14}, {x * 1.3f - 2, -5}, .75f, edge, joint);
        Ellipse(id, x * 1.3f + 2, -2, 7.5f, 3.5f, outline, joint);
        Quad(id, {x * 1.3f - 4, -5}, {x * 1.3f + 6, -6}, {x * 1.3f + 9, -2}, {x * 1.3f - 4, -2},
             steel, Material::Plain, joint);
    }

    // Split tabard sits in front of the legs; the breastplate has distinct lit planes.
    Quad(id, {-12, -42}, {11, -42}, {15, -19}, {-14, -20}, cloak, Material::Cloth);
    Triangle(id, {-9, -32}, {-12, -15}, {-1, -22}, fold, Material::Cloth);
    Triangle(id, {2, -31}, {5, -15}, {12, -21}, fold, Material::Cloth);
    Quad(id, {-13, -59}, {11, -59}, {13, -35}, {-11, -33}, outline);
    Quad(id, {-11, -57}, {0, -60}, {0, -36}, {-10, -37}, steel);
    Quad(id, {0, -60}, {10, -56}, {10, -36}, {0, -36}, Color(.23f, .30f, .31f));
    Segment(id, {-10, -56}, {-2, -59}, .9f, edge);
    Segment(id, {0, -56}, {0, -39}, .8f, edge);
    for (int i = 0; i < 3; ++i)
    {
        float y = -47.f + i * 4;
        Segment(id, {-9, y}, {0, y + 2}, 1, steel);
        Segment(id, {0, y + 2}, {9, y - 1}, .8f, edge);
    }
    Segment(id, {-11, -57}, {11, -34}, 2, leather);
    Ellipse(id, -5, -50, 2, 2, brass);
    Quad(id, {-13, -34}, {13, -35}, {13, -30}, {-13, -29}, leather);
    Quad(id, {-3, -35}, {3, -35}, {3, -29}, {-3, -29}, brass);
    Quad(id, {-1.5f, -33}, {1.5f, -33}, {1.5f, -30}, {-1.5f, -30}, outline);
    Quad(id, {-13, -29}, {-6, -28}, {-6, -20}, {-14, -21}, leather);
    Segment(id, {-12, -27}, {-7, -26}, .8f, brass);

    // Off hand holds the small blood reliquary; its cage distinguishes it from bare flesh.
    Segment(id, {-13, -53}, {-19, -42}, 4, cloak, 3, Material::Cloth);
    Segment(id, {-19, -42}, {-19, -30}, 3, leather, 3);
    Quad(id, {-23, -42}, {-16, -41}, {-16, -32}, {-23, -33}, steel, Material::Plain, 3);
    Segment(id, {-22, -40}, {-22, -34}, .7f, edge, 3);
    Ellipse(id, -19, -28, 3.5f, 3, bone, 3);
    Segment(id, {-19, -28}, {-20, -22}, 1, brass, 3);
    Ellipse(id, -20, -16, 6, 8, outline, 3);
    Ellipse(id, -20, -16, 4, 6, Color(.64f, .075f, .13f), 3);
    Ellipse(id, -21, -18, 1.3f, 3, Color(1.2f, .40f, .35f), 3);
    Segment(id, {-25, -22}, {-15, -22}, 1.1f, brass, 3);
    Segment(id, {-25, -11}, {-15, -11}, 1.1f, brass, 3);
    Segment(id, {-24, -21}, {-24, -12}, .8f, steel, 3);
    Segment(id, {-16, -21}, {-16, -12}, .8f, steel, 3);

    // Sword arm: articulated gauntlet, wrapped grip, weathered crossguard and bevelled blade.
    Segment(id, {12, -53}, {19, -41}, 4.3f, leather, 4);
    Segment(id, {19, -41}, {23, -29}, 3.3f, steel, 4);
    Segment(id, {17, -41}, {21, -31}, .9f, edge, 4);
    Ellipse(id, 24, -27, 4, 3.5f, outline, 4);
    Ellipse(id, 24, -28, 3, 2, steel, 4);
    Segment(id, {25, -33}, {25, -20}, 1.8f, leather, 4);
    for (int i = 0; i < 3; ++i)
    {
        Segment(id, {23, -31.f + i * 3}, {27, -30.f + i * 3}, .55f, brass, 4);
    }
    Ellipse(id, 25, -35, 3, 3, brass, 4);
    Quad(id, {22, -19}, {28, -19}, {29, 9}, {25, 16}, outline, Material::Plain, 4);
    Triangle(id, {23, -19}, {26, -19}, {25, 13}, Color(.76f, .79f, .69f), Material::Plain, 4);
    Triangle(id, {26, -19}, {28, 8}, {25, 13}, steel, Material::Plain, 4);
    Segment(id, {16, -21}, {34, -21}, 1.6f, brass, 4);
    Segment(id, {17, -22}, {20, -25}, 1, edge, 4);
    Segment(id, {30, -21}, {34, -18}, 1, edge, 4);
    Segment(id, {24, -12}, {25, -5}, .65f, outline, 4);

    // Layered pauldrons cover shoulder pivots without hiding the moving forearms.
    for (int side : {-1, 1})
    {
        float x = side * 13.f;
        unsigned joint = side < 0 ? 3 : 4;
        Ellipse(id, x, -55, 8.5f, 6, outline, joint);
        Quad(id, {x - 7, -58}, {x + 4, -61}, {x + 8, -53}, {x - 6, -51}, steel, Material::Plain,
             joint);
        Segment(id, {x - 6, -58}, {x + 3, -60}, 1, edge, joint);
        Ellipse(id, x + 4, -55, 1.2f, 1.2f, brass, joint);
    }

    // Hood, inset face, brow and cloth mask retain a human silhouette at game scale.
    Segment(id, {-6, -60}, {7, -60}, 4, outline);
    Quad(id, {-13, -65}, {-7, -81}, {7, -84}, {15, -67}, outline, Material::Cloth, 6);
    Triangle(id, {-7, -79}, {-12, -65}, {2, -72}, fold, Material::Cloth, 6);
    Triangle(id, {-7, -79}, {6, -82}, {12, -69}, cloak, Material::Cloth, 6);
    Ellipse(id, 3, -68, 8, 10, outline, 6);
    Quad(id, {-2, -74}, {8, -75}, {10, -64}, {0, -61}, bone, Material::Plain, 6);
    Triangle(id, {6, -74}, {10, -64}, {5, -63}, Color(.41f, .36f, .27f), Material::Plain, 6);
    Segment(id, {-1, -71}, {8, -71}, 1.3f, outline, 6);
    Segment(id, {2, -70}, {4, -70}, .55f, Color(.88f, .86f, .66f), 6);
    Quad(id, {-3, -66}, {10, -65}, {7, -58}, {-2, -59}, cloak, Material::Cloth, 6);
    Segment(id, {-2, -64}, {7, -63}, .7f, clothLight, 6);
    Segment(id, {-10, -75}, {-13, -64}, 1, clothLight, 6);
    Quad(id, {-12, -61}, {4, -60}, {11, -54}, {-10, -55}, fold, Material::Cloth, 5);
    Triangle(id, {-10, -59}, {-22, -45}, {-14, -43}, cloak, Material::Cloth, 5);
    Ellipse(id, -9, -57, 2.5f, 2.5f, brass);
    Segment(id, {-10, -59}, {-8, -55}, .6f, bone);
}

void ModelCache::BuildCreatures()
{
    const Color bone(.66f, .61f, .46f), flesh(.39f, .24f, .21f), dark(.095f, .07f, .075f);
    const Color blood(.51f, .07f, .12f), iron(.24f, .29f, .27f), cloth(.21f, .19f, .16f);
    auto growth = [&](ModelId id, float x, float y, float height, float lean)
    {
        Segment(id, {x, y}, {x + lean, y - height}, 2.3f, bone, 6, Material::Organic);
        Segment(id, {x + lean * .45f, y - height * .45f}, {x + lean - 8, y - height * .78f}, 1.1f,
                bone, 6);
        Ellipse(id, x + lean, y - height, 4, 2.8f, blood, 6);
        Ellipse(id, x + lean - 1, y - height - 1, 1.5f, 1, Color(1.15f, .30f, .28f), 6);
    };

    // A narrow, elongated penitent with a buried face and a splintered reliquary arm.
    ModelId id = ModelId::Infected;
    growth(id, -10, -43, 37, -15);
    growth(id, 7, -49, 31, 12);
    for (int side : {-1, 1})
    {
        unsigned joint = side < 0 ? 1 : 2;
        float x = side * 7.f;
        Segment(id, {x, -30}, {x * 1.5f, -15}, 3.4f, flesh, joint);
        Segment(id, {x * 1.5f, -15}, {x * 1.4f, -3}, 2.4f, bone, joint);
        Segment(id, {x * 1.4f, -3}, {x * 1.4f + 7, -1}, 2.8f, dark, joint);
    }
    Quad(id, {-12, -55}, {10, -54}, {16, -22}, {-14, -17}, dark, Material::Cloth, 5);
    for (int i = 0; i < 5; ++i)
    {
        float x = -12 + i * 5.f;
        Triangle(id, {x, -43}, {x + 6, -39}, {x + 1, -13 - i % 2 * 7.f},
                 i % 2 ? cloth : Color(.31f, .26f, .22f), Material::Cloth, 5);
    }
    Ellipse(id, 0, -50, 12, 17, dark);
    Ellipse(id, -2, -51, 9, 15, flesh);
    for (int i = 0; i < 5; ++i)
    {
        float y = -60 + i * 4.2f;
        Segment(id, {-8, y}, {0, y + 3}, 1.1f, bone);
        Segment(id, {0, y + 3}, {8, y - 1}, 1.1f, bone);
    }
    Segment(id, {0, -62}, {1, -36}, 1.4f, blood, 0, Material::Organic);
    Segment(id, {-10, -55}, {-22, -40}, 3.6f, flesh, 3);
    Segment(id, {-22, -40}, {-24, -20}, 2.4f, bone, 3);
    Segment(id, {10, -54}, {23, -39}, 5.3f, flesh, 4);
    Segment(id, {23, -39}, {32, -19}, 4, bone, 4);
    for (int i = 0; i < 3; ++i)
    {
        Segment(id, {30.f + i * 2, -23}, {33.f + i * 4, -9.f + i * 2}, 1.2f, bone, 4);
    }
    Ellipse(id, 1, -71, 9, 12, dark, 6);
    Ellipse(id, -1, -72, 7, 10, bone, 6);
    Quad(id, {-8, -77}, {9, -74}, {7, -69}, {-7, -71}, cloth, Material::Cloth, 6);
    Segment(id, {-1, -70}, {3, -63}, 1.5f, dark, 6);
    Ellipse(id, 6, -72, 1.8f, 1, Color(1.1f, .21f, .17f), 6);

    // A bell keeper: broad stone-and-flesh shoulders, tiny head, massive striking arm.
    id = ModelId::Brute;
    for (int side : {-1, 1})
    {
        unsigned joint = side < 0 ? 1 : 2;
        float x = side * 12.f;
        Segment(id, {x, -36}, {x * 1.3f, -17}, 8, flesh, joint);
        Segment(id, {x * 1.3f, -17}, {x * 1.5f, -4}, 5.5f, bone, joint);
        Ellipse(id, x * 1.5f + 3, -3, 10, 4.5f, dark, joint);
    }
    Ellipse(id, -1, -54, 29, 32, dark);
    Ellipse(id, -4, -56, 25, 29, flesh);
    Ellipse(id, -8, -60, 18, 22, Color(.46f, .33f, .26f));
    Quad(id, {-27, -49}, {25, -46}, {19, -20}, {-19, -23}, cloth, Material::Cloth, 5);
    for (int i = 0; i < 7; ++i)
    {
        float x = -25 + i * 8.f;
        Triangle(id, {x, -55}, {x + 5, -67}, {x + 8, -28 - i % 3 * 4.f}, i % 2 ? bone : iron,
                 Material::Stone);
        Segment(id, {x + 2, -39}, {x + 5, -20}, 1.1f, blood);
    }
    for (int i = 0; i < 5; ++i)
    {
        growth(id, -22 + i * 10.f, -70, 15 + i % 3 * 7.f, (i - 2) * 5.f);
    }
    Segment(id, {-24, -67}, {-35, -39}, 8, flesh, 3);
    Segment(id, {-35, -39}, {-32, -20}, 5, bone, 3);
    Segment(id, {22, -65}, {38, -47}, 11, flesh, 4);
    Segment(id, {38, -47}, {42, -16}, 10, iron, 4, Material::Stone);
    Quad(id, {30, -37}, {51, -36}, {56, -11}, {27, -12}, dark, Material::Stone, 4);
    Quad(id, {32, -34}, {48, -34}, {51, -14}, {30, -15}, bone, Material::Stone, 4);
    Segment(id, {37, -31}, {43, -18}, 1.5f, blood, 4);
    Ellipse(id, -6, -81, 11, 12, dark, 6);
    Quad(id, {-15, -88}, {1, -92}, {6, -77}, {-11, -72}, bone, Material::Stone, 6);
    Quad(id, {-12, -84}, {1, -86}, {2, -82}, {-11, -80}, dark, Material::Plain, 6);
    Ellipse(id, -2, -84, 2, 1.3f, Color(1.2f, .28f, .14f), 6);

    // A low, six-limbed host: the silhouette and gait differ even at quarter-view scale.
    id = ModelId::Crawler;
    for (int side : {-1, 1})
    {
        for (int i = 0; i < 3; ++i)
        {
            unsigned joint = (i + (side > 0 ? 1 : 0)) % 2 == 0 ? 1 : 2;
            float x = side * (14.f + i * 3), y = -30.f + i * 9;
            Point elbow{side * (31.f + i * 2), y - 6};
            Segment(id, {x, y}, elbow, 2.6f, flesh, joint);
            Segment(id, elbow, {side * (40.f - i * 3), -2.f + i * 2}, 1.5f, bone, joint);
        }
    }
    Ellipse(id, -5, -24, 24, 16, dark);
    Ellipse(id, -8, -26, 20, 14, flesh);
    for (int i = 0; i < 5; ++i)
    {
        float x = -24 + i * 7.f;
        Quad(id, {x, -33}, {x + 5, -39}, {x + 9, -16}, {x + 4, -13}, i % 2 ? bone : iron,
             Material::Stone);
        growth(id, x + 3, -33, 9 + i % 3 * 5.f, -6);
    }
    Segment(id, {10, -29}, {25, -18}, 6, flesh, 4);
    Ellipse(id, 24, -19, 10, 9, dark, 6);
    Ellipse(id, 24, -22, 8, 7, bone, 6);
    for (int i = 0; i < 4; ++i)
    {
        Triangle(id, {18.f + i * 4, -19}, {21.f + i * 4, -19}, {20.f + i * 4, -12}, bone,
                 Material::Plain, 6);
    }
    Ellipse(id, 28, -25, 2, 1.3f, Color(1.2f, .3f, .26f), 6);
}

void ModelCache::BuildEnvironment()
{
    const Color stone(.34f, .38f, .35f), light(.51f, .52f, .43f), shade(.17f, .22f, .21f);
    const Color bone(.57f, .54f, .41f), rust(.27f, .17f, .13f), blood(.41f, .055f, .095f);
    auto block = [&](ModelId id, float x, float y, float w, float h, float depth)
    {
        Quad(id, {x, y}, {x + w, y + w * .5f}, {x + w, y + w * .5f - h}, {x, y - h}, stone,
             Material::Stone);
        Quad(id, {x + w, y + w * .5f}, {x + w + depth, y + w * .5f - depth * .5f},
             {x + w + depth, y + w * .5f - depth * .5f - h}, {x + w, y + w * .5f - h}, shade,
             Material::Stone);
        Quad(id, {x, y - h}, {x + depth, y - depth * .5f - h},
             {x + w + depth, y + w * .5f - depth * .5f - h}, {x + w, y + w * .5f - h}, light,
             Material::Stone);
    };

    ModelId id = ModelId::BrokenWall;
    for (int i = 0; i < 4; ++i)
    {
        block(id, -43.f + i * 20, -17.f + i * 10, 19, 34.f + (i % 3) * 18, 12);
    }
    Segment(id, {-34, -56}, {-7, -21}, 1.1f, shade);
    Segment(id, {-7, -21}, {8, -33}, 1.2f, shade);
    for (int i = 0; i < 5; ++i)
    {
        block(id, -37.f + i * 16, 4.f + i % 2 * 8, 10, 5 + i % 3 * 4.f, 6);
    }

    id = ModelId::Arch;
    block(id, -55, -13, 20, 100, 14);
    block(id, 25, 19, 20, 84, 14);
    for (int i = 0; i < 4; ++i)
    {
        block(id, -34.f + i * 16, -104.f + i * i * 2, 15, 15, 12);
    }
    Triangle(id, {-36, -106}, {-12, -137}, {0, -100}, light, Material::Stone);
    Segment(id, {-42, -100}, {-42, -22}, 1.2f, light);
    Segment(id, {36, -67}, {36, 11}, 1.2f, light);
    Quad(id, {-34, -98}, {-22, -94}, {-25, -49}, {-30, -61}, Color(.29f, .065f, .09f),
         Material::Cloth);
    Segment(id, {-26, -87}, {-27, -69}, 1.1f, bone);

    id = ModelId::Statue;
    block(id, -23, 0, 31, 14, 23);
    block(id, -16, -12, 20, 13, 16);
    Quad(id, {-12, -31}, {16, -32}, {7, -82}, {-3, -84}, shade, Material::Stone);
    Triangle(id, {-3, -82}, {-12, -31}, {2, -37}, light, Material::Stone);
    Segment(id, {-4, -79}, {-23, -62}, 4.5f, stone);
    Segment(id, {-23, -62}, {-32, -71}, 3.5f, light);
    Segment(id, {10, -80}, {23, -62}, 4, stone);
    Ellipse(id, 2, -91, 10, 12, shade);
    Quad(id, {-7, -102}, {8, -106}, {11, -89}, {-6, -87}, light, Material::Stone);
    Segment(id, {-3, -98}, {6, -91}, 1.7f, shade);
    for (int i = 0; i < 4; ++i)
    {
        Segment(id, {8, -48}, {23.f + i * 5, -75.f - i % 2 * 13}, 1.8f, bone);
        Ellipse(id, 23.f + i * 5, -75.f - i % 2 * 13, 3, 4, blood);
    }

    id = ModelId::Grave;
    block(id, -12, -1, 16, 8, 12);
    Quad(id, {-10, -4}, {10, 3}, {9, -39}, {-9, -44}, shade, Material::Stone);
    Quad(id, {-9, -44}, {9, -39}, {14, -31}, {-12, -34}, light, Material::Stone);
    Quad(id, {-7, -30}, {6, -26}, {6, -7}, {-7, -12}, stone, Material::Stone);
    Segment(id, {0, -28}, {0, -14}, 1, bone);
    Segment(id, {-5, -24}, {5, -21}, 1, bone);
    Segment(id, {6, -39}, {1, -33}, 1, shade);

    id = ModelId::Cart;
    for (float x : {-26.f, 25.f})
    {
        Ellipse(id, x, -6, 13, 15, shade);
        Ellipse(id, x, -6, 9, 11, rust);
        Ellipse(id, x, -6, 6, 8, Color(.095f, .09f, .07f));
        Segment(id, {x - 9, -7}, {x + 9, -5}, 1.4f, bone);
        Segment(id, {x, -16}, {x, 4}, 1.4f, bone);
    }
    for (int i = 0; i < 5; ++i)
    {
        float y = -27.f + i * 5;
        Quad(id, {-29, y}, {19, y + 5}, {29, y}, {-19, y - 5},
             i % 2 ? rust : Color(.35f, .27f, .18f), Material::Wood);
    }
    Segment(id, {-22, -15}, {-46, -1}, 2.4f, rust, 0, Material::Wood);
    Segment(id, {18, -12}, {0, 3}, 2.4f, rust, 0, Material::Wood);
    Ellipse(id, 0, -31, 23, 8, Color(.21f, .23f, .20f));
    Ellipse(id, -17, -33, 5, 5, bone);
    for (int i = 0; i < 3; ++i)
    {
        Segment(id, {-9.f + i * 8, -37}, {-6.f + i * 8, -25}, 1, shade);
    }

    id = ModelId::FungalColony;
    for (int i = 0; i < 9; ++i)
    {
        float x = -28.f + i * 7, y = (i % 3) * -8.f;
        float height = 17 + (i * 7 % 5) * 9.f;
        Segment(id, {x, 0}, {x + (i % 2 ? 7 : -6), y - height}, 2 + i % 3, bone, 0,
                Material::Organic);
        float topX = x + (i % 2 ? 7 : -6);
        Ellipse(id, topX, y - height, 8 + i % 3 * 3.f, 5, shade);
        Ellipse(id, topX - 1, y - height - 2, 8 + i % 3 * 3.f, 4, blood);
        Ellipse(id, topX - 3, y - height - 3, 2, 1, Color(.88f, .23f, .23f));
        Segment(id, {x, -2}, {x + 13, 6}, 1.4f, blood, 0, Material::Organic);
    }

    id = ModelId::Debris;
    for (int i = 0; i < 6; ++i)
    {
        block(id, -22.f + i * 8, -8.f + i % 3 * 6, 5 + i % 3 * 2.f, 3 + i % 2 * 4.f, 5);
    }
    Segment(id, {-24, 3}, {21, -7}, 2, rust, 0, Material::Wood);
    Segment(id, {-11, -13}, {14, 8}, 1.5f, bone);

    id = ModelId::Corpse;
    Ellipse(id, 0, 2, 28, 8, Color(.19f, .035f, .055f, .65f));
    Segment(id, {-6, -2}, {17, -5}, 7, Color(.23f, .25f, .22f));
    Ellipse(id, -17, -3, 7, 5, bone);
    Segment(id, {-5, -3}, {-9, 7}, 2, bone);
    Segment(id, {11, -3}, {27, 4}, 2.5f, rust);
    Segment(id, {11, -2}, {25, -8}, 2.5f, rust);
    for (int i = 0; i < 4; ++i)
    {
        Segment(id, {-6.f + i * 5, -7}, {-4.f + i * 5, -1}, 1, bone);
    }

    id = ModelId::Fence;
    for (int i = 0; i < 5; ++i)
    {
        float x = -28 + i * 14.f, y = x * .5f;
        Segment(id, {x, y}, {x, y - 38 - i % 2 * 6.f}, 1.5f, shade);
        Triangle(id, {x - 4, y - 35}, {x, y - 47}, {x + 4, y - 35}, rust);
    }
    Segment(id, {-30, -30}, {30, 0}, 1.5f, rust);
    Segment(id, {-30, -44}, {30, -14}, 1.5f, shade);

    // Add readable bevels, bands, hanging seals and candles to the original landmarks.
    for (int i = 0; i < 4; ++i)
    {
        Segment(ModelId::Pillar, {-12, -10.f - i * 12}, {12, -10.f - i * 12}, 1, shade);
    }
    Segment(ModelId::Pillar, {-10, -51}, {-10, -12}, 1.8f, light);
    for (float x : {-14.f, 14.f})
    {
        Segment(ModelId::Brazier, {x * .45f, -8}, {x, -29}, 1.7f, rust);
        Segment(ModelId::Shrine, {x, -30}, {x, -41}, 2, bone);
        Ellipse(ModelId::Shrine, x, -43, 1.6f, 3, Color(1.3f, .72f, .29f));
    }
    Quad(ModelId::Shrine, {-9, -24}, {9, -24}, {8, -4}, {-7, -9}, blood, Material::Cloth);
    Segment(ModelId::Shrine, {0, -22}, {0, -10}, 1, bone);
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
    const auto& model = models[index];
    bool creature = id == ModelId::Infected || id == ModelId::Brute || id == ModelId::Crawler;
    float swing = std::sin(phase) * movement;
    float strike = std::sin(attack * 3.14159265f);
    bool pilgrim = id == ModelId::Pilgrim;
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
