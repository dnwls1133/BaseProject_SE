#pragma once
#include "ModelCache.h"
#include "Actor.h"
#include "Scene.h"
#include "WorldSimulation.h"
#include <list>
#include "RandomLevelMap.h"
#include "RpgStats.h"
#include <string>
#include <vector>

class LevelOne
{
  public:
    LevelOne();
    LevelOne(const LevelOne&) = delete;
    LevelOne& operator=(const LevelOne&) = delete;

    bool Start(const std::string& dataDirectory, const std::string& levelFile = "");

    const std::string& ContentError() const
    {
        return contentError;
    }

    void Key(unsigned char key, bool down);
    void ClearInput();
    void Update(float dt);
    void Render(Renderer& renderer);

  private:
    struct Enemy : Actor
    {
        explicit Enemy(LevelOne* owner) : owner(owner)
        {
            SetRenderPasses(ActorPassBit(ActorPass::Shadow) | ActorPassBit(ActorPass::World) |
                            ActorPassBit(ActorPass::Overlay));
            SetRenderBounds({-180, -220, 180, 150});
        }

        LevelOne* owner;
        int health = 0, maximumHealth = 0;
        EnemyDefinition type;
        bool dead = false;
        float cooldown = 0, windup = 0, flash = 0, phase = 0;
        float attackX = 0, attackY = 0;
        float movement = 0, facing = 1, strikeTime = 0, collapse = 0;

        float WindupDuration() const
        {
            return type.windup;
        }

      protected:
        void OnUpdate(float dt) override
        {
            owner->UpdateEnemy(*this, dt);
        }

        void OnRender(Renderer& r, ActorPass pass) const override
        {
            owner->DrawEnemy(r, *this, pass);
        }
    };

    struct Drop : Actor
    {
        Drop(LevelOne* owner, float x, float y, bool potion, bool collected)
            : Actor(x, y), owner(owner), potion(potion), collected(collected)
        {
            SetRenderPasses(ActorPassBit(ActorPass::World));
            SetRenderBounds({-40, -50, 40, 30});
        }

        LevelOne* owner;
        bool potion;
        bool collected = false;

      protected:
        void OnRender(Renderer& r, ActorPass pass) const override
        {
            if (pass == ActorPass::World && !collected)
            {
                owner->DrawDrop(r, *this);
            }
        }
    };

    struct Prop : Actor
    {
        Prop(LevelOne* owner, float x, float y, const PropDefinition& type)
            : Actor(x, y), owner(owner), model(type.model), type(type)
        {
            SetRenderPasses(ActorPassBit(ActorPass::Shadow) | ActorPassBit(ActorPass::World));
            SetRenderBounds({-220, -220, 220, 120});
        }

        LevelOne* owner;
        ModelId model;
        PropDefinition type;
        float scale = 1, facing = 1;

      protected:
        void OnRender(Renderer& r, ActorPass pass) const override
        {
            owner->DrawProp(r, *this, pass);
        }
    };

    struct FloatingText : Actor
    {
        FloatingText(LevelOne* owner, float x, float y, float remaining, const std::string& text,
                     bool reward)
            : Actor(x, y), owner(owner), remaining(remaining), text(text), reward(reward)
        {
            SetRenderPasses(ActorPassBit(ActorPass::Overlay));
            SetRenderBounds({-40, -160, 300, 40});
        }

        LevelOne* owner;
        float remaining;
        std::string text;
        bool reward;

      protected:
        void OnUpdate(float dt) override
        {
            remaining -= dt;
            if (remaining <= 0)
            {
                Destroy();
            }
        }

        void OnRender(Renderer& r, ActorPass pass) const override
        {
            if (pass == ActorPass::Overlay)
            {
                owner->DrawNumber(r, *this);
            }
        }
    };

    ModelCache modelCache;
    RandomLevelMap map;
    LevelData content;
    WorldSimulation simulation;
    std::string contentRoot, worldFile, contentError;
    RpgStats stats;
    std::vector<Enemy> enemies;
    std::list<Drop> drops;
    std::vector<Prop> props;
    std::vector<Renderer::MeshVertex> minimapBackground;
    void BuildMinimapBackground();
    std::list<FloatingText> numbers;
    std::vector<int> distances;
    bool keys[256] = {};
    bool started = false, paused = false, statPanel = false, completed = false;
    CallbackActor player, terrain, hud, navigation, minimap, menus, atmosphere;
    Scene scene;
    Scene::Node* lootGroup = nullptr;
    Scene::Node* numberGroup = nullptr;
    Scene::Node* minimapGroup = nullptr;
    void BuildScene();
    void SpawnDrop(float x, float y, bool potion);
    void SpawnNumber(float x, float y, float remaining, const std::string& text, bool reward);
    float cx = 0, cy = 0, time = 0, phase = 0, movement = 0;
    float aimX = .7071f, aimY = .7071f, facing = 1;
    float attackTime = 0, attackCooldown = 0, hurtTime = 0, navigationTime = 0;
    float attackAimX = 0, attackAimY = 0;
    bool attackResolved = false;
    int kills = 0, pickups = 0, expedition = 0;
    std::string notice;
    float noticeTime = 0;

    bool Generate();
    void Move(float& x, float& y, float dx, float dy);
    void Attack();
    void Kill(Enemy& enemy);
    void Interact();
    const Prop* FindShrine() const;
    void UpdateNavigation(float dt);
    void Notify(const std::string& text);
    void DrawUI(Renderer& renderer);
    void DrawMinimap(Renderer& renderer);
    void DrawMenus(Renderer& renderer);
    void DrawGround(Renderer& renderer);
    void DrawAtmosphere(Renderer& renderer);
    void DrawPlayer(Renderer& renderer, ActorPass pass);
    void DrawEnemy(Renderer& renderer, const Enemy& enemy, ActorPass pass);
    void DrawProp(Renderer& renderer, const Prop& prop, ActorPass pass);
    void DrawDrop(Renderer& renderer, const Drop& drop);
    void DrawNumber(Renderer& renderer, const FloatingText& number);
    void UpdatePlayer(float dt);
    void UpdateEnemy(Enemy& enemy, float dt);
    Point Project(float x, float y, float z, Renderer& renderer) const;
};
