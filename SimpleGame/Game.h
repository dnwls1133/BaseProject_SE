#pragma once
#include "Renderer.h"
#include "LevelOne.h"
#include "Actor.h"
#include "Scene.h"
#include <list>
#include <string>
#include <vector>
#include <unordered_map>

class Game
{
  public:
    Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    bool Load(const std::string& path);
    void Key(unsigned char key, bool down);
    void Update(float dt);
    void Render(Renderer& renderer);
    void ClearInput();

  private:
    LevelOne levelOne;
    MeshCache3D meshCache3D;
    Camera3D camera3D;
    std::shared_ptr<Geometry3D> villageGround3D;
    void BuildVillage3D();
    bool inFirstLevel = false;
    std::string dataDirectory;

    void EnterFirstLevel();

    struct Object : Actor
    {
        Object() = default;

        Object(const std::string& type, float x, float y, float w, float h, float height,
               int variant)
            : Actor(x, y), type(type), w(w), h(h), height(height), variant(variant)
        {
        }

        Game* owner = nullptr;
        std::string type;
        float w = 1, h = 1, height = 0;
        int variant = 0;

      protected:
        void OnRender(Renderer& r, ActorPass pass) const override
        {
            if (owner)
            {
                owner->DrawObjectActor(r, *this, pass);
            }
        }
    };

    struct DialogueLine
    {
        std::string speaker, text;
    };

    std::vector<Object> objects;
    std::unordered_map<std::string, std::vector<DialogueLine>> conversations;
    std::vector<DialogueLine> dialogue;
    bool keys[256] = {};
    float mapWidth = 52, mapHeight = 40, spawnX = 8, spawnY = 7;
    CallbackActor player, terrain, atmosphere, hud;
    float cx = 8, cy = 7, time = 0, elapsed = 0, walkPhase = 0, motion = 0, facing = 1;
    float reaction = 0, noticeTime = 0, stepTime = 0;

    struct Footprint : Actor
    {
        Footprint(Game* owner, float x, float y, float age) : Actor(x, y), owner(owner), age(age)
        {
            SetRenderPasses(ActorPassBit(ActorPass::Ground));
            SetRenderBounds({-24, -16, 24, 16});
        }

        Game* owner;
        float age;

      protected:
        void OnUpdate(float dt) override
        {
            age += dt;
            if (age > 3)
            {
                Destroy();
            }
        }

        void OnRender(Renderer& r, ActorPass pass) const override
        {
            if (pass == ActorPass::Ground)
            {
                owner->DrawFootprint(r, *this);
            }
        }
    };

    std::list<Footprint> footsteps;
    Scene scene;
    Scene::Node* footprintGroup = nullptr;
    void BuildScene();
    void SpawnFootprint(float x, float y);
    bool paused = false, blood = false, given = false, accepted = false, choice = false,
         done = false;
    int answer = 0, line = 0, target = -1;
    std::string notice;
    static float Dist(float x, float y, float a, float b);
    static float SegmentDistance(float x, float y, float ax, float ay, float bx, float by);
    bool Solid(const Object& object) const;
    bool Blocked(float x, float y) const;
    bool Reach(const Object& object) const;
    const Object* Find(const std::string& type) const;
    void Say(const std::string& id);
    void Reset();
    void RefreshTarget();
    Point Project(float x, float y, float z, Renderer& renderer) const;
    void Ground(Renderer& r, float x, float y, float w, float h, Color color, float z = 0);
    void Box(Renderer& r, const Object& object, float height, Color color,
             Material material = Material::Stone);
    void Person(Renderer& r, float x, float y, bool player);
    void DrawObject(Renderer& r, const Object& object);
    void DrawShadow(Renderer& r, const Object& object);
    void DrawObjectActor(Renderer& r, const Object& object, ActorPass pass);
    void DrawFootprint(Renderer& r, const Footprint& footprint);
    void UpdatePlayer(float dt);
    void DrawGround(Renderer& r);
    void DrawAtmosphere(Renderer& r);
    void DrawUI(Renderer& r);
    void Sigil(Renderer& r, Point p, float size, Color color);
};
