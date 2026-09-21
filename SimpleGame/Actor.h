#pragma once
#include <functional>

class Renderer;

enum class ActorPass
{
    Ground,
    Shadow,
    World,
    Effects,
    Overlay,
    UI
};

// One placed entity. Primitive shapes and model vertices belong to its visual representation.
class Actor
{
  public:
    Actor(float x = 0, float y = 0);
    virtual ~Actor() = default;

    float x = 0;
    float y = 0;
    float depthOffset = 0;

    void SetPosition(float newX, float newY);
    void SetActive(bool value);
    void SetVisible(bool value);
    void Destroy();
    bool IsActive() const;
    bool IsVisible() const;
    bool IsDestroyed() const;
    virtual float Depth() const;

    // All callers go through these guards, including derived actors and render passes.
    void Update(float dt);
    void Render(Renderer& renderer, ActorPass pass) const;

  protected:
    virtual void OnUpdate(float dt);
    virtual void OnRender(Renderer& renderer, ActorPass pass) const = 0;

  private:
    bool active = true;
    bool visible = true;
    bool destroyed = false;
};

// A persistent actor for composite terrain, atmosphere, HUD, and existing player logic.
class CallbackActor final : public Actor
{
  public:
    std::function<void(float)> update;
    std::function<void(Renderer&, ActorPass)> draw;

  protected:
    void OnUpdate(float dt) override;
    void OnRender(Renderer& renderer, ActorPass pass) const override;
};
