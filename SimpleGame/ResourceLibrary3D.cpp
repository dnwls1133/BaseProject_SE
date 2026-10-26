#include "stdafx.h"
#include "ResourceLibrary3D.h"
#include "MeshCache3D.h"
#include "ContentData.h"
#include "Profiler.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>

namespace
{
template <class T> T Read(std::ifstream& in)
{
    static_assert(std::is_trivially_copyable<T>::value, "binary record must be trivial");
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    ContentData::Require(bool(in), "incomplete prepared GLB record");
    return value;
}

std::string ReadName(std::ifstream& in)
{
    const auto size = Read<std::uint32_t>(in);
    ContentData::Require(size > 0 && size <= 128, "invalid prepared animation name");
    std::string text(size, '\0');
    in.read(text.data(), size);
    ContentData::Require(bool(in), "incomplete animation name");
    return text;
}
} // namespace

ResourceLibrary3D::Asset ResourceLibrary3D::ReadAsset(const std::string& path,
                                                      const std::string& id) const
{
    std::ifstream in(path, std::ios::binary);
    ContentData::Require(bool(in), "cannot open prepared resource: " + path);
    ContentData::Require(Read<std::uint32_t>(in) == 0x48534D52 && Read<std::uint32_t>(in) == 1,
                         "unsupported prepared GLB: " + path);
    const auto vertices = Read<std::uint32_t>(in);
    const auto matrices = Read<std::uint32_t>(in);
    const auto clips = Read<std::uint32_t>(in);
    const auto bones = Read<std::uint32_t>(in);
    ContentData::Require(vertices > 0 && vertices <= 2000000 && vertices % 3 == 0 && matrices > 0 &&
                             matrices <= 1000000 && clips > 0 && clips <= 64 && bones > 0 &&
                             bones <= 256,
                         "invalid prepared GLB counts: " + path);
    Asset asset;
    asset.id = id;
    asset.bones = int(bones);
    asset.geometry = std::make_shared<Geometry3D>();
    asset.geometry->minimum = Read<Vec3>(in);
    asset.geometry->maximum = Read<Vec3>(in);
    asset.geometry->surfaces = surfaces;
    for (unsigned i = 0; i < clips; ++i)
    {
        Clip clip;
        clip.name = ReadName(in);
        clip.start = Read<std::uint32_t>(in);
        clip.frames = Read<std::uint32_t>(in);
        clip.duration = Read<float>(in);
        ContentData::Require(clip.frames > 0 && clip.frames <= 10000 &&
                                 std::isfinite(clip.duration) && clip.duration >= 0 &&
                                 std::uint64_t(clip.start) + std::uint64_t(clip.frames) * bones <=
                                     matrices,
                             "invalid prepared animation: " + path);
        asset.clips.push_back(std::move(clip));
    }
    static_assert(sizeof(Vertex3D) == 23 * sizeof(float), "prepared vertex layout changed");
    asset.geometry->vertices.resize(vertices);
    in.read(reinterpret_cast<char*>(asset.geometry->vertices.data()), vertices * sizeof(Vertex3D));
    asset.geometry->skinFrames.resize(matrices);
    in.read(reinterpret_cast<char*>(asset.geometry->skinFrames.data()), matrices * sizeof(Mat4));
    ContentData::Require(bool(in) && in.peek() == std::char_traits<char>::eof(),
                         "incomplete or trailing prepared resource data: " + path);
    for (const auto& vertex : asset.geometry->vertices)
    {
        ContentData::Require(std::isfinite(Dot(vertex.position, vertex.position)) &&
                                 std::isfinite(Dot(vertex.normal, vertex.normal)) &&
                                 vertex.uvLayer.z >= 0 &&
                                 vertex.uvLayer.z < float(surfaces->entries.size()),
                             "invalid prepared vertex: " + path);
        float weight = 0;
        for (int i = 0; i < 4; ++i)
        {
            ContentData::Require(vertex.joints[i] >= 0 && vertex.joints[i] < float(bones) &&
                                     std::isfinite(vertex.weights[i]) && vertex.weights[i] >= 0,
                                 "invalid prepared skin influence: " + path);
            weight += vertex.weights[i];
        }
        ContentData::Require(std::abs(weight - 1) < .01f, "invalid skin weight sum: " + path);
    }
    for (const auto& matrix : asset.geometry->skinFrames)
    {
        for (float value : matrix.m)
        {
            ContentData::Require(std::isfinite(value), "invalid skin matrix: " + path);
        }
    }
    Profile::Add(Profile::Metric::ContentBytes,
                 double(vertices) * sizeof(Vertex3D) + double(matrices) * sizeof(Mat4));
    Profile::Event("resource_mesh_loaded", "asset=" + id + " vertices=" + std::to_string(vertices) +
                                               " bones=" + std::to_string(bones) +
                                               " palette_matrices=" + std::to_string(matrices));
    return asset;
}

void ResourceLibrary3D::Load(const std::string& root)
{
    PROFILE_SCOPE(CacheRead);
    if (loadedRoot == root)
    {
        return;
    }
    using ContentData::Require;
    ResourceLibrary3D next;
    next.surfaces = std::make_shared<SurfaceSet3D>();
    ContentData::Load(ContentData::Path(root, "Resources/surfaces.catalog"),
                      [&](const std::string& command, ContentData::Row& row)
                      {
                          Require(command == "surface" && next.surfaces->entries.size() < 64,
                                  "invalid resource surface catalog");
                          const std::string name = row.Word();
                          SurfaceSet3D::Entry entry;
                          entry.name = name;
                          entry.baseColor = ContentData::Path(root, row.Word());
                          entry.normal = ContentData::Path(root, row.Word());
                          entry.roughness = ContentData::Path(root, row.Word());
                          for (const auto& path : {entry.baseColor, entry.normal, entry.roughness})
                          {
                              Require(std::filesystem::is_regular_file(path),
                                      "missing resource PNG: " + path);
                          }
                          next.surfaces->entries.push_back(std::move(entry));
                      });
    Require(!next.surfaces->entries.empty(), "empty resource surface catalog");
    auto vector = [](ContentData::Row& row)
    {
        return Vec3{row.Number(-100, 100), row.Number(-100, 100), row.Number(-100, 100)};
    };
    ContentData::Load(
        ContentData::Path(root, "Resources/models.catalog"),
        [&](const std::string& command, ContentData::Row& row)
        {
            if (command == "asset")
            {
                const std::string id = row.Word();
                Require(next.assets.size() < 512 &&
                            std::none_of(next.assets.begin(), next.assets.end(),
                                         [&](const Asset& a)
                                         {
                                             return a.id == id;
                                         }),
                        "duplicate resource asset");
                next.assets.push_back(next.ReadAsset(ContentData::Path(root, row.Word()), id));
            }
            else if (command == "gait")
            {
                const std::string id = row.Word();
                const auto asset = std::find_if(next.assets.begin(), next.assets.end(),
                                                [&](const Asset& a)
                                                {
                                                    return a.id == id;
                                                });
                Require(asset != next.assets.end(), "unknown gait resource");
                asset->walkDistance = row.Number(.01f, 100);
                asset->runDistance = row.Number(.01f, 100);
            }
            else if (command == "prefab")
            {
                Prefab prefab;
                prefab.name = row.Word();
                Require(next.Find(prefab.name) < 0 && next.prefabs.size() < 256,
                        "duplicate/too many resource prefabs");
                const float yaw = row.Number(-360, 360) * .0174532925f;
                const float scale = row.Number(.01f, 20);
                prefab.root = Mat4::Rotation({0, 1, 0}, yaw) * Mat4::Scale({scale, scale, scale});
                next.prefabs.push_back(std::move(prefab));
            }
            else if (command == "part")
            {
                const int prefab = next.Find(row.Word());
                const std::string id = row.Word();
                const auto asset = std::find_if(next.assets.begin(), next.assets.end(),
                                                [&](const Asset& a)
                                                {
                                                    return a.id == id;
                                                });
                Require(prefab >= 0 && asset != next.assets.end(), "unknown resource prefab part");
                const Vec3 position = vector(row);
                const float yaw = row.Number(-360, 360) * .0174532925f;
                const Vec3 scale = vector(row);
                Require(scale.x > 0 && scale.y > 0 && scale.z > 0 &&
                            next.prefabs[prefab].parts.size() < 128,
                        "invalid prefab scale/part count");
                Part part;
                part.asset = int(asset - next.assets.begin());
                part.transform = Mat4::Translation(position) * Mat4::Rotation({0, 1, 0}, yaw) *
                                 Mat4::Scale(scale);
                next.prefabs[prefab].parts.push_back(part);
            }
            else if (command == "bind")
            {
                const std::string prop = row.Word();
                const int model = next.Find(row.Word());
                Require(model >= 0 && next.propBindings.emplace(prop, model).second,
                        "unknown/duplicate resource prop binding");
            }
            else if (command == "freeze")
            {
                const int prefab = next.Find(row.Word());
                Require(prefab >= 0, "unknown frozen prefab");
                next.prefabs[prefab].frozenClip = row.Word();
                next.prefabs[prefab].frozenTime = row.Number(0, 1);
            }
            else
            {
                Require(false, "unknown resource model record: " + command);
            }
        });
    std::size_t index = 0;
    ContentData::Load(ContentData::Path(root, "models3d.catalog"),
                      [&](const std::string& command, ContentData::Row& row)
                      {
                          Require(command == "model", "invalid original model catalog");
                          const std::string name = row.Word();
                          row.Word();
                          Require(index < next.prefabs.size() && next.prefabs[index].name == name,
                                  "resource prefabs must preserve model ID: " + name);
                          ++index;
                      });
    next.loadedRoot = root;
    *this = std::move(next);
}

int ResourceLibrary3D::PropModel(const std::string& id, int fallback) const
{
    const auto found = propBindings.find(id);
    return found == propBindings.end() ? fallback : found->second;
}

int ResourceLibrary3D::Find(const std::string& name) const
{
    const auto it = std::find_if(prefabs.begin(), prefabs.end(),
                                 [&](const Prefab& prefab)
                                 {
                                     return prefab.name == name;
                                 });
    return it == prefabs.end() ? -1 : int(it - prefabs.begin());
}

Vec3 ResourceLibrary3D::Animation(const Asset& asset, const Prefab& prefab, const Pose3D& pose,
                                  float scale) const
{
    if (asset.clips.size() == 1)
    {
        return {-1, 0, 0};
    }
    std::string name = "Idle";
    float phase = -1;
    if (!prefab.frozenClip.empty())
    {
        name = prefab.frozenClip;
        phase = prefab.frozenTime;
    }
    else if (pose.collapse > 0)
    {
        name = "Death";
        phase = std::clamp(pose.collapse, 0.f, 1.f);
    }
    else if (pose.activity == 5)
    {
        name = "Death";
        phase = 1;
    }
    else if (pose.attack > 0)
    {
        name = pose.attackStyle == 4 ? "Jump" : "Attack";
        phase = pose.attack < .45f ? pose.attack / .45f * .62f
                                   : .62f + (pose.attack - .45f) / .55f * .38f;
    }
    else if (pose.flash > .01f)
    {
        name = "Hit";
        phase = 1 - std::clamp(pose.flash, 0.f, 1.f);
    }
    else if (pose.movement > .02f)
    {
        name = pose.speed > 2 ? "Run" : "Walk";
        const float prefabScale =
            std::sqrt(prefab.root.m[0] * prefab.root.m[0] + prefab.root.m[1] * prefab.root.m[1] +
                      prefab.root.m[2] * prefab.root.m[2]);
        const float stride =
            (name == "Run" ? asset.runDistance : asset.walkDistance) * prefabScale * scale;
        const float travel = pose.distance >= 0 ? pose.distance : pose.phase / 5;
        phase = travel / std::max(.01f, stride);
        phase -= std::floor(phase);
    }
    else if (pose.activity == 1 || pose.activity == 3)
    {
        name = "Attack";
    }
    auto clip = std::find_if(asset.clips.begin(), asset.clips.end(),
                             [&](const Clip& c)
                             {
                                 return c.name == name;
                             });
    if (clip == asset.clips.end())
    {
        clip = std::find_if(asset.clips.begin(), asset.clips.end(),
                            [](const Clip& c)
                            {
                                return c.name == "Wind" || c.name == "Pulse";
                            });
    }
    if (clip == asset.clips.end())
    {
        clip = asset.clips.begin();
    }
    if (phase < 0)
    {
        phase = clip->duration > 0
                    ? std::fmod(std::max(0.f, pose.time), clip->duration) / clip->duration
                    : 0;
    }
    const float frame = std::clamp(phase, 0.f, 1.f) * (clip->frames - 1);
    const auto first = std::uint32_t(frame);
    const auto second = std::min(first + 1, clip->frames - 1);
    return {float(clip->start + first * asset.bones), float(clip->start + second * asset.bones),
            frame - first};
}

void ResourceLibrary3D::Bounds(int id, Vec3& minimum, Vec3& maximum) const
{
    minimum = {0, 0, 0};
    maximum = {0, 0, 0};
    if (id < 0 || id >= int(prefabs.size()))
    {
        return;
    }
    const auto& prefab = prefabs[id];
    for (const auto& part : prefab.parts)
    {
        const auto& geometry = assets[part.asset].geometry;
        const auto transform = prefab.root * part.transform;
        for (int i = 0; i < 8; ++i)
        {
            const Vec3 point =
                transform.Transform({i & 1 ? geometry->maximum.x : geometry->minimum.x,
                                     i & 2 ? geometry->maximum.y : geometry->minimum.y,
                                     i & 4 ? geometry->maximum.z : geometry->minimum.z});
            minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y),
                       std::min(minimum.z, point.z)};
            maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y),
                       std::max(maximum.z, point.z)};
        }
    }
    // Animated limbs, death poses and visual jump height remain conservative render candidates.
    minimum = minimum - Vec3{1, .2f, 1};
    maximum = maximum + Vec3{1, 1.5f, 1};
}

std::vector<ResourceLibrary3D::Placement> ResourceLibrary3D::Place(int id, Vec3 position, float yaw,
                                                                   float scale) const
{
    std::vector<Placement> result;
    if (id < 0 || id >= int(prefabs.size()))
    {
        return result;
    }
    const auto& prefab = prefabs[id];
    for (const auto& part : prefab.parts)
    {
        if (assets[part.asset].clips.size() > 1)
        {
            return {};
        }
    }
    const auto root = Mat4::Translation(position) * Mat4::Rotation({0, 1, 0}, yaw) *
                      Mat4::Scale({scale, scale, scale}) * prefab.root;
    for (const auto& part : prefab.parts)
    {
        result.push_back({assets[part.asset].geometry, root * part.transform});
    }
    return result;
}

void ResourceLibrary3D::Draw(Renderer& renderer, int id, Vec3 position, float yaw, float scale,
                             const Pose3D& pose, Color tint) const
{
    if (id < 0 || id >= int(prefabs.size()))
    {
        return;
    }
    const auto& prefab = prefabs[id];
    if (pose.activity == 5)
    {
        position.y += .35f;
    }
    const auto root = Mat4::Translation(position) * Mat4::Rotation({0, 1, 0}, yaw) *
                      Mat4::Scale({scale, scale, scale}) * prefab.root;
    tint.r += pose.flash * .3f;
    tint.g += pose.flash * .15f;
    for (const auto& part : prefab.parts)
    {
        const auto& asset = assets[part.asset];
        renderer.Submit3D(asset.geometry, root * part.transform, tint,
                          Animation(asset, prefab, pose, scale));
    }
}
