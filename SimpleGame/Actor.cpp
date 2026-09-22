#include "stdafx.h"
#include "Actor.h"

Actor::Actor(float x, float y) : x(x), y(y)
{
}

void Actor::SetPosition(float newX, float newY)
{
    x = newX;
    y = newY;
}

void Actor::SetActive(bool value)
{
    active = value;
}

void Actor::SetVisible(bool value)
{
    visible = value;
}

void Actor::Destroy()
{
    destroyed = true;
}

bool Actor::IsActive() const
{
    return active && hierarchyActive && !destroyed;
}

bool Actor::IsVisible() const
{
    return visible && hierarchyVisible && IsActive();
}

bool Actor::IsDestroyed() const
{
    return destroyed;
}

float Actor::Depth() const
{
    return x + y + depthOffset;
}

void Actor::Update(float dt)
{
    if (IsActive() && tickEnabled)
    {
        OnUpdate(dt);
    }
}

void Actor::Render(Renderer& renderer, ActorPass pass) const
{
    if (IsVisible() && RendersIn(pass))
    {
        OnRender(renderer, pass);
    }
}

void Actor::OnUpdate(float)
{
}

void Actor::SetRenderPasses(unsigned mask)
{
    renderPasses = mask & ((1u << 6) - 1);
}

bool Actor::RendersIn(ActorPass pass) const
{
    return (renderPasses & ActorPassBit(pass)) != 0;
}

unsigned Actor::RenderPasses() const
{
    return renderPasses;
}

void Actor::SetRenderBounds(SceneRect bounds)
{
    renderBounds = bounds;
    bounded = true;
}

void Actor::ClearRenderBounds()
{
    bounded = false;
}

void Actor::SetTickEnabled(bool value)
{
    tickEnabled = value;
}

bool Actor::IsTickEnabled() const
{
    return tickEnabled;
}

bool Actor::HasRenderBounds() const
{
    return bounded;
}

SceneRect Actor::RenderBounds() const
{
    return renderBounds;
}

void CallbackActor::OnUpdate(float dt)
{
    if (update)
    {
        update(dt);
    }
}

void CallbackActor::OnRender(Renderer& renderer, ActorPass pass) const
{
    if (draw)
    {
        draw(renderer, pass);
    }
}
