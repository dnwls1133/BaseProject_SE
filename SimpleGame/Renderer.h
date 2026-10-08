#pragma once
#include "Dependencies/glew.h"
#include "Profiler.h"
#include "Math3D.h"
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

struct Point
{
    float x, y;
};

struct Color
{
    float r, g, b, a;

    Color(float R, float G, float B, float A = 1) : r(R), g(G), b(B), a(A)
    {
    }
};
enum class Material
{
    Plain = 0,
    Stone = 1,
    Earth = 2,
    Wood = 3,
    Cloth = 4,
    Paving = 5,
    Water = 7,
    Flame = 8,
    Organic = 9,
    Mist = 10,
    SoftLight = 11,
    Metal = 12
};

class WorldRenderer3D;
struct Geometry3D;

class Renderer
{
  public:
    struct MeshVertex
    {
        float x, y, r, g, b, a, u, v, kind;
    };

    void AppendMesh(const std::vector<MeshVertex>& mesh, Point position, float scaleX = 1,
                    float scaleY = 1, float opacity = 1);

    Renderer(int width, int height);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool IsInitialized() const
    {
        return program && postProgram && blurProgram && ready && worldReady;
    }

    void Resize(int width, int height);

    void ResetDrawCallCount()
    {
        drawCallCount = 0;
    }

    std::uint64_t GetDrawCallCount() const
    {
        return drawCallCount;
    }

    void Begin(float time = 0, Point worldOffset = {0, 0});
    void UseWorld3D(const Camera3D& value);
    void UseScreen2D();

    void SetModelSpace3D(const Mat4& value)
    {
        modelSpace3D = value;
        modelSpaceIdentity = true;
        for (int i = 0; i < 16; ++i)
        {
            if (value.m[i] != (i % 5 == 0 ? 1.f : 0.f))
            {
                modelSpaceIdentity = false;
                break;
            }
        }
    }

    Vec3 Project3D(Vec3 point) const
    {
        return camera.Project(modelSpace3D.Transform(point));
    }

    bool IsWorld3D() const
    {
        return worldEnabled;
    }

    const Camera3D& Camera() const
    {
        return camera;
    }

    void Submit3D(int shape, const Mat4& matrix, Color color, Material material);
    void Submit3D(const std::shared_ptr<Geometry3D>& geometry);
    void Light3D(Vec3 position, Color color);
    void FinishScene();
    void Flush(Profile::Metric reason = Profile::Metric::FlushOther);
    void SetClipRect(int x, int y, int w, int h);
    void ResetClipRect();
    void Triangle(Point a, Point b, Point c, Color color);
    void Quad(Point a, Point b, Point c, Point d, Color color);
    void Rect(float x, float y, float w, float h, Color color);
    void Ellipse(float x, float y, float rx, float ry, Color color);
    void SoftEllipse(float x, float y, float rx, float ry, Color color);
    void Line(Point a, Point b, float width, Color color);
    void Panel(float x, float y, float w, float h);
    void Effect(float x, float y, float w, float h, Material effect, Color color);

    void SetMaterial(Material value)
    {
        material = value;
    }

    void Text(float x, float baseline, const std::string& utf8, Color color, int size = 19);
    float TextWidth(const std::string& utf8, int size = 19);
    void WrappedText(float x, float baseline, float maxWidth, const std::string& utf8, Color color,
                     int size = 19);

    int Width() const
    {
        return width;
    }

    int Height() const
    {
        return height;
    }

  private:
    using Vertex = MeshVertex;

    struct Label
    {
        GLuint texture = 0;
        int width = 0, height = 0;
        std::uint64_t lastUsed = 0;
    };

    std::vector<Vertex> vertices;
    std::unordered_map<std::string, Label> labels;
    GLuint program = 0, postProgram = 0, blurProgram = 0, buffer = 0, vao = 0;
    GLuint sceneFbo = 0, sceneColor = 0, resolveFbo = 0, sceneTexture = 0;
    GLuint sceneDepth = 0;
    std::unique_ptr<WorldRenderer3D> world;
    Camera3D camera;
    Mat4 modelSpace3D;
    bool modelSpaceIdentity = true;
    bool worldEnabled = false, worldReady = false;
    GLuint bloomFbo[2] = {}, bloomTexture[2] = {};
    int width = 1, height = 1, bloomWidth = 1, bloomHeight = 1, samples = 1;
    bool ready = false;
    bool clipEnabled = false;
    bool profileGpuBegun = false;
    int clipX = 0, clipY = 0, clipWidth = 0, clipHeight = 0;
    std::uint64_t drawCallCount = 0;
    std::size_t labelCacheBytes = 0;
    float clock = 0;
    Point offset = {0, 0};
    Material material = Material::Plain;
    void* fontDC = nullptr;
    std::unordered_map<int, void*> fonts;
    std::unordered_map<std::string, float> textMeasures;
    std::uint64_t labelUse = 0;
    void* Font(int size);
    GLuint Compile(const char* vertex, const char* fragment);
    void ReleaseTargets();
    void Fullscreen(GLuint shader);
    Label& GetLabel(const std::string& utf8, int size);
    void Push(Point p, Color c, float u = 0, float v = 0, float kind = -1);
};
