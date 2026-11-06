#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Matrix4.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace cod4x::rml {

struct Geometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
};

struct Texture {
    int width = 0, height = 0;
    std::vector<Rml::byte> rgba;
};

struct Draw {
    std::shared_ptr<const Geometry> geometry;
    std::shared_ptr<const Texture> texture;
    Rml::Vector2f translation;
    Rml::Matrix4f transform = Rml::Matrix4f::Identity();
    Rml::Rectanglei scissor;
    bool scissor_enabled = false;
};

struct Frame {
    int width = 0, height = 0;
    std::vector<Draw> draws;
};

// RmlUi handles are client-thread owned. Frames keep immutable resources alive independently.
class Recorder final : public Rml::RenderInterface {
public:
    void Begin(int width, int height);
    std::shared_ptr<const Frame> End();
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override;
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override;
    void ReleaseTexture(Rml::TextureHandle) override;
    void EnableScissorRegion(bool) override;
    void SetScissorRegion(Rml::Rectanglei) override;
    void SetTransform(const Rml::Matrix4f*) override;

private:
    std::unordered_map<uintptr_t, std::shared_ptr<const Geometry>> geometries;
    std::unordered_map<uintptr_t, std::shared_ptr<const Texture>> textures;
    uintptr_t next_geometry = 1, next_texture = 1;
    std::shared_ptr<Frame> frame;
    Rml::Rectanglei scissor;
    bool scissor_enabled = false;
    Rml::Matrix4f transform = Rml::Matrix4f::Identity();
};

}
