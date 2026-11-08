#pragma once
#include "rml_host.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <map>

namespace cod4x::rml {
// Last-seen dvar values for one data model; any change dirties the whole model.
class DvarSnapshot {
public:
    static Rml::String Fetch(const rml_host_t& host, const Rml::String& name) {
        const char* raw = host.get_value ? host.get_value(name.c_str()) : nullptr;
        return raw ? raw : "";
    }
    Rml::String Track(const Rml::String& name, Rml::String value, Rml::DataModelHandle& model) {
        auto found = values.find(name);
        if (found == values.end() || found->second != value) { values[name] = value; if (model) model.DirtyAllVariables(); }
        return value;
    }
    Rml::String Read(const rml_host_t& host, const Rml::String& name, Rml::DataModelHandle& model) { return Track(name, Fetch(host, name), model); }
    void Clear() { values.clear(); }
private:
    std::map<Rml::String, Rml::String> values;
};

// Fits the context to a base canvas; returns true when the size or density changed.
inline bool FitStage(Rml::Context* context, int width, int height, float base_width, float base_height, float min_density) {
    const float density = std::max(min_density, std::min(width / base_width, height / base_height));
    const bool resized = context->GetDimensions() != Rml::Vector2i(width, height) || context->GetDensityIndependentPixelRatio() != density;
    context->SetDimensions({width, height});
    context->SetDensityIndependentPixelRatio(density);
    return resized;
}
// A resize needs a second pass so layout settles at the new density.
inline void UpdateStage(Rml::Context* context, bool resized) { context->Update(); if (resized) context->Update(); }

// A CoD colour escape (^0 to ^@) starts at text[i]; <player-name> colours by it and StripColors drops it.
inline bool IsColorEscape(const Rml::String& text, size_t i) {
    return text[i] == '^' && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '@';
}
inline Rml::String StripColors(const Rml::String& name) {
    Rml::String plain;
    for (size_t i = 0; i < name.size(); ++i) {
        if (IsColorEscape(name, i)) ++i;
        else plain += name[i];
    }
    return plain;
}
// "images/<key>.tga" for an asset key, or empty when the key could form any other path.
inline Rml::String ImagePath(const Rml::String& key) {
    if (key.empty() || key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != Rml::String::npos) return {};
    return "images/" + key + ".tga";
}
// Roster tactical grenade icon from the published utility weapon ("weapon_flashbang" or the smoke default).
inline Rml::String UtilityImage(const Rml::String& utility) { return ImagePath(utility == "weapon_flashbang" ? "eq-flash" : "eq-smoke"); }

// Clock text as m:ss; minutes are zero-padded to minute_width digits.
inline Rml::String FormatClock(int total_seconds, size_t minute_width) {
    Rml::String minutes = std::to_string(total_seconds / 60);
    if (minutes.size() < minute_width) minutes.insert(0, minute_width - minutes.size(), '0');
    return minutes + (total_seconds % 60 < 10 ? ":0" : ":") + std::to_string(total_seconds % 60);
}
}
