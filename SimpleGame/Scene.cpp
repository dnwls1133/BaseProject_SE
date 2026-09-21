#include "stdafx.h"
#include "Scene.h"
#include <algorithm>

void Scene::Add(const Actor& actor)
{
    actors.push_back(&actor);
}

void Scene::Render(Renderer& renderer, float time, Point worldOffset) const
{
    auto ordered = actors;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Actor* a, const Actor* b)
                     {
                         return a->Depth() < b->Depth();
                     });

    renderer.Begin(time, worldOffset);
    for (ActorPass pass : {ActorPass::Ground, ActorPass::Shadow, ActorPass::World,
                           ActorPass::Effects, ActorPass::Overlay, ActorPass::UI})
    {
        if (pass == ActorPass::Overlay)
        {
            renderer.FinishScene();
        }

        const auto& entries = pass == ActorPass::World ? ordered : actors;
        for (const Actor* actor : entries)
        {
            renderer.SetMaterial(Material::Plain);
            actor->Render(renderer, pass);
        }
    }
    renderer.Flush();
}
