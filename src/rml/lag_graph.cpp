#include "lag_graph.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <algorithm>
#include <cstdlib>

namespace cod4x::rml {
namespace {
constexpr int kColumns = 48;
// The stock meanings (blue, yellow, green, red) in the HUD's softer palette.
const Rml::Colourb kInterpolating(103, 168, 220), kExtrapolating(230, 196, 90), kSnapshot(140, 201, 122),
    kDropped(219, 130, 121), kMidline(255, 255, 255, 40);

int ParseList(const Rml::String& text, int* out) {
    int n = 0;
    const char* p = text.c_str();
    while (*p && n < kColumns) {
        char* end = nullptr;
        out[n++] = int(std::strtol(p, &end, 10));
        if (end == p) break;
        p = *end == ',' ? end + 1 : end;
    }
    return n;
}

class LagGraph final : public Rml::Element {
public:
    using Rml::Element::Element;
protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override { Element::OnAttributeChange(changed); dirty = true; }
    void OnResize() override { Element::OnResize(); dirty = true; }
    void OnRender() override {
        auto* render = GetRenderManager();
        if (!render) return;
        if (dirty || opacity != GetComputedValues().opacity()) { Build(*render); dirty = false; }
        geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
    }
private:
    void Build(Rml::RenderManager& render) {
        opacity = GetComputedValues().opacity();
        const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
        int frames[kColumns] = {}, snaps[kColumns] = {};
        const int frame_count = ParseList(GetAttribute<Rml::String>("frames", ""), frames);
        const int snap_count = ParseList(GetAttribute<Rml::String>("snaps", ""), snaps);
        const Rml::String delayed = GetAttribute<Rml::String>("delayed", "");
        const float column = size.x / kColumns, half = size.y * .25f, mid = size.y * .25f, base = size.y, band = size.y * .5f;
        auto colour = [&](Rml::Colourb c) { c.alpha = Rml::byte(c.alpha * opacity); return c.ToPremultiplied(); };
        Rml::Mesh mesh;
        Rml::MeshUtilities::GenerateQuad(mesh, {0, mid}, {size.x, 1}, colour(kMidline));
        for (int a = 0; a < kColumns; ++a) {
            const float x = size.x - (a + 1) * column;
            if (a < frame_count && frames[a]) {
                const float h = std::min(std::abs(frames[a]) / 300.f, 1.f) * half;
                if (frames[a] > 0) Rml::MeshUtilities::GenerateQuad(mesh, {x, mid - h}, {column, h}, colour(kExtrapolating));
                else Rml::MeshUtilities::GenerateQuad(mesh, {x, mid}, {column, h}, colour(kInterpolating));
            }
            if (a < snap_count && snaps[a]) {
                if (snaps[a] < 0) { Rml::MeshUtilities::GenerateQuad(mesh, {x, base - band}, {column, band}, colour(kDropped)); continue; }
                const float h = std::min(snaps[a] / 900.f, 1.f) * band;
                const bool late = size_t(a) < delayed.size() && delayed[size_t(a)] == '1';
                Rml::MeshUtilities::GenerateQuad(mesh, {x, base - h}, {column, h}, colour(late ? kExtrapolating : kSnapshot));
            }
        }
        geometry = render.MakeGeometry(std::move(mesh));
    }
    Rml::Geometry geometry;
    bool dirty = true;
    float opacity = 1;
};
}

void RegisterLagGraph() {
    static Rml::ElementInstancerGeneric<LagGraph> instancer;
    Rml::Factory::RegisterElementInstancer("lag-graph", &instancer);
}
}
