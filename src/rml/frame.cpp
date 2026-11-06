#include "frame.hpp"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <algorithm>
#include <cstring>

namespace cod4x::rml {

void Recorder::Begin(int width, int height) {
    frame = std::make_shared<Frame>();
    frame->width = width;
    frame->height = height;
    // Scissor and transform are stateful across Render() calls in RmlUi.
}

std::shared_ptr<const Frame> Recorder::End() {
    auto result = frame;
    frame.reset();
    return result;
}

Rml::CompiledGeometryHandle Recorder::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty() || indices.size() % 3) return 0;
    for (int index : indices)
        if (index < 0 || size_t(index) >= vertices.size()) return 0;
    auto geometry = std::make_shared<Geometry>();
    geometry->vertices.assign(vertices.begin(), vertices.end());
    geometry->indices.assign(indices.begin(), indices.end());
    const auto handle = next_geometry++;
    geometries.emplace(handle, std::move(geometry));
    return handle;
}

void Recorder::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {
    auto g = geometries.find(geometry);
    if (!frame || g == geometries.end()) return;
    std::shared_ptr<const Texture> t;
    if (texture) {
        auto found = textures.find(texture);
        if (found == textures.end()) return;
        t = found->second;
    }
    frame->draws.push_back({g->second, std::move(t), translation, transform, scissor, scissor_enabled});
}

void Recorder::ReleaseGeometry(Rml::CompiledGeometryHandle handle) { geometries.erase(handle); }
void Recorder::ReleaseTexture(Rml::TextureHandle handle) { textures.erase(handle); }
void Recorder::EnableScissorRegion(bool enabled) { scissor_enabled = enabled; }
void Recorder::SetScissorRegion(Rml::Rectanglei region) { scissor = region; }
void Recorder::SetTransform(const Rml::Matrix4f* value) { transform = value ? *value : Rml::Matrix4f::Identity(); }

Rml::TextureHandle Recorder::GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i dimensions) {
    if (dimensions.x <= 0 || dimensions.y <= 0 || dimensions.x > 4096 || dimensions.y > 4096 ||
        pixels.size() != size_t(dimensions.x) * size_t(dimensions.y) * 4) return 0;
    auto texture = std::make_shared<Texture>();
    texture->width = dimensions.x;
    texture->height = dimensions.y;
    texture->rgba.assign(pixels.begin(), pixels.end());
    const auto handle = next_texture++;
    textures.emplace(handle, std::move(texture));
    return handle;
}

Rml::TextureHandle Recorder::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
    Rml::String data;
    if (!Rml::GetFileInterface()->LoadFile(source, data) || data.size() < 18) return 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const int width = bytes[12] | (bytes[13] << 8), height = bytes[14] | (bytes[15] << 8);
    const int channels = bytes[16] / 8;
    const size_t offset = 18 + bytes[0];
    if (bytes[1] || bytes[2] != 2 || (channels != 3 && channels != 4) || (bytes[17] & 0xC0) ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        offset + size_t(width) * height * channels > data.size()) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "RmlUi: expected an uncompressed 24/32-bit TGA: %s", source.c_str());
        return 0;
    }
    std::vector<Rml::byte> pixels(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int sy = (bytes[17] & 0x20) ? y : height - y - 1;
        const int sx = (bytes[17] & 0x10) ? width - x - 1 : x;
        const auto* src = bytes + offset + (size_t(sy) * width + sx) * channels;
        auto* dst = pixels.data() + (size_t(y) * width + x) * 4;
        const unsigned alpha = channels == 4 ? src[3] : 255;
        dst[0] = (unsigned(src[2]) * alpha + 127) / 255;
        dst[1] = (unsigned(src[1]) * alpha + 127) / 255;
        dst[2] = (unsigned(src[0]) * alpha + 127) / 255;
        dst[3] = alpha;
    }
    dimensions = {width, height};
    return GenerateTexture(pixels, dimensions);
}

}
