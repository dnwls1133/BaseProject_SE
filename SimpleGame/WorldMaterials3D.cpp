#include "stdafx.h"
#include "WorldRenderer3D.h"
#include "ContentData.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <iterator>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
std::vector<unsigned char> ReadPng(IWICImagingFactory* factory, const std::string& path)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICBitmapDecoder> decoder;
    const auto wide = std::filesystem::path(path).wstring();
    ContentData::Require(SUCCEEDED(factory->CreateDecoderFromFilename(
                             wide.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                             decoder.GetAddressOf())),
                         "cannot decode resource texture: " + path);
    ComPtr<IWICBitmapFrameDecode> frame;
    ContentData::Require(SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())),
                         "missing PNG frame: " + path);
    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);
    ContentData::Require(width == 512 && height == 512,
                         "resource texture must be 512x512: " + path);
    ComPtr<IWICFormatConverter> converter;
    ContentData::Require(SUCCEEDED(factory->CreateFormatConverter(converter.GetAddressOf())) &&
                             SUCCEEDED(converter->Initialize(
                                 frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                 nullptr, 0, WICBitmapPaletteTypeCustom)),
                         "cannot convert resource PNG: " + path);
    std::vector<unsigned char> pixels(width * height * 4);
    ContentData::Require(
        SUCCEEDED(converter->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data())),
        "cannot read resource pixels: " + path);
    return pixels;
}

struct ComScope
{
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ~ComScope()
    {
        if (SUCCEEDED(initialized))
        {
            CoUninitialize();
        }
    }
};
} // namespace

void WorldRenderer3D::EnsureSurfaces(const std::shared_ptr<SurfaceSet3D>& surfaces)
{
    if (!surfaces || uploadedSurfaces.lock() == surfaces)
    {
        return;
    }
    PROFILE_SCOPE(CacheRead);
    ComScope com;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    ContentData::Require(
        SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(factory.GetAddressOf()))),
        "cannot initialize Windows PNG decoder");
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
                const auto pixels = ReadPng(factory.Get(), kind == 0   ? entry.baseColor
                                                           : kind == 1 ? entry.normal
                                                                       : entry.roughness);
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
