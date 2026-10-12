#pragma once
#include "ModelCache.h"
#include "MeshCache3D.h"
#include "Terrain3D.h"
#include "Actor.h"
#include "Scene.h"
#include "WorldSimulation.h"
#include "WorldSociety.h"
#include <list>
#include <map>
#include <utility>
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
    struct ActivityPlace : Actor
    {
        ActivityPlace(LevelOne* owner, std::size_t index, int model)
            : owner(owner), index(index), model(model)
        {
            SetRenderPasses(ActorPassBit(ActorPass::World) | ActorPassBit(ActorPass::Overlay));
            SetRenderBounds({-120, -120, 120, 80});
            bounds3DMin = {-4, -1, -4};
            bounds3DMax = {4, 4, 4};
            SetTickEnabled(false);
        }

        LevelOne* owner;
        std::size_t index;
        int model;

        void OnRender(Renderer& r, ActorPass pass) const override
        {
            owner->DrawActivityPlace(r, *this, pass);
        }
    };

    std::vector<ActivityPlace> activityPlaces;
    void DrawActivityPlace(Renderer& renderer, const ActivityPlace& place, ActorPass pass);

    struct Citizen : Actor
    {
        Citizen(LevelOne* owner, std::size_t index) : owner(owner), index(index)
        {
            SetRenderPasses(ActorPassBit(ActorPass::Shadow) | ActorPassBit(ActorPass::World) |
                            ActorPassBit(ActorPass::Overlay));
            SetRenderBounds({-240, -180, 340, 120});
        }

        LevelOne* owner;
        std::size_t index;

      protected:
        void OnUpdate(float dt) override
        {
            owner->UpdateCitizen(*this, dt);
        }

        void OnRender(Renderer& r, ActorPass pass) const override
        {
            owner->DrawCitizen(r, *this, pass);
        }
    };

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
        int targetKind = 0, targetIndex = -1;
        float decisionTime = 0, homeX = 0, homeY = 0;
        Point route{0, 0};
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
            const float oldX = x, oldY = y;
            owner->UpdateEnemy(*this, dt);
            if (std::hypot(x - oldX, y - oldY) > .001f)
            {
                yaw = std::atan2(oldX - x, oldY - y);
            }
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
        std::vector<MeshCache3D::PlacedPart> placedParts;
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
    MeshCache3D meshCache3D;
    Terrain3D terrain3D;
    Camera3D camera3D;
    RandomLevelMap map;
    LevelData content;
    WorldSimulation simulation;
    WorldSociety society;
    std::vector<Citizen> citizens;
    int talkingNpc = -1, focusedNpc = -1, journalTab = 0, journalPage = 0;
    int dialoguePage = 0, dialogueTextPage = 0, pendingDialogueOption = 0;
    std::string dialogueResponse;
    std::vector<std::pair<int, std::string>> dialogueChoices;
    void CloseConversation();
    bool journalOpen = false, autosaveAllowed = true;
    float autosaveTime = 0;
    float monitorTime = 1;
    std::uint64_t monitorSequence = 0;
    void PublishMonitoring(float dt);
    void DrawAttackEffect(Renderer& renderer, Vec3 position, float yaw, float progress, float reach,
                          Color color);
    void UpdateCitizen(Citizen& citizen, float dt);
    void DrawCitizen(Renderer& renderer, const Citizen& citizen, ActorPass pass);
    void DrawSocietyUI(Renderer& renderer);
    bool SocietyKey(unsigned char key);
    void CreateCitizens();
    bool SaveWorld(bool manual);
    bool RestoreWorld(bool automatic);
    std::string SavePath() const;
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

    bool Generate(std::uint32_t fixedSeed = 0);
    void Move(float& x, float& y, float dx, float dy);
    void Attack();
    void Kill(Enemy& enemy, bool playerCredit = true);
    void BuildEcologyIndex();
    std::vector<std::pair<int, int>> EcologyNearby(float x, float y) const;
    std::map<int, std::vector<std::pair<int, int>>> ecologyIndex;
    int ecologyPathBudget = 2;
    Point EcologyStep(float x, float y, Point goal);
    bool UpdateCitizenWildlife(WorldSociety::Npc& npc, float dt);
    bool EnemyTarget(const Enemy& enemy, Point& point) const;
    void SelectEnemyTarget(Enemy& enemy);

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
