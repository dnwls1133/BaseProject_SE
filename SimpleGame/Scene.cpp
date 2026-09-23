#include "stdafx.h"
#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
SceneRect Intersection(SceneRect a, SceneRect b)
{
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
            std::min(a.bottom, b.bottom)};
}

bool Empty(SceneRect rect)
{
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

SceneRect Union(SceneRect a, SceneRect b)
{
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
            std::max(a.bottom, b.bottom)};
}
} // namespace

Scene::Node::Node(Scene* owner, std::string name, bool screenSpace)
    : owner(owner), name(std::move(name)), screenSpace(screenSpace)
{
}

Scene::Scene() : root(this, "Root", false)
{
}

Scene::Node& Scene::Root()
{
    return root;
}

Scene::Node& Scene::CreateGroup(Node& parent, const std::string& name, bool screenSpace)
{
    if (updating || parent.owner != this)
    {
        throw std::logic_error("Create scene groups outside traversal, using this scene's parent");
    }
    std::unique_ptr<Node> node(new Node(this, name, screenSpace || parent.screenSpace));
    node->parent = &parent;
    parent.children.push_back(std::move(node));
    return *parent.children.back();
}

void Scene::Attach(Actor& actor, Node& parent)
{
    if (parent.owner != this)
    {
        throw std::logic_error("Actor parent belongs to another scene");
    }
    if (updating)
    {
        pending.push_back({&actor, &parent});
    }
    else
    {
        AttachNow(actor, parent);
    }
}

void Scene::AttachNow(Actor& actor, Node& parent)
{
    if (actor.IsDestroyed() || attached.find(&actor) != attached.end())
    {
        return;
    }
    std::unique_ptr<Node> node(new Node(this, "Actor", parent.screenSpace));
    node->parent = &parent;
    node->actor = &actor;
    attached.emplace(&actor, node.get());
    parent.children.push_back(std::move(node));
}

void Scene::Clear()
{
    if (updating)
    {
        throw std::logic_error("Clear a scene outside update traversal");
    }
    // Detach before the owning level erases or reallocates its actor containers.
    for (const auto& entry : attached)
    {
        entry.first->hierarchyActive = true;
        entry.first->hierarchyVisible = true;
    }
    attached.clear();
    pending.clear();
    renderList.clear();
    root.children.clear();
    worldList.clear();
    root.active = root.visible = true;
    root.clipped = false;
    statistics = {};
}

void Scene::ApplyStates(Node& node, bool active, bool visible)
{
    active = active && node.active;
    visible = visible && node.visible;
    if (node.actor)
    {
        node.actor->hierarchyActive = active;
        node.actor->hierarchyVisible = visible;
        active = active && node.actor->active && !node.actor->destroyed;
        visible = visible && node.actor->visible;
    }
    for (auto& child : node.children)
    {
        ApplyStates(*child, active, visible);
    }
}

void Scene::UpdateNode(Node& node, float dt)
{
    if (!node.active || (node.actor && !node.actor->IsActive()))
    {
        return;
    }
    if (node.actor && node.actor->IsTickEnabled())
    {
        node.actor->Update(dt);
        ++statistics.updatedActors;
    }
    for (auto& child : node.children)
    {
        UpdateNode(*child, dt);
    }
}

void Scene::Update(float dt)
{
    PROFILE_SCOPE(SceneUpdate);
    ApplyStates(root, true, true);
    statistics.updatedActors = 0;
    updating = true;
    try
    {
        UpdateNode(root, dt);
    }
    catch (...)
    {
        updating = false;
        throw;
    }
    updating = false;
    // New actors render this frame and begin updating on the next tick.
    for (const auto& entry : pending)
    {
        AttachNow(*entry.actor, *entry.parent);
    }
    pending.clear();
    RemoveDestroyed();
    Profile::Add(Profile::Metric::ActorsUpdated, double(statistics.updatedActors));
}

void Scene::Prune(Node& node)
{
    for (auto it = node.children.begin(); it != node.children.end();)
    {
        Node& child = **it;
        Prune(child);
        if (child.actor && child.actor->IsDestroyed())
        {
            attached.erase(child.actor);
            it = node.children.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Scene::RemoveDestroyed()
{
    if (updating)
    {
        throw std::logic_error("Prune actors after traversal");
    }
    renderList.clear();
    worldList.clear();
    Prune(root);
}

void Scene::RefreshBounds(Node& node, Point worldOffset)
{
    node.drawableCount = 0;
    node.unbounded = false;
    if (!node.active || !node.visible || (node.actor && !node.actor->IsVisible()))
    {
        return;
    }
    if (node.actor && node.actor->RenderPasses() != 0)
    {
        node.drawableCount = 1;
        node.unbounded = !node.actor->HasRenderBounds();
        if (!node.unbounded)
        {
            const Actor& actor = *node.actor;
            float x = node.screenSpace ? actor.x : (actor.x - actor.y) * 32 - worldOffset.x;
            float y = node.screenSpace ? actor.y : (actor.x + actor.y) * 16 - worldOffset.y;
            SceneRect b = actor.RenderBounds();
            node.bounds = {b.left + x, b.top + y, b.right + x, b.bottom + y};
        }
    }
    for (auto& child : node.children)
    {
        RefreshBounds(*child, worldOffset);
        if (child->drawableCount == 0)
        {
            continue;
        }
        if (node.drawableCount == 0)
        {
            node.bounds = child->bounds;
        }
        else if (!child->unbounded)
        {
            node.bounds = Union(node.bounds, child->bounds);
        }
        node.unbounded = node.unbounded || child->unbounded;
        node.drawableCount += child->drawableCount;
    }
}

void Scene::Collect(Node& node, SceneRect clip)
{
    if (node.drawableCount == 0)
    {
        return;
    }
    if (node.clipped)
    {
        clip = Intersection(clip, node.clip);
    }
    if (Empty(clip) || (!node.unbounded && Empty(Intersection(clip, node.bounds))))
    {
        statistics.culledActors += node.drawableCount;
        if (!node.actor)
        {
            ++statistics.culledGroups;
        }
        return;
    }
    if (node.actor && node.actor->RenderPasses() != 0)
    {
        renderList.push_back({node.actor, clip});
        ++statistics.visibleActors;
    }
    for (auto& child : node.children)
    {
        Collect(*child, clip);
    }
}

void Scene::Render(Renderer& renderer, float time, Point worldOffset)
{
    SceneRect viewport{0, 0, static_cast<float>(renderer.Width()),
                       static_cast<float>(renderer.Height())};
    {
        PROFILE_SCOPE(ScenePrepare);
        ApplyStates(root, true, true);
        RefreshBounds(root, worldOffset);
        statistics.visibleActors = statistics.culledActors = statistics.culledGroups = 0;
        renderList.clear();
        Collect(root, viewport);

        worldList.clear();
        for (const auto& entry : renderList)
        {
            if (entry.actor->RendersIn(ActorPass::World))
            {
                worldList.push_back(entry);
            }
        }
        {
            PROFILE_SCOPE(SceneSort);
            std::stable_sort(worldList.begin(), worldList.end(),
                             [](const RenderEntry& a, const RenderEntry& b)
                             {
                                 return a.actor->Depth() < b.actor->Depth();
                             });
        }
    }
    Profile::Set(Profile::Metric::ActorsVisible, double(statistics.visibleActors));
    Profile::Set(Profile::Metric::ActorsCulled, double(statistics.culledActors));
    Profile::Set(Profile::Metric::GroupsCulled, double(statistics.culledGroups));
    constexpr Profile::Metric passMetrics[] = {
        Profile::Metric::GroundPass,  Profile::Metric::ShadowPass,  Profile::Metric::WorldPass,
        Profile::Metric::EffectsPass, Profile::Metric::OverlayPass, Profile::Metric::UiPass};

    renderer.Begin(time, worldOffset);
    for (ActorPass pass : {ActorPass::Ground, ActorPass::Shadow, ActorPass::World,
                           ActorPass::Effects, ActorPass::Overlay, ActorPass::UI})
    {
        if (pass == ActorPass::Overlay)
        {
            renderer.FinishScene();
        }
        Profile::Scope passScope(passMetrics[static_cast<unsigned>(pass)]);
        const auto& entries = pass == ActorPass::World ? worldList : renderList;
        for (const auto& entry : entries)
        {
            if (!entry.actor->RendersIn(pass))
            {
                continue;
            }
            const auto& c = entry.clip;
            if (c.left == 0 && c.top == 0 && c.right == viewport.right &&
                c.bottom == viewport.bottom)
            {
                renderer.ResetClipRect();
            }
            else
            {
                int left = static_cast<int>(std::ceil(c.left));
                int top = static_cast<int>(std::ceil(c.top));
                int right = static_cast<int>(std::floor(c.right));
                int bottom = static_cast<int>(std::floor(c.bottom));
                renderer.SetClipRect(left, top, std::max(0, right - left),
                                     std::max(0, bottom - top));
            }
            renderer.SetMaterial(Material::Plain);
            Profile::Add(Profile::Metric::ActorCallbacks);
            entry.actor->Render(renderer, pass);
        }
    }
    renderer.ResetClipRect();
    renderer.Flush(Profile::Metric::FlushFrameEnd);
    Profile::EndGpuFrame();
}

const Scene::Statistics& Scene::GetStatistics() const
{
    return statistics;
}
