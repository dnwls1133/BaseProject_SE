#include "stdafx.h"
#include "MeshCache3D.h"
#include "ContentData.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
constexpr std::uint32_t CacheMagic = 0x334D5342, CacheVersion3D = 3;
}

bool MeshCache3D::ReadCache(const std::string& path)
{
    PROFILE_SCOPE(CacheRead);
    std::ifstream in(path, std::ios::binary);
    std::uint32_t header[4] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!in || header[0] != CacheMagic || header[1] != CacheVersion3D || header[2] != hash ||
        header[3] != names.size())
    {
        return false;
    }
    std::vector<Model> next(names.size());
    for (auto& m : next)
    {
        std::uint32_t counts[2] = {};
        in.read(reinterpret_cast<char*>(counts), sizeof(counts));
        if (!in || counts[0] < 1 || counts[0] > 16 || counts[1] < 1 || counts[1] > 512)
        {
            return false;
        }
        m.joints.resize(counts[0]);
        m.parts.resize(counts[1]);
        in.read(reinterpret_cast<char*>(m.joints.data()), m.joints.size() * sizeof(Joint));
        in.read(reinterpret_cast<char*>(m.parts.data()), m.parts.size() * sizeof(Part));
        if (!in)
        {
            return false;
        }
        for (std::size_t i = 0; i < m.joints.size(); ++i)
        {
            const auto& j = m.joints[i];
            if (j.parent < -1 || j.parent >= static_cast<int>(i) ||
                !std::isfinite(Dot(j.pivot, j.pivot)))
            {
                return false;
            }
        }
        for (const auto& p : m.parts)
        {
            for (float value : p.local.m)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            if (p.primitive < 0 || p.primitive > 3 || p.joint < 0 ||
                p.joint >= static_cast<int>(m.joints.size()) || p.material < 0 || p.material > 12 ||
                !std::isfinite(Dot(p.position, p.position)) ||
                !std::isfinite(Dot(p.rotation, p.rotation)) ||
                !std::isfinite(Dot(p.size, p.size)) || p.size.x <= 0 || p.size.y <= 0 ||
                p.size.z <= 0 || !std::isfinite(p.color.r + p.color.g + p.color.b + p.color.a))
            {
                return false;
            }
        }
    }
    if (in.peek() != std::char_traits<char>::eof())
    {
        return false;
    }
    models = std::move(next);
    return true;
}

void MeshCache3D::WriteCache(const std::string& path) const
{
    PROFILE_SCOPE(CacheWrite);
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    std::uint32_t header[4] = {CacheMagic, CacheVersion3D, hash,
                               static_cast<std::uint32_t>(models.size())};
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (const auto& m : models)
    {
        std::uint32_t counts[2] = {static_cast<std::uint32_t>(m.joints.size()),
                                   static_cast<std::uint32_t>(m.parts.size())};
        out.write(reinterpret_cast<const char*>(counts), sizeof(counts));
        out.write(reinterpret_cast<const char*>(m.joints.data()), m.joints.size() * sizeof(Joint));
        out.write(reinterpret_cast<const char*>(m.parts.data()), m.parts.size() * sizeof(Part));
    }
    if (!out)
    {
        Profile::Event("model_3d_cache_write_failed", path);
    }
}

void MeshCache3D::Load(const std::string& root)
{
    if (loadedRoot == root)
    {
        return;
    }
    using ContentData::Require;
    MeshCache3D next;
    const auto catalog = ContentData::Path(root, "models3d.catalog");
    std::vector<std::pair<std::string, std::string>> files;
    next.hash = 2166136261u;
    auto digest = [&](const std::string& s)
    {
        for (unsigned char c : s)
        {
            next.hash = (next.hash ^ c) * 16777619u;
        }
    };
    const std::string text = ContentData::Read(catalog);
    digest(text);
    ContentData::Rows(
        catalog, text,
        [&](const std::string& command, ContentData::Row& row)
        {
            Require(command == "model", "expected 3D model record");
            const std::string name = row.Word(), path = ContentData::Path(root, row.Word());
            Require(next.Find(name) < 0 && next.names.size() < 256, "duplicate/too many 3D models");
            const auto source = ContentData::Read(path);
            digest(source);
            next.names.push_back(name);
            files.push_back({path, source});
        });
    Require(!files.empty(), "empty 3D catalog");
    std::size_t legacyIndex = 0;
    ContentData::Load(ContentData::Path(root, "models.catalog"),
                      [&](const std::string& command, ContentData::Row& row)
                      {
                          Require(command == "model", "expected model catalog record");
                          const std::string name = row.Word();
                          row.Word();
                          row.Word();
                          Require(legacyIndex < next.names.size() &&
                                      next.names[legacyIndex] == name,
                                  "3D catalog must preserve model IDs: " + name);
                          ++legacyIndex;
                      });
    const auto cache = ContentData::Path(root, "Cache/world_models3d.bin");
    if (!next.ReadCache(cache))
    {
        PROFILE_SCOPE(CacheBuild);
        Profile::Add(Profile::Metric::CacheMiss);
        for (const auto& file : files)
        {
            Model model;
            ContentData::Rows(
                file.first, file.second,
                [&](const std::string& command, ContentData::Row& row)
                {
                    auto vector = [&](float lo, float hi)
                    {
                        Vec3 v;
                        v.x = row.Number(lo, hi);
                        v.y = row.Number(lo, hi);
                        v.z = row.Number(lo, hi);
                        return v;
                    };
                    if (command == "joint")
                    {
                        Require(model.joints.size() < 16, "too many joints");
                        Joint j;
                        j.parent = row.Integer(-1, static_cast<int>(model.joints.size()) - 1);
                        j.pivot = vector(-12, 12);
                        model.joints.push_back(j);
                    }
                    else if (command == "part")
                    {
                        Require(!model.joints.empty() && model.parts.size() < 512,
                                "joint required/too many parts");
                        Part p;
                        p.primitive = row.Integer(0, 3);
                        p.joint = row.Integer(0, static_cast<int>(model.joints.size()) - 1);
                        p.material = row.Integer(0, 12);
                        p.position = vector(-16, 16);
                        p.size = vector(.005f, 32);
                        p.rotation = vector(-180, 180);
                        p.color.r = row.Number(0, 3);
                        p.color.g = row.Number(0, 3);
                        p.color.b = row.Number(0, 3);
                        constexpr float radians = .0174532925f;
                        p.local = Mat4::Translation(p.position) *
                                  Mat4::Rotation({0, 1, 0}, p.rotation.y * radians) *
                                  Mat4::Rotation({1, 0, 0}, p.rotation.x * radians) *
                                  Mat4::Rotation({0, 0, 1}, p.rotation.z * radians) *
                                  Mat4::Scale(p.size);
                        model.parts.push_back(p);
                    }
                    else
                    {
                        Require(false, "unknown 3D model record");
                    }
                });
            Require(!model.joints.empty() && !model.parts.empty(), "empty 3D model: " + file.first);
            next.models.push_back(std::move(model));
        }
        next.WriteCache(cache);
    }
    else
    {
        Profile::Add(Profile::Metric::CacheHit);
    }
    next.loadedRoot = root;
    *this = std::move(next);
}

int MeshCache3D::Find(const std::string& name) const
{
    auto it = std::find(names.begin(), names.end(), name);
    return it == names.end() ? -1 : static_cast<int>(it - names.begin());
}

void MeshCache3D::Bounds(int id, Vec3& minimum, Vec3& maximum) const
{
    minimum = {-2, -1, -2};
    maximum = {2, 6, 2};
    if (id < 0 || id >= static_cast<int>(models.size()))
    {
        return;
    }
    for (const auto& part : models[id].parts)
    {
        for (int i = 0; i < 8; ++i)
        {
            const Vec3 p =
                part.local.Transform({i & 1 ? .5f : -.5f, i & 2 ? .5f : -.5f, i & 4 ? .5f : -.5f});
            minimum = {std::min(minimum.x, p.x), std::min(minimum.y, p.y),
                       std::min(minimum.z, p.z)};
            maximum = {std::max(maximum.x, p.x), std::max(maximum.y, p.y),
                       std::max(maximum.z, p.z)};
        }
    }
}

void MeshCache3D::Draw(Renderer& r, int id, Vec3 position, float yaw, float scale,
                       const Pose3D& pose, Color tint) const
{
    if (id < 0 || id >= static_cast<int>(models.size()))
    {
        return;
    }
    PROFILE_SCOPE(ModelDraw);
    Profile::Add(Profile::Metric::ModelCalls);
    const auto& model = models[id];
    if (pose.activity == 5)
    {
        position = position + Mat4::Rotation({0, 1, 0}, yaw).Transform({0, .18f, -.8f});
    }
    if (model.joints.size() > 1 && pose.movement < .01f)
    {
        position.y += std::sin(pose.time * 2) * .013f;
    }
    Mat4 base = Mat4::Translation(position) * Mat4::Rotation({0, 1, 0}, yaw) *
                Mat4::Rotation({1, 0, 0}, pose.activity == 5 ? 1.45f : 0) *
                Mat4::Rotation({0, 0, 1}, pose.collapse * 1.52f) *
                Mat4::Scale({scale, scale, scale});
    Mat4 joints[16];
    const bool crawler = names[id] == "Crawler";
    for (std::size_t i = 0; i < model.joints.size(); ++i)
    {
        const auto& joint = model.joints[i];
        float angle = 0;
        if (i == 1 || i == 2)
        {
            angle = std::sin(pose.phase + (i == 2 ? 3.14159f : 0)) * pose.movement * .48f;
        }
        if (i == 3 || i == 4)
        {
            angle = -std::sin(pose.phase + (i == 4 ? 3.14159f : 0)) * pose.movement * .38f;
            if (pose.activity == 1)
            {
                angle = -.7f + std::sin(pose.time * 4) * .45f;
            }
            if (pose.activity == 2)
            {
                angle = -1.1f;
            }
            if (pose.activity == 3)
            {
                angle = -.85f + std::sin(pose.time * 2) * .12f;
            }
            if (pose.activity == 4 && i == 4)
            {
                angle = -.65f + std::sin(pose.time * 3) * .18f;
            }
            if (pose.attack > 0 && i == 4)
            {
                angle = -1.7f + std::sin(pose.attack * 3.14159f) * 2.3f;
            }
        }
        if (i == 5)
        {
            angle = std::sin(pose.time * 1.7f) * .05f + pose.movement * .14f;
        }
        Vec3 axis{1, 0, 0};
        if (crawler && i > 0)
        {
            angle = std::sin(pose.phase + float(i) * 2.1f) * pose.movement * .4f;
            axis = {0, 0, 1};
        }
        Mat4 local = Mat4::Translation(joint.pivot) * Mat4::Rotation(axis, angle) *
                     Mat4::Translation(joint.pivot * -1);
        joints[i] = (joint.parent < 0 ? base : joints[joint.parent]) * local;
    }
    for (const auto& p : model.parts)
    {
        Mat4 transform = joints[p.joint] * p.local;
        Color c{p.color.r * tint.r + pose.flash * .3f, p.color.g * tint.g + pose.flash * .15f,
                p.color.b * tint.b, tint.a};
        r.Submit3D(p.primitive, transform, c, static_cast<Material>(p.material));
    }
}

std::vector<MeshCache3D::PlacedPart> MeshCache3D::Place(int id, Vec3 position, float yaw,
                                                        float scale) const
{
    std::vector<PlacedPart> result;
    if (id < 0 || id >= int(models.size()) || models[id].joints.size() != 1)
    {
        return result;
    }
    const Mat4 root = Mat4::Translation(position) * Mat4::Rotation({0, 1, 0}, yaw) *
                      Mat4::Scale({scale, scale, scale});
    for (const auto& part : models[id].parts)
    {
        result.push_back({part.primitive, part.material, root * part.local, part.color});
    }
    return result;
}

void MeshCache3D::DrawPlaced(Renderer& renderer, const std::vector<PlacedPart>& parts,
                             Color tint) const
{
    PROFILE_SCOPE(ModelDraw);
    Profile::Add(Profile::Metric::ModelCalls);
    Profile::Add(Profile::Metric::StaticModelParts, double(parts.size()));
    for (const auto& part : parts)
    {
        Color color(part.color.r * tint.r, part.color.g * tint.g, part.color.b * tint.b, tint.a);
        renderer.Submit3D(part.primitive, part.transform, color,
                          static_cast<Material>(part.material));
    }
}
