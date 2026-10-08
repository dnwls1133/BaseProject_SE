#pragma once
#include "Actor.h"
#include "Renderer.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Scene
{
  public:
    class Node
    {
      public:
        void SetTransform3D(const Mat4& value)
        {
            local3D = value;
        }

        void SetActive(bool value)
        {
            active = value;
        }

        void SetVisible(bool value)
        {
            visible = value;
        }

        void SetClipRect(SceneRect value)
        {
            clip = value;
            clipped = true;
        }

        void ClearClipRect()
        {
            clipped = false;
        }

        const std::string& Name() const
        {
            return name;
        }

      private:
        friend class Scene;
        explicit Node(Scene* owner, std::string name, bool screenSpace);
        Scene* owner;
        Node* parent = nullptr;
        std::string name;
        Actor* actor = nullptr;
        bool active = true, visible = true, screenSpace = false;
        bool clipped = false;
        SceneRect clip;
        std::vector<std::unique_ptr<Node>> children;
        Mat4 local3D, world3D;
        Mat4 cachedTransform;
        Vec3 cachedPosition, cachedMinimum, cachedMaximum, worldMinimum, worldMaximum;
        bool boundsCached = false;
        SceneRect bounds;
        bool unbounded = false;
        std::size_t drawableCount = 0;
    };

    struct Statistics
    {
        std::size_t updatedActors = 0;
        std::size_t visibleActors = 0;
        std::size_t culledActors = 0;
        std::size_t culledGroups = 0;
    };

    Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Node& Root();
    Node& CreateGroup(Node& parent, const std::string& name, bool screenSpace = false);
    void Attach(Actor& actor, Node& parent);
    void Clear();
    void RemoveDestroyed();
    void Update(float dt);
    void Render(Renderer& renderer, float time, Point worldOffset);
    const Statistics& GetStatistics() const;

  private:
    struct PendingActor
    {
        Actor* actor;
        Node* parent;
    };

    struct RenderEntry
    {
        Actor* actor;
        SceneRect clip;
        Mat4 transform3D;
    };

    Node root;
    std::unordered_map<Actor*, Node*> attached;
    std::vector<PendingActor> pending;
    std::vector<RenderEntry> renderList;
    std::vector<RenderEntry> worldList;
    Statistics statistics;
    bool updating = false;

    void AttachNow(Actor& actor, Node& parent);
    void ApplyStates(Node& node, bool active, bool visible);
    void UpdateNode(Node& node, float dt);
    void Prune(Node& node);
    void RefreshBounds(Node& node, Point worldOffset, const Renderer& renderer);
    void Collect(Node& node, SceneRect clip);
};
