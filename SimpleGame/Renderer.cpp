#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Renderer.h"
#include "RendererShaders.h"
#include "WorldRenderer3D.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstring>

namespace
{
std::wstring Wide(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L' ');
    if (n)
    {
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    }

    return out;
}
} // namespace

GLuint Renderer::Compile(const char* vs, const char* fs)
{
    GLuint ids[2] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
    const char* source[2] = {vs, fs};
    bool good = true;
    for (int i = 0; i < 2; ++i)
    {
        glShaderSource(ids[i], 1, &source[i], nullptr);
        glCompileShader(ids[i]);
        GLint status = 0;
        glGetShaderiv(ids[i], GL_COMPILE_STATUS, &status);
        if (!status)
        {
            char log[2048] = {};
            glGetShaderInfoLog(ids[i], 2048, nullptr, log);
            std::cerr << log << '\n';
            good = false;
        }
    }
    GLuint p = 0;
    if (good)
    {
        p = glCreateProgram();
        for (auto id : ids)
        {
            glAttachShader(p, id);
        }
        glLinkProgram(p);
        GLint status = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &status);
        if (!status)
        {
            char log[2048] = {};
            glGetProgramInfoLog(p, 2048, nullptr, log);
            std::cerr << log << '\n';
            glDeleteProgram(p);
            p = 0;
        }
    }

    for (auto id : ids)
    {
        glDeleteShader(id);
    }

    return p;
}

Renderer::Renderer(int w, int h)
{
    world = std::make_unique<WorldRenderer3D>();
    worldReady = world->Ready();
    program = Compile(ShaderSource::GeometryVertex, ShaderSource::GeometryFragment);
    postProgram = Compile(ShaderSource::ScreenVertex, ShaderSource::PostFragment);
    blurProgram = Compile(ShaderSource::ScreenVertex, ShaderSource::BlurFragment);
    glGenBuffers(1, &buffer);
    glGenVertexArrays(1, &vao);
    fontDC = CreateCompatibleDC(nullptr);
    vertices.reserve(100000);
    Resize(w, h);
}

Renderer::~Renderer()
{
    for (auto& item : labels)
    {
        glDeleteTextures(1, &item.second.texture);
    }

    for (auto& font : fonts)
    {
        DeleteObject(static_cast<HFONT>(font.second));
    }
    if (fontDC)
    {
        DeleteDC((HDC)fontDC);
    }
    ReleaseTargets();
    glDeleteBuffers(1, &buffer);
    glDeleteVertexArrays(1, &vao);
    for (auto p : {program, postProgram, blurProgram})
    {
        if (p)
        {
            glDeleteProgram(p);
        }
    }
}

void Renderer::ReleaseTargets()
{
    glDeleteFramebuffers(1, &sceneFbo);
    glDeleteRenderbuffers(1, &sceneColor);
    glDeleteRenderbuffers(1, &sceneDepth);
    sceneDepth = 0;
    glDeleteFramebuffers(1, &resolveFbo);
    glDeleteTextures(1, &sceneTexture);
    glDeleteFramebuffers(2, bloomFbo);
    glDeleteTextures(2, bloomTexture);
    sceneFbo = sceneColor = resolveFbo = sceneTexture = 0;
    bloomFbo[0] = bloomFbo[1] = bloomTexture[0] = bloomTexture[1] = 0;
    ready = false;
}

void Renderer::Resize(int w, int h)
{
    Profile::Event("viewport_resize", std::to_string(w) + "x" + std::to_string(h));
    width = std::max(1, w);
    height = std::max(1, h);
    ReleaseTargets();
    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples = std::max(1, std::min(4, maxSamples));
    auto texture = [&](GLuint& id, int x, int y)
    {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, x, y, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    glGenFramebuffers(1, &sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glGenRenderbuffers(1, &sceneColor);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColor);
    glGenRenderbuffers(1, &sceneDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
    bool valid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    // Some drivers expose fewer samples for floating-point color targets.
    while (!valid && samples > 1)
    {
        samples = std::max(1, samples / 2);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneColor);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width,
                                         height);
        valid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glGenFramebuffers(1, &resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    texture(sceneTexture, width, height);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture, 0);
    valid = valid && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    bloomWidth = std::max(1, width / 4);
    bloomHeight = std::max(1, height / 4);
    glGenFramebuffers(2, bloomFbo);
    for (int i = 0; i < 2; ++i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i]);
        texture(bloomTexture[i], bloomWidth, bloomHeight);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTexture[i],
                               0);
        valid = valid && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    ready = valid && fontDC;
    if (!ready)
    {
        std::cerr << "Render target or font initialization failed.\n";
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
}

void Renderer::Begin(float t, Point worldOffset)
{
    profileGpuBegun = false;
    Profile::Set(Profile::Metric::ViewWidth, width);
    Profile::Set(Profile::Metric::ViewHeight, height);
    Profile::Set(Profile::Metric::MsaaSamples, samples);
    Profile::Set(Profile::Metric::TargetBytes,
                 double(width) * height * (8 * (samples + 1) + 4 * samples) + 2048.0 * 2048 * 4 +
                     double(bloomWidth) * bloomHeight * 16);
    Profile::Set(Profile::Metric::LabelCacheBytes, double(labelCacheBytes));
    Profile::Set(Profile::Metric::LabelCacheEntries, double(labels.size()));
    vertices.clear();
    world->Begin();
    modelSpace3D = Mat4{};
    modelSpaceIdentity = true;
    clock = t;
    glDisable(GL_SCISSOR_TEST);
    clipEnabled = false;
    offset = worldOffset;
    material = Material::Plain;
    glBindFramebuffer(GL_FRAMEBUFFER, ready ? sceneFbo : 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(.033f, .044f, .05f, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::Push(Point p, Color c, float u, float v, float kind)
{
    vertices.push_back({p.x, p.y, c.r, c.g, c.b, c.a, u, v, kind < 0 ? (float)material : kind});
}

void Renderer::AppendMesh(const std::vector<MeshVertex>& mesh, Point position, float scaleX,
                          float scaleY, float opacity)
{
    if (mesh.empty())
    {
        return;
    }
    // Keep painter order and the existing single batch. No per-triangle calls or joint work.
    const std::size_t first = vertices.size();
    vertices.insert(vertices.end(), mesh.begin(), mesh.end());
    Vertex* output = vertices.data() + first;
    const Vertex* end = output + mesh.size();
    for (; output != end; ++output)
    {
        output->x = position.x + output->x * scaleX;
        output->y = position.y + output->y * scaleY;
        output->a *= opacity;
    }
}

void Renderer::Triangle(Point a, Point b, Point c, Color k)
{
    Push(a, k);
    Push(b, k);
    Push(c, k);
}

void Renderer::Quad(Point a, Point b, Point c, Point d, Color k)
{
    Triangle(a, b, c, k);
    Triangle(a, c, d, k);
}

void Renderer::Rect(float x, float y, float w, float h, Color k)
{
    Quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, k);
}

void Renderer::Line(Point a, Point b, float w, Color k)
{
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < .001f)
    {
        return;
    }
    float nx = -dy / len * w * .5f, ny = dx / len * w * .5f;
    Quad({a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}, k);
}

void Renderer::Ellipse(float x, float y, float rx, float ry, Color k)
{
    for (int i = 0; i < 40; ++i)
    {
        float a = i * 6.2831853f / 40, b = (i + 1) * 6.2831853f / 40;
        Triangle({x, y}, {x + cosf(a) * rx, y + sinf(a) * ry}, {x + cosf(b) * rx, y + sinf(b) * ry},
                 k);
    }
}

void Renderer::SoftEllipse(float x, float y, float rx, float ry, Color k)
{
    // Multiple rings approximate Gaussian falloff, not a hard-edged transparent disc.
    const int segments = 40, rings = 6;
    for (int ring = 0; ring < rings; ++ring)
    {
        float r0 = ring / (float)rings, r1 = (ring + 1) / (float)rings;
        float alpha0 = std::exp(-r0 * r0 * 5.f) * (1 - r0),
              alpha1 = std::exp(-r1 * r1 * 5.f) * (1 - r1);
        Color inner = k, outer = k;
        inner.a *= alpha0;
        outer.a *= alpha1;
        for (int i = 0; i < segments; ++i)
        {
            float a = i * 6.2831853f / segments, b = (i + 1) * 6.2831853f / segments;
            Point p0 = {x + cosf(a) * rx * r0, y + sinf(a) * ry * r0},
                  p1 = {x + cosf(b) * rx * r0, y + sinf(b) * ry * r0};
            Point q0 = {x + cosf(a) * rx * r1, y + sinf(a) * ry * r1},
                  q1 = {x + cosf(b) * rx * r1, y + sinf(b) * ry * r1};
            Push(p0, inner);
            Push(q0, outer);
            Push(q1, outer);
            Push(p0, inner);
            Push(q1, outer);
            Push(p1, inner);
        }
    }
}

void Renderer::Flush(Profile::Metric reason)
{
    if (vertices.empty() || !program)
    {
        return;
    }
    PROFILE_SCOPE(Flush);
    Profile::Add(reason);
    Profile::Add(Profile::Metric::GeometryDraws);
    Profile::Add(Profile::Metric::DrawCalls);
    Profile::Add(Profile::Metric::Vertices, double(vertices.size()));
    Profile::Add(Profile::Metric::UploadBytes, double(vertices.size() * sizeof(Vertex)));
    Profile::Maximum(Profile::Metric::MaxBatchVertices, double(vertices.size()));
    Profile::Set(Profile::Metric::VertexCapacity, double(vertices.capacity() * sizeof(Vertex)));
    glUseProgram(program);
    glUniform2f(glGetUniformLocation(program, "viewport"), (float)width, (float)height);
    glUniform2f(glGetUniformLocation(program, "worldOffset"), offset.x, offset.y);
    glUniform1i(glGetUniformLocation(program, "glyph"), 0);
    glUniform1f(glGetUniformLocation(program, "effectTime"), clock);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    {
        PROFILE_SCOPE(Upload);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(),
                     GL_STREAM_DRAW);
    }
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));
    if (!profileGpuBegun)
    {
        Profile::BeginGpuFrame();
        profileGpuBegun = true;
        if (!worldEnabled)
        {
            Profile::GpuMarker(1);
        }
    }
    ++drawCallCount;
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());
    vertices.clear();
    glBindVertexArray(0);
}

void Renderer::Fullscreen(GLuint shader)
{
    Profile::Add(Profile::Metric::DrawCalls);
    Profile::Add(Profile::Metric::FullscreenDraws);
    glUseProgram(shader);
    glBindVertexArray(vao);
    ++drawCallCount;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Renderer::FinishScene()
{
    PROFILE_SCOPE(Postprocess);
    if (worldEnabled)
    {
        if (!profileGpuBegun)
        {
            Profile::BeginGpuFrame();
            profileGpuBegun = true;
        }
        world->Render(ready ? sceneFbo : 0, camera, clock, drawCallCount);
    }
    ResetClipRect();
    Flush(Profile::Metric::FlushPost);
    if (!profileGpuBegun)
    {
        Profile::BeginGpuFrame();
        profileGpuBegun = true;
        if (!worldEnabled)
        {
            Profile::GpuMarker(1);
        }
    }
    Profile::GpuMarker(2);
    material = Material::Plain;
    if (!ready)
    {
        for (unsigned marker = 3; marker <= 6; ++marker)
        {
            Profile::GpuMarker(marker);
        }
        return;
    }
    {
        PROFILE_SCOPE(Resolve);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    }
    Profile::GpuMarker(3);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, bloomWidth, bloomHeight);
    glUseProgram(blurProgram);
    glUniform1i(glGetUniformLocation(blurProgram, "sourceImage"), 0);
    for (int i = 0; i < 2; ++i)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i]);
        glBindTexture(GL_TEXTURE_2D, i ? bloomTexture[0] : sceneTexture);
        glUniform1i(glGetUniformLocation(blurProgram, "extractLight"), i == 0 ? 1 : 0);
        glUniform2f(glGetUniformLocation(blurProgram, "direction"), i ? 0.f : 1.f / bloomWidth,
                    i ? 1.f / bloomHeight : 0.f);
        Fullscreen(blurProgram);
        Profile::GpuMarker(4 + i);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glUseProgram(postProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glUniform1i(glGetUniformLocation(postProgram, "scene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTexture[1]);
    glUniform1i(glGetUniformLocation(postProgram, "bloom"), 1);
    glUniform2f(glGetUniformLocation(postProgram, "texel"), 1.f / width, 1.f / height);
    glUniform1f(glGetUniformLocation(postProgram, "time"), clock);
    Fullscreen(postProgram);
    Profile::GpuMarker(6);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::SetClipRect(int x, int y, int w, int h)
{
    if (clipEnabled && clipX == x && clipY == y && clipWidth == w && clipHeight == h)
    {
        return;
    }
    // Submit the old batch under its old clip before changing OpenGL state.
    Profile::Add(Profile::Metric::ClipChanges);
    Flush(Profile::Metric::FlushClip);
    clipEnabled = true;
    clipX = x;
    clipY = y;
    clipWidth = w;
    clipHeight = h;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, height - y - h, std::max(0, w), std::max(0, h));
}

void Renderer::ResetClipRect()
{
    if (clipEnabled)
    {
        Profile::Add(Profile::Metric::ClipChanges);
        Flush(Profile::Metric::FlushClip);
        glDisable(GL_SCISSOR_TEST);
        clipEnabled = false;
    }
}

Renderer::Label& Renderer::GetLabel(const std::string& text, int size)
{
    std::string key = std::to_string(size) + ":" + text;
    auto found = labels.find(key);
    if (found != labels.end())
    {
        found->second.lastUsed = ++labelUse;
        Profile::Add(Profile::Metric::LabelHits);
        return found->second;
    }

    Profile::Add(Profile::Metric::LabelMisses);
    PROFILE_SCOPE(LabelBuild);
    while (!labels.empty() && (labels.size() >= 512 || labelCacheBytes > 32 * 1024 * 1024))
    {
        const auto oldest = std::min_element(labels.begin(), labels.end(),
                                             [](const auto& a, const auto& b)
                                             {
                                                 return a.second.lastUsed < b.second.lastUsed;
                                             });
        Flush(Profile::Metric::FlushLabelEviction);
        glDeleteTextures(1, &oldest->second.texture);
        labelCacheBytes -= std::size_t(oldest->second.width) * oldest->second.height * 4;
        labels.erase(oldest);
        Profile::Add(Profile::Metric::LabelEvictions);
    }
    Label label;
    label.lastUsed = ++labelUse;
    HDC dc = (HDC)fontDC;
    if (!dc)
    {
        return labels.emplace(key, label).first->second;
    }
    std::wstring wide = Wide(text);
    HFONT font = static_cast<HFONT>(Font(size));
    if (!font)
    {
        return labels.emplace(key, label).first->second;
    }
    HGDIOBJ oldFont = SelectObject(dc, font);
    SIZE measure = {};
    GetTextExtentPoint32W(dc, wide.c_str(), (int)wide.size(), &measure);
    int w = std::max(2, (int)measure.cx + 8), h = std::max(2, (int)measure.cy + 8);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap && pixels)
    {
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        std::memset(pixels, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 4, 4, wide.c_str(), (int)wide.size());
        GdiFlush();
        unsigned char* bytes = (unsigned char*)pixels;
        for (int i = 0; i < w * h; ++i)
        {
            unsigned char a = std::max(bytes[i * 4], std::max(bytes[i * 4 + 1], bytes[i * 4 + 2]));
            bytes[i * 4] = bytes[i * 4 + 1] = bytes[i * 4 + 2] = 255;
            bytes[i * 4 + 3] = a;
        }
        glGenTextures(1, &label.texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, label.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        label.width = w;
        label.height = h;
        labelCacheBytes += static_cast<std::size_t>(w) * h * 4;
        Profile::Add(Profile::Metric::LabelUploadBytes, double(w) * h * 4);
        Profile::Set(Profile::Metric::LabelCacheBytes, double(labelCacheBytes));
        Profile::Set(Profile::Metric::LabelCacheEntries, double(labels.size() + 1));
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
    }
    SelectObject(dc, oldFont);
    return labels.emplace(key, label).first->second;
}

void* Renderer::Font(int size)
{
    auto found = fonts.find(size);
    if (found != fonts.end())
    {
        return found->second;
    }
    HFONT font = CreateFontW(-size * 2, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Malgun Gothic");
    if (font)
    {
        fonts.emplace(size, font);
    }
    return font;
}

float Renderer::TextWidth(const std::string& s, int size)
{
    if (s.empty())
    {
        return 4;
    }
    const std::string key = std::to_string(size) + ":" + s;
    const auto found = textMeasures.find(key);
    if (found != textMeasures.end())
    {
        return found->second;
    }
    HDC dc = static_cast<HDC>(fontDC);
    HFONT font = static_cast<HFONT>(Font(size));
    if (!dc || !font)
    {
        return 4;
    }
    auto previous = SelectObject(dc, font);
    const std::wstring text = Wide(s);
    SIZE measure{};
    GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &measure);
    SelectObject(dc, previous);
    if (textMeasures.size() >= 4096)
    {
        textMeasures.clear();
    }
    const float width = measure.cx * .5f + 4;
    textMeasures.emplace(key, width);
    return width;
}

void Renderer::Text(float x, float baseline, const std::string& s, Color c, int size)
{
    PROFILE_SCOPE(Text);
    if (s.empty())
    {
        return;
    }
    Profile::Add(Profile::Metric::TextCalls);
    Flush(Profile::Metric::FlushText);
    Label& label = GetLabel(s, size);
    if (!label.texture)
    {
        return;
    }
    float w = label.width * .5f, h = label.height * .5f, y = baseline - size - 2;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, label.texture);
    Push({x, y}, c, 0, 0, 6);
    Push({x + w, y}, c, 1, 0, 6);
    Push({x + w, y + h}, c, 1, 1, 6);
    Push({x, y}, c, 0, 0, 6);
    Push({x + w, y + h}, c, 1, 1, 6);
    Push({x, y + h}, c, 0, 1, 6);
    Flush(Profile::Metric::FlushText);
}

void Renderer::WrappedText(float x, float y, float maxWidth, const std::string& s, Color c,
                           int size)
{
    // Split on UTF-8 code point boundaries; no Korean glyph is cut in half.
    std::string row;
    float length = 0;
    for (size_t i = 0; i < s.size();)
    {
        unsigned char lead = (unsigned char)s[i];
        size_t n = lead < 128 ? 1 : lead < 224 ? 2 : lead < 240 ? 3 : 4;
        n = std::min(n, s.size() - i);
        std::string ch = s.substr(i, n);
        i += n;
        float advance = (TextWidth(ch, size) - 4);
        if (ch == "\n" || length + advance > maxWidth)
        {
            Text(x, y, row, c, size);
            y += size + 9;
            row.clear();
            length = 0;
        }

        if (ch != "\n")
        {
            row += ch;
            length += advance;
        }
    }
    Text(x, y, row, c, size);
}

void Renderer::Panel(float x, float y, float w, float h)
{
    SetMaterial(Material::Plain);
    Rect(x + 3, y + 5, w, h, Color(0, 0, 0, .24f));
    Rect(x, y, w, h, Color(.025f, .032f, .038f, .96f));
    Rect(x, y, w, 1, Color(.48f, .40f, .29f, .7f));
    Rect(x, y + h - 1, w, 1, Color(.28f, .25f, .2f, .7f));
    Rect(x, y, 1, h, Color(.32f, .29f, .23f, .7f));
    Rect(x + w - 1, y, 1, h, Color(.32f, .29f, .23f, .7f));
    for (float edge : {x, x + w - 16})
    {
        Rect(edge, y, 16, 2, Color(.64f, .5f, .32f));
        Rect(edge, y + h - 2, 16, 2, Color(.4f, .31f, .23f));
    }
}

void Renderer::Effect(float x, float y, float w, float h, Material effect, Color color)
{
    Profile::Add(Profile::Metric::Effects);
    if (Profile::Enabled())
    {
        float visibleWidth = std::max(0.f, std::min(float(width), x + w) - std::max(0.f, x));
        float visibleHeight = std::max(0.f, std::min(float(height), y + h) - std::max(0.f, y));
        Profile::Add(Profile::Metric::EffectPixels, double(visibleWidth) * visibleHeight);
        if (effect == Material::Mist)
        {
            Profile::Add(Profile::Metric::MistPixels, double(visibleWidth) * visibleHeight);
        }
        else if (effect == Material::SoftLight)
        {
            Profile::Add(Profile::Metric::LightPixels, double(visibleWidth) * visibleHeight);
        }
        else if (effect == Material::Flame)
        {
            Profile::Add(Profile::Metric::FlamePixels, double(visibleWidth) * visibleHeight);
        }
    }
    float kind = static_cast<float>(effect);
    Push({x, y}, color, 0, 0, kind);
    Push({x + w, y}, color, 1, 0, kind);
    Push({x + w, y + h}, color, 1, 1, kind);
    Push({x, y}, color, 0, 0, kind);
    Push({x + w, y + h}, color, 1, 1, kind);
    Push({x, y + h}, color, 0, 1, kind);
}
