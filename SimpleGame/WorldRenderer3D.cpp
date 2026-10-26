#include "stdafx.h"
#include "WorldRenderer3D.h"
#include <stdexcept>
#include "WorldShaders3D.h"
#include <algorithm>
#include <cstddef>
#include <iostream>

namespace
{
GLuint Program(const char* fragment)
{
    GLuint ids[2] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
    const char* sources[2] = {WorldShaders3D::Vertex, fragment};
    GLuint result = glCreateProgram();
    bool valid = true;
    for (int i = 0; i < 2; ++i)
    {
        glShaderSource(ids[i], 1, &sources[i], nullptr);
        glCompileShader(ids[i]);
        GLint ok = 0;
        glGetShaderiv(ids[i], GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[4096] = {};
            glGetShaderInfoLog(ids[i], 4096, nullptr, log);
            std::cerr << "3D shader: " << log << '\n';
            valid = false;
        }
        glAttachShader(result, ids[i]);
    }
    glLinkProgram(result);
    GLint ok = 0;
    glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096] = {};
        glGetProgramInfoLog(result, 4096, nullptr, log);
        std::cerr << "3D program: " << log << '\n';
        valid = false;
    }
    for (auto id : ids)
    {
        glDeleteShader(id);
    }
    if (!valid)
    {
        glDeleteProgram(result);
        return 0;
    }
    return result;
}

std::vector<Vertex3D> Primitive(int shape)
{
    std::vector<Vertex3D> v;
    auto triangle = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 na, Vec3 nb, Vec3 nc)
    {
        v.push_back({a, na});
        v.push_back({b, nb});
        v.push_back({c, nc});
    };
    if (shape == 0)
    {
        const Vec3 normal[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
        for (Vec3 n : normal)
        {
            Vec3 u = std::abs(n.y) > .5f ? Vec3{1, 0, 0} : Normalize(Cross({0, 1, 0}, n));
            Vec3 w = Cross(n, u), center = n * .5f;
            Vec3 a = center - u * .5f - w * .5f, b = center + u * .5f - w * .5f,
                 c = center + u * .5f + w * .5f, d = center - u * .5f + w * .5f;
            triangle(a, b, c, n, n, n);
            triangle(a, c, d, n, n, n);
        }
    }
    else if (shape == 1)
    {
        auto point = [](int x, int y)
        {
            float a = x * 6.2831853f / 16, b = y * 3.14159265f / 10;
            return Vec3{std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a)};
        };
        for (int y = 0; y < 10; ++y)
        {
            for (int x = 0; x < 16; ++x)
            {
                Vec3 a = point(x, y), b = point(x + 1, y), c = point(x + 1, y + 1),
                     d = point(x, y + 1);
                triangle(a * .5f, b * .5f, c * .5f, a, b, c);
                triangle(a * .5f, c * .5f, d * .5f, a, c, d);
            }
        }
    }
    else if (shape == 4)
    {
        const Vec3 ring[] = {{.5f, 0, 0}, {0, 0, .5f}, {-.5f, 0, 0}, {0, 0, -.5f}};
        for (int i = 0; i < 4; ++i)
        {
            const Vec3 a = ring[i], b = ring[(i + 1) % 4];
            triangle({0, .5f, 0}, b, a, {0, 1, 0}, Normalize(b), Normalize(a));
            triangle({0, -.5f, 0}, a, b, {0, -1, 0}, Normalize(a), Normalize(b));
        }
    }
    else
    {
        for (int i = 0; i < 16; ++i)
        {
            float a = i * 6.2831853f / 16, b = (i + 1) * 6.2831853f / 16;
            Vec3 na{std::cos(a), shape == 3 ? .5f : 0, std::sin(a)},
                nb{std::cos(b), shape == 3 ? .5f : 0, std::sin(b)};
            Vec3 p{std::cos(a) * .5f, -.5f, std::sin(a) * .5f},
                q{std::cos(b) * .5f, -.5f, std::sin(b) * .5f};
            Vec3 r = shape == 3 ? Vec3{0, .5f, 0} : Vec3{q.x, .5f, q.z}, s{p.x, .5f, p.z};
            triangle(p, r, q, Normalize(na), shape == 3 ? Normalize(na + nb) : nb, Normalize(nb));
            if (shape == 2)
            {
                triangle(p, s, r, na, na, nb);
                triangle({0, .5f, 0}, r, s, {0, 1, 0}, {0, 1, 0}, {0, 1, 0});
            }
            triangle({0, -.5f, 0}, p, q, {0, -1, 0}, {0, -1, 0}, {0, -1, 0});
        }
    }
    return v;
}
} // namespace

void WorldRenderer3D::Upload(Buffer& b, const std::vector<Vertex3D>& vertices)
{
    glGenVertexArrays(1, &b.vao);
    glBindVertexArray(b.vao);
    glGenBuffers(1, &b.vertices);
    glBindBuffer(GL_ARRAY_BUFFER, b.vertices);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex3D), vertices.data(),
                 GL_STATIC_DRAW);
    b.count = static_cast<GLsizei>(vertices.size());
    for (int i = 0; i < 3; ++i)
    {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, i == 2 ? 4 : 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                              reinterpret_cast<void*>(sizeof(float) * (i == 0   ? 0
                                                                       : i == 1 ? 3
                                                                                : 6)));
    }
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                          reinterpret_cast<void*>(offsetof(Vertex3D, uvLayer)));
    glEnableVertexAttribArray(11);
    glVertexAttribPointer(11, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                          reinterpret_cast<void*>(offsetof(Vertex3D, joints)));
    glEnableVertexAttribArray(12);
    glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                          reinterpret_cast<void*>(offsetof(Vertex3D, weights)));
    glEnableVertexAttribArray(14);
    glVertexAttribPointer(14, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                          reinterpret_cast<void*>(offsetof(Vertex3D, metallic)));
    glGenBuffers(1, &b.instances);
    glBindBuffer(GL_ARRAY_BUFFER, b.instances);
    for (int i = 0; i < 6; ++i)
    {
        GLuint at = 3 + i;
        glEnableVertexAttribArray(at);
        glVertexAttribPointer(at, i == 5 ? 1 : 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                              reinterpret_cast<void*>(sizeof(float) * i * 4));
        glVertexAttribDivisor(at, 1);
    }
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, clip)));
    glVertexAttribDivisor(9, 1);
    glEnableVertexAttribArray(13);
    glVertexAttribPointer(13, 3, GL_FLOAT, GL_FALSE, sizeof(Instance),
                          reinterpret_cast<void*>(offsetof(Instance, animation)));
    glVertexAttribDivisor(13, 1);
    glBindVertexArray(0);
}

WorldRenderer3D::WorldRenderer3D()
{
    shader = Program(WorldShaders3D::Fragment);
    shadowShader = Program(WorldShaders3D::Shadow);
    for (int i = 0; i < 5; ++i)
    {
        Upload(primitives[i], Primitive(i));
    }
    Mat4 identity;
    glGenBuffers(1, &identityPalette);
    glBindBuffer(GL_TEXTURE_BUFFER, identityPalette);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(identity), &identity, GL_STATIC_DRAW);
    glGenTextures(1, &identityPaletteTexture);
    glBindTexture(GL_TEXTURE_BUFFER, identityPaletteTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, identityPalette);
    glGenTextures(1, &shadowDepth);
    glBindTexture(GL_TEXTURE_2D, shadowDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2048, 2048, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glGenFramebuffers(1, &shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepth, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glDeleteFramebuffers(1, &shadowFbo);
        shadowFbo = 0;
        std::cerr << "3D shadow framebuffer incomplete\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void WorldRenderer3D::Release(Buffer& b)
{
    glDeleteBuffers(1, &b.palette);
    glDeleteTextures(1, &b.paletteTexture);
    glDeleteBuffers(1, &b.vertices);
    glDeleteBuffers(1, &b.instances);
    glDeleteVertexArrays(1, &b.vao);
}

WorldRenderer3D::~WorldRenderer3D()
{
    for (auto& b : primitives)
    {
        Release(b);
    }
    for (auto& b : staticBuffers)
    {
        Release(b.second);
    }
    glDeleteBuffers(1, &identityPalette);
    glDeleteTextures(1, &identityPaletteTexture);
    glDeleteTextures(3, surfaceTextures);
    glDeleteProgram(shader);
    glDeleteProgram(shadowShader);
    glDeleteFramebuffers(1, &shadowFbo);
    glDeleteTextures(1, &shadowDepth);
}

void WorldRenderer3D::Begin()
{
    for (auto& b : batches)
    {
        b.clear();
    }
    lamps.clear();
    for (auto& batch : meshInstances)
    {
        batch.second.clear();
    }
    for (auto it = staticBuffers.begin(); it != staticBuffers.end();)
    {
        if (it->second.owner.expired())
        {
            meshInstances.erase(it->first);
            Release(it->second);
            it = staticBuffers.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void WorldRenderer3D::Submit(int shape, const Mat4& matrix, Color color, float material,
                             const std::array<float, 4>& clip)
{
    if (shape == 1)
    {
        const float diameter2 = std::max(
            {matrix.m[0] * matrix.m[0] + matrix.m[1] * matrix.m[1] + matrix.m[2] * matrix.m[2],
             matrix.m[4] * matrix.m[4] + matrix.m[5] * matrix.m[5] + matrix.m[6] * matrix.m[6],
             matrix.m[8] * matrix.m[8] + matrix.m[9] * matrix.m[9] + matrix.m[10] * matrix.m[10]});
        if (diameter2 < .14f * .14f)
        {
            shape = 4;
            Profile::Add(Profile::Metric::World3DSmallLod);
        }
    }
    if (shape >= 0 && shape < 5)
    {
        Instance instance;
        instance.transform = matrix;
        instance.color = color;
        instance.material = material;
        instance.clip = clip;
        batches[shape].push_back(instance);
    }
}

void WorldRenderer3D::Submit(const std::shared_ptr<Geometry3D>& mesh, const Mat4& transform,
                             const std::array<float, 4>& clip, Color tint, Vec3 animation)
{
    if (mesh->vertices.empty())
    {
        return;
    }
    EnsureSurfaces(mesh->surfaces);
    auto it = staticBuffers.find(mesh.get());
    if (it == staticBuffers.end())
    {
        Buffer b;
        Upload(b, mesh->vertices);
        b.owner = mesh;
        if (mesh->skinFrames.size() > 1)
        {
            GLint maximum = 0;
            glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximum);
            if (mesh->skinFrames.size() * 4 > std::size_t(maximum))
            {
                Release(b);
                throw std::runtime_error("GPU skin palette exceeds texture buffer capacity");
            }
            glGenBuffers(1, &b.palette);
            glBindBuffer(GL_TEXTURE_BUFFER, b.palette);
            glBufferData(GL_TEXTURE_BUFFER, mesh->skinFrames.size() * sizeof(Mat4),
                         mesh->skinFrames.data(), GL_STATIC_DRAW);
            glGenTextures(1, &b.paletteTexture);
            glBindTexture(GL_TEXTURE_BUFFER, b.paletteTexture);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, b.palette);
        }
        b.instanceCount = b.shadowCount = 1;
        Instance instance;
        instance.material = -1;
        glBindBuffer(GL_ARRAY_BUFFER, b.instances);
        glBufferData(GL_ARRAY_BUFFER, sizeof(instance), &instance, GL_STATIC_DRAW);
        it = staticBuffers.emplace(mesh.get(), b).first;
    }
    Instance submitted;
    submitted.transform = transform;
    submitted.material = -1;
    submitted.clip = clip;
    submitted.color = tint;
    submitted.animation = animation;
    meshInstances[mesh.get()].push_back(submitted);
}

void WorldRenderer3D::Light(Vec3 p, Color c)
{
    lamps.push_back({p, c});
}

void WorldRenderer3D::Draw(std::uint64_t& calls, bool shadows)
{
    auto draw = [&](const Buffer& b)
    {
        const GLsizei count = shadows ? b.shadowCount : b.instanceCount;
        if (!count)
        {
            return;
        }
        glBindVertexArray(b.vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, b.count, count);
        ++calls;
        Profile::Add(Profile::Metric::DrawCalls);
        Profile::Add(Profile::Metric::GeometryDraws);
        Profile::Add(Profile::Metric::World3DTriangles, double(b.count / 3) * count);
    };
    // Authored grass/cloth includes two-sided surfaces. Keep their reverse faces visible.
    glDisable(GL_CULL_FACE);
    for (const auto& batch : meshInstances)
    {
        if (batch.second.empty())
        {
            continue;
        }
        const auto& buffer = staticBuffers.at(batch.first);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_BUFFER,
                      buffer.paletteTexture ? buffer.paletteTexture : identityPaletteTexture);
        draw(buffer);
    }
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER, identityPaletteTexture);
    glEnable(GL_CULL_FACE);
    for (auto& b : primitives)
    {
        draw(b);
    }
}

void WorldRenderer3D::Render(GLuint target, const Camera3D& camera, float time,
                             std::uint64_t& calls)
{
    PROFILE_SCOPE(World3D);
    if (!Ready())
    {
        return;
    }
    for (int i = 0; i < 5; ++i)
    {
        auto& b = primitives[i];
        b.instanceCount = static_cast<GLsizei>(batches[i].size());
        const auto end = std::partition(batches[i].begin(), batches[i].end(),
                                        [](const Instance& item)
                                        {
                                            return item.material != 7 && item.material != 8 &&
                                                   item.material != 10 && item.material != 11 &&
                                                   item.color.a >= .8f;
                                        });
        b.shadowCount = static_cast<GLsizei>(end - batches[i].begin());
        Profile::Add(Profile::Metric::World3DShadowInstances, b.shadowCount);
        glBindBuffer(GL_ARRAY_BUFFER, b.instances);
        glBufferData(GL_ARRAY_BUFFER, batches[i].size() * sizeof(Instance), batches[i].data(),
                     GL_STREAM_DRAW);
        Profile::Add(Profile::Metric::World3DInstances, b.instanceCount);
    }
    for (const auto& batch : meshInstances)
    {
        auto& buffer = staticBuffers.at(batch.first);
        buffer.instanceCount = buffer.shadowCount = GLsizei(batch.second.size());
        glBindBuffer(GL_ARRAY_BUFFER, buffer.instances);
        glBufferData(GL_ARRAY_BUFFER, batch.second.size() * sizeof(Instance), batch.second.data(),
                     GL_STREAM_DRAW);
        Profile::Add(Profile::Metric::World3DInstances, buffer.instanceCount);
        Profile::Add(Profile::Metric::World3DShadowInstances, buffer.shadowCount);
    }
    const Vec3 sun = Normalize({-.55f, 1, -.35f});
    const Vec3 right = Normalize(Cross(sun * -1, {0, 1, 0}));
    const Vec3 up = Cross(right, sun * -1);
    constexpr float shadowTexel = 90.f / 2048;
    Vec3 anchor = camera.target;
    for (Vec3 axis : {right, up})
    {
        const float coordinate = Dot(anchor, axis);
        anchor = anchor + axis * (std::round(coordinate / shadowTexel) * shadowTexel - coordinate);
    }
    Mat4 light = Mat4::Ortho(90, 90, .1f, 180) * Mat4::LookAt(anchor + sun * 85, anchor);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glViewport(0, 0, 2048, 2048);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(shadowShader);
    glUniform1i(glGetUniformLocation(shadowShader, "skinPalette"), 4);
    glUniformMatrix4fv(glGetUniformLocation(shadowShader, "viewProjection"), 1, GL_FALSE, light.m);
    glUniformMatrix4fv(glGetUniformLocation(shadowShader, "lightMatrix"), 1, GL_FALSE, light.m);
    glUniform1f(glGetUniformLocation(shadowShader, "time"), time);
    {
        PROFILE_SCOPE(Shadow3D);
        Draw(calls, true);
    }
    Profile::GpuMarker(1);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, camera.width, camera.height);
    glUseProgram(shader);
    glUniform1i(glGetUniformLocation(shader, "skinPalette"), 4);
    for (int i = 0; i < 3; ++i)
    {
        glActiveTexture(GL_TEXTURE1 + i);
        glBindTexture(GL_TEXTURE_2D_ARRAY, surfaceTextures[i]);
    }
    glUniform1i(glGetUniformLocation(shader, "baseColorMaps"), 1);
    glUniform1i(glGetUniformLocation(shader, "normalMaps"), 2);
    glUniform1i(glGetUniformLocation(shader, "roughnessMaps"), 3);
    glUniformMatrix4fv(glGetUniformLocation(shader, "viewProjection"), 1, GL_FALSE,
                       camera.viewProjection.m);
    glUniformMatrix4fv(glGetUniformLocation(shader, "lightMatrix"), 1, GL_FALSE, light.m);
    glUniform3f(glGetUniformLocation(shader, "eye"), camera.eye.x, camera.eye.y, camera.eye.z);
    glUniform3f(glGetUniformLocation(shader, "focus"), camera.target.x, camera.target.y,
                camera.target.z);
    glUniform1f(glGetUniformLocation(shader, "time"), time);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowDepth);
    glUniform1i(glGetUniformLocation(shader, "shadowMap"), 0);
    std::sort(lamps.begin(), lamps.end(),
              [&](const Lamp& a, const Lamp& b)
              {
                  return Dot(a.position - camera.target, a.position - camera.target) <
                         Dot(b.position - camera.target, b.position - camera.target);
              });
    int count = std::min(8, static_cast<int>(lamps.size()));
    glUniform1i(glGetUniformLocation(shader, "lampCount"), count);
    for (int i = 0; i < count; ++i)
    {
        auto& p = lamps[i].position;
        auto& c = lamps[i].color;
        glUniform3f(
            glGetUniformLocation(shader, ("lampPosition[" + std::to_string(i) + "]").c_str()), p.x,
            p.y, p.z);
        glUniform3f(glGetUniformLocation(shader, ("lampColor[" + std::to_string(i) + "]").c_str()),
                    c.r, c.g, c.b);
    }
    Draw(calls, false);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::UseWorld3D(const Camera3D& value)
{
    camera = value;
    worldEnabled = true;
}

void Renderer::UseScreen2D()
{
    worldEnabled = false;
}

void Renderer::Submit3D(int shape, const Mat4& matrix, Color color, Material mat)
{
    std::array<float, 4> clip =
        clipEnabled ? std::array<float, 4>{{float(clipX), float(height - clipY - clipHeight),
                                            float(clipX + clipWidth), float(height - clipY)}}
                    : std::array<float, 4>{{0, 0, float(width), float(height)}};
    world->Submit(shape, modelSpaceIdentity ? matrix : modelSpace3D * matrix, color,
                  static_cast<float>(mat), clip);
}

void Renderer::Submit3D(const std::shared_ptr<Geometry3D>& geometry)
{
    std::array<float, 4> clip =
        clipEnabled ? std::array<float, 4>{{float(clipX), float(height - clipY - clipHeight),
                                            float(clipX + clipWidth), float(height - clipY)}}
                    : std::array<float, 4>{{0, 0, float(width), float(height)}};
    world->Submit(geometry, modelSpace3D, clip);
}

void Renderer::Submit3D(const std::shared_ptr<Geometry3D>& geometry, const Mat4& transform,
                        Color tint, Vec3 animation)
{
    std::array<float, 4> clip =
        clipEnabled ? std::array<float, 4>{{float(clipX), float(height - clipY - clipHeight),
                                            float(clipX + clipWidth), float(height - clipY)}}
                    : std::array<float, 4>{{0, 0, float(width), float(height)}};
    world->Submit(geometry, modelSpaceIdentity ? transform : modelSpace3D * transform, clip, tint,
                  animation);
}

void Renderer::Light3D(Vec3 p, Color color)
{
    world->Light(modelSpace3D.Transform(p), color);
}
