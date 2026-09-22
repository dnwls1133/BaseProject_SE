#pragma once
#include <functional>

class Renderer;
class Scene;

struct SceneRect
{
    float left = 0, top = 0, right = 0, bottom = 0;
};

enum class ActorPass
{
    Ground,
    Shadow,
    World,
    Effects,
    Overlay,
    UI
};

constexpr unsigned ActorPassBit(ActorPass pass)
{
    return 1u << static_cast<unsigned>(pass);
}

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
    void SetRenderPasses(unsigned mask);
    bool RendersIn(ActorPass pass) const;
    unsigned RenderPasses() const;
    void SetRenderBounds(SceneRect bounds);
    void ClearRenderBounds();
    bool HasRenderBounds() const;
    SceneRect RenderBounds() const;
    void SetTickEnabled(bool value);
    bool IsTickEnabled() const;

    // All callers go through these guards, including derived actors and render passes.
    void Update(float dt);
    void Render(Renderer& renderer, ActorPass pass) const;

  protected:
    virtual void OnUpdate(float dt);
    virtual void OnRender(Renderer& renderer, ActorPass pass) const = 0;

  private:
    friend class Scene;
    bool active = true;
    bool visible = true;
    bool destroyed = false;
    bool hierarchyActive = true;
    bool hierarchyVisible = true;
    bool bounded = false;
    bool tickEnabled = true;
    SceneRect renderBounds;
    unsigned renderPasses = (1u << 6) - 1;
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
