#pragma once
#include "ModelCache.h"
#include "Actor.h"
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

    void Start(const std::string& dataDirectory);
    void Key(unsigned char key, bool down);
    void ClearInput();
    void Update(float dt);
    void Render(Renderer& renderer);

  private:
    struct Enemy : Actor
    {
        explicit Enemy(LevelOne* owner) : owner(owner)
        {
        }

        LevelOne* owner;
        int health = 0, maximumHealth = 0;
        bool brute = false, dead = false;
        float cooldown = 0, windup = 0, flash = 0, phase = 0;
        float attackX = 0, attackY = 0;

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
        Prop(LevelOne* owner, float x, float y, ModelId model)
            : Actor(x, y), owner(owner), model(model)
        {
        }

        LevelOne* owner;
        ModelId model;

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
    RpgStats stats;
    std::vector<Enemy> enemies;
    std::vector<Drop> drops;
    std::vector<Prop> props;
    std::vector<FloatingText> numbers;
    std::array<int, RandomLevelMap::Width * RandomLevelMap::Height> distances = {};
    bool keys[256] = {};
    bool started = false, paused = false, statPanel = false, completed = false;
    CallbackActor player, terrain, hud;
    float cx = 0, cy = 0, time = 0, phase = 0, movement = 0;
    float aimX = .7071f, aimY = .7071f, facing = 1;
    float attackTime = 0, attackCooldown = 0, hurtTime = 0, navigationTime = 0;
    float attackAimX = 0, attackAimY = 0;
    bool attackResolved = false;
    int kills = 0, pickups = 0, expedition = 0;
    std::string notice;
    float noticeTime = 0;

    void Generate();
    void Move(float& x, float& y, float dx, float dy);
    void Attack();
    void Kill(Enemy& enemy);
    void Interact();
    const Prop* FindShrine() const;
    void UpdateEnemies(float dt);
    void Notify(const std::string& text);
    void DrawUI(Renderer& renderer);
    void DrawGround(Renderer& renderer);
    void DrawPlayer(Renderer& renderer);
    void DrawEnemy(Renderer& renderer, const Enemy& enemy, ActorPass pass);
    void DrawProp(Renderer& renderer, const Prop& prop, ActorPass pass);
    void DrawDrop(Renderer& renderer, const Drop& drop);
    void DrawNumber(Renderer& renderer, const FloatingText& number);
    void UpdatePlayer(float dt);
    void UpdateEnemy(Enemy& enemy, float dt);
    Point Project(float x, float y, float z, Renderer& renderer) const;
};
