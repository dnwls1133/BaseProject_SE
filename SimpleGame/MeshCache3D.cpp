#include "stdafx.h"
#include "MeshCache3D.h"
#include "ResourceLibrary3D.h"
#include <filesystem>
#include <unordered_map>

void MeshCache3D::Load(const std::string& root)
{
    static std::unordered_map<std::string, std::weak_ptr<ResourceLibrary3D>> libraries;
    const auto key = std::filesystem::absolute(root).lexically_normal().string();
    resources = libraries[key].lock();
    if (resources)
    {
        Profile::Add(Profile::Metric::CacheHit);
        return;
    }
    auto candidate = std::make_shared<ResourceLibrary3D>();
    candidate->Load(root);
    resources = std::move(candidate);
    libraries[key] = resources;
    Profile::Add(Profile::Metric::CacheMiss);
}

int MeshCache3D::Find(const std::string& name) const
{
    return resources ? resources->Find(name) : -1;
}

int MeshCache3D::PropModel(const std::string& id, int fallback) const
{
    return resources->PropModel(id, fallback);
}

void MeshCache3D::Bounds(int id, Vec3& minimum, Vec3& maximum) const
{
    resources->Bounds(id, minimum, maximum);
}

std::vector<MeshCache3D::PlacedPart> MeshCache3D::Place(int id, Vec3 position, float yaw,
                                                        float scale) const
{
    std::vector<PlacedPart> result;
    for (const auto& part : resources->Place(id, position, yaw, scale))
    {
        result.push_back({part.geometry, part.transform, Color(1, 1, 1)});
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
        renderer.Submit3D(part.geometry, part.transform, tint);
    }
}

void MeshCache3D::Draw(Renderer& renderer, int id, Vec3 position, float yaw, float scale,
                       const Pose3D& pose, Color tint) const
{
    PROFILE_SCOPE(ModelDraw);
    Profile::Add(Profile::Metric::ModelCalls);
    resources->Draw(renderer, id, position, yaw, scale, pose, tint);
}

std::shared_ptr<SurfaceSet3D> MeshCache3D::Surfaces() const
{
    return resources->Surfaces();
}
