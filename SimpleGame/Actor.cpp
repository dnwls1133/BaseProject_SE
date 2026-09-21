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
    return active && !destroyed;
}

bool Actor::IsVisible() const
{
    return visible && IsActive();
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
    if (IsActive())
    {
        OnUpdate(dt);
    }
}

void Actor::Render(Renderer& renderer, ActorPass pass) const
{
    if (IsVisible())
    {
        OnRender(renderer, pass);
    }
}

void Actor::OnUpdate(float)
{
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
