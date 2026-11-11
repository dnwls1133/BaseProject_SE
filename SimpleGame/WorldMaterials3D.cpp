#include "stdafx.h"
#include "WorldRenderer3D.h"
#include "ContentData.h"
#include "Platform.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

namespace
{
constexpr std::uint32_t TextureCacheMagic = 0x41424752, TextureCacheVersion = 1;
constexpr std::size_t TextureBytes = 512 * 512 * 4;

std::uint64_t TextureHash(const std::vector<unsigned char>& bytes)
{
    std::uint64_t hash = 14695981039346656037ull;
    std::size_t offset = 0;
    for (; offset + sizeof(std::uint64_t) <= bytes.size(); offset += sizeof(std::uint64_t))
    {
        std::uint64_t word = 0;
        std::memcpy(&word, bytes.data() + offset, sizeof(word));
        hash = (hash ^ word) * 1099511628211ull;
    }
    for (; offset < bytes.size(); ++offset)
    {
        hash = (hash ^ bytes[offset]) * 1099511628211ull;
    }
    return hash ^ static_cast<std::uint64_t>(bytes.size());
}

std::vector<unsigned char> ReadPng(const std::string& path, const std::string& cacheDirectory)
{
    std::ifstream source(path, std::ios::binary | std::ios::ate);
    ContentData::Require(bool(source), "cannot open resource texture: " + path);
    const auto size = source.tellg();
    ContentData::Require(size > 0 && size <= 16 * 1024 * 1024,
                         "resource PNG size is invalid: " + path);
    std::vector<unsigned char> encoded(static_cast<std::size_t>(size));
    source.seekg(0);
    source.read(reinterpret_cast<char*>(encoded.data()), std::streamsize(encoded.size()));
    ContentData::Require(bool(source), "cannot read resource texture: " + path);
    const auto sourceHash = TextureHash(encoded);
    const auto cache =
        std::filesystem::path(cacheDirectory) / (std::to_string(sourceHash) + ".rgba");
    if (!cacheDirectory.empty())
    {
        PROFILE_SCOPE(TextureCacheRead);
        std::ifstream file(cache, std::ios::binary | std::ios::ate);
        if (file && file.tellg() == std::streamoff(TextureBytes + 24))
        {
            file.seekg(0);
            std::uint32_t magic = 0, version = 0;
            std::uint64_t original = 0, checksum = 0;
            file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
            file.read(reinterpret_cast<char*>(&version), sizeof(version));
            file.read(reinterpret_cast<char*>(&original), sizeof(original));
            file.read(reinterpret_cast<char*>(&checksum), sizeof(checksum));
            std::vector<unsigned char> pixels(TextureBytes);
            file.read(reinterpret_cast<char*>(pixels.data()), std::streamsize(pixels.size()));
            if (file && magic == TextureCacheMagic && version == TextureCacheVersion &&
                original == sourceHash && checksum == TextureHash(pixels))
            {
                Profile::Add(Profile::Metric::TextureCacheHits);
                return pixels;
            }
        }
    }
    Profile::Add(Profile::Metric::TextureCacheMisses);
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
    {
        PROFILE_SCOPE(TextureDecode);
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
            stbi_load_from_memory(encoded.data(), int(encoded.size()), &width, &height, nullptr, 4),
            stbi_image_free);
        ContentData::Require(decoded != nullptr,
                             "cannot decode resource texture: " + path + " (" +
                                 (stbi_failure_reason() ? stbi_failure_reason() : "unknown error") +
                                 ")");
        ContentData::Require(width == 512 && height == 512,
                             "resource texture must be 512x512: " + path);
        pixels.resize(TextureBytes);
        std::copy(decoded.get(), decoded.get() + pixels.size(), pixels.begin());
    }
    if (!cacheDirectory.empty())
    {
        PROFILE_SCOPE(TextureCacheWrite);
        std::error_code error;
        std::filesystem::create_directories(cacheDirectory, error);
        if (!error)
        {
            const std::filesystem::path temporary(cache.string() + "." +
                                                  std::to_string(Platform::ProcessId()) + ".tmp");
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            const auto checksum = TextureHash(pixels);
            file.write(reinterpret_cast<const char*>(&TextureCacheMagic),
                       sizeof(TextureCacheMagic));
            file.write(reinterpret_cast<const char*>(&TextureCacheVersion),
                       sizeof(TextureCacheVersion));
            file.write(reinterpret_cast<const char*>(&sourceHash), sizeof(sourceHash));
            file.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
            file.write(reinterpret_cast<const char*>(pixels.data()),
                       std::streamsize(pixels.size()));
            file.close();
            std::string message;
            if (!file || !Platform::AtomicReplace(temporary, cache, message))
            {
                std::filesystem::remove(temporary, error);
            }
        }
    }
    return pixels;
}
} // namespace

void WorldRenderer3D::EnsureSurfaces(const std::shared_ptr<SurfaceSet3D>& surfaces)
{
    if (!surfaces || uploadedSurfaces.lock() == surfaces)
    {
        return;
    }
    PROFILE_SCOPE(TexturePrepare);
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maximum);
    ContentData::Require(!surfaces->entries.empty() &&
                             surfaces->entries.size() <= std::size_t(maximum),
                         "resource texture array exceeds device capacity");
    GLuint next[3] = {};
    glGenTextures(3, next);
    try
    {
        for (int kind = 0; kind < 3; ++kind)
        {
            glBindTexture(GL_TEXTURE_2D_ARRAY, next[kind]);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, kind == 0 ? GL_SRGB8_ALPHA8 : GL_RGBA8, 512, 512,
                         GLsizei(surfaces->entries.size()), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            for (int layer = 0; layer < int(surfaces->entries.size()); ++layer)
            {
                const auto& entry = surfaces->entries[layer];
                const auto pixels = ReadPng(kind == 0   ? entry.baseColor
                                            : kind == 1 ? entry.normal
                                                        : entry.roughness,
                                            surfaces->cacheDirectory);
                PROFILE_SCOPE(TextureUpload);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, 512, 512, 1, GL_RGBA,
                                GL_UNSIGNED_BYTE, pixels.data());
            }
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
            if (GLEW_EXT_texture_filter_anisotropic)
            {
                GLfloat maximumAnisotropy = 1;
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximumAnisotropy);
                glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                                std::min(8.f, maximumAnisotropy));
            }
        }
    }
    catch (...)
    {
        glDeleteTextures(3, next);
        throw;
    }
    glDeleteTextures(3, surfaceTextures);
    std::copy(std::begin(next), std::end(next), std::begin(surfaceTextures));
    uploadedSurfaces = surfaces;
    Profile::Event("resource_texture_arrays_loaded",
                   "layers=" + std::to_string(surfaces->entries.size()) + " size=512 mipmaps=1");
}
