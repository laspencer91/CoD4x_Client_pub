#pragma once
#include "rml_host.h"
#include <RmlUi/Core.h>
#include <map>

namespace cod4x::rml {
struct BroadcastPlayer {
    Rml::String key, name, health, slot, weapon, utility, fill, bottom;
    bool present=false, condensed=false, fresh=false, axis=false, alive=false, observed=false, frag=false;
};
class BroadcastView {
public:
    bool Prepare(const rml_host_t& host, int width, int height, double seconds);
    bool Reload();
    void Render();
    void Reset();
private:
    Rml::Context* context=nullptr;
    Rml::ElementDocument* document=nullptr;
    Rml::DataModelHandle model;
    std::vector<BroadcastPlayer> players;
    std::map<Rml::String,Rml::String> values;
    Rml::String left_score, right_score, clock, round, target, left_alive, right_alive, phase, name_color8, name_color9, left_header_bottom, right_header_bottom;
    enum class CardMode { Expanded, Compact, Hidden };
    CardMode last_visible_mode=CardMode::Expanded;
    bool observer=false, compact=false, hidden=false, planted=false;
    double retry_at=0;
};
}
