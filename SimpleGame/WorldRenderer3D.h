#pragma once
#include "Renderer.h"
#include <array>
#include <memory>

struct Vertex3D
{
    Vec3 position, normal;
    float r = 1, g = 1, b = 1, material = 0;
    Vec3 uvLayer{0, 0, -1};
    float joints[4] = {0, 0, 0, 0};
    float weights[4] = {1, 0, 0, 0};
    float metallic = 0, roughness = 1;
};

struct SurfaceSet3D
{
    struct Entry
    {
        std::string name, baseColor, normal, roughness;
    };

    std::vector<Entry> entries;

    int Find(const std::string& name) const
    {
        for (int i = 0; i < int(entries.size()); ++i)
        {
            if (entries[i].name == name)
            {
                return i;
            }
        }
        return -1;
    }
};

struct Geometry3D
{
    std::vector<Vertex3D> vertices;
    Vec3 minimum, maximum;
    std::shared_ptr<SurfaceSet3D> surfaces;
    std::vector<Mat4> skinFrames;
};

class WorldRenderer3D
{
  public:
    struct Instance
    {
        Mat4 transform;
        Color color{1, 1, 1};
        float material = 0;
        Vec3 animation{-1, 0, 0};
        std::array<float, 4> clip{{-1e9f, -1e9f, 1e9f, 1e9f}};
    };

    WorldRenderer3D();
    ~WorldRenderer3D();

    bool Ready() const
    {
        return shader && shadowShader && shadowFbo;
    }

    void Begin();
    void Submit(int shape, const Mat4& matrix, Color color, float material,
                const std::array<float, 4>& clip);
    void Submit(const std::shared_ptr<Geometry3D>& mesh, const Mat4& transform,
                const std::array<float, 4>& clip, Color tint = Color(1, 1, 1),
                Vec3 animation = {-1, 0, 0});
    void Light(Vec3 position, Color color);
    void Render(GLuint target, const Camera3D& camera, float time, std::uint64_t& calls);

  private:
    struct Buffer
    {
        GLuint vao = 0, vertices = 0, instances = 0, palette = 0, paletteTexture = 0;
        GLsizei count = 0, instanceCount = 0, shadowCount = 0;
        std::weak_ptr<Geometry3D> owner;
    };

    struct Lamp
    {
        Vec3 position;
        Color color;
    };

    std::array<Buffer, 5> primitives;
    std::array<std::vector<Instance>, 5> batches;
    std::unordered_map<const Geometry3D*, Buffer> staticBuffers;
    std::unordered_map<const Geometry3D*, std::vector<Instance>> meshInstances;
    GLuint surfaceTextures[3] = {};
    GLuint identityPalette = 0, identityPaletteTexture = 0;
    std::weak_ptr<SurfaceSet3D> uploadedSurfaces;
    void EnsureSurfaces(const std::shared_ptr<SurfaceSet3D>& surfaces);
    std::vector<Lamp> lamps;
    GLuint shader = 0, shadowShader = 0, shadowFbo = 0, shadowDepth = 0;
    void Upload(Buffer& buffer, const std::vector<Vertex3D>& vertices);
    static void Release(Buffer& buffer);
    void Draw(std::uint64_t& calls, bool shadows);
};
