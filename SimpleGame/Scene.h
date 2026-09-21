#pragma once
#include "Actor.h"
#include "Renderer.h"
#include <vector>

// Frame-scoped, non-owning render list. Owners must not mutate actor containers while rendering.
class Scene
{
  public:
    void Add(const Actor& actor);
    void Render(Renderer& renderer, float time, Point worldOffset) const;

  private:
    std::vector<const Actor*> actors;
};
