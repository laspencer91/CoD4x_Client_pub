#pragma once
#include "rml_host.h"
#include <RmlUi/Core.h>
#include "view_support.hpp"

namespace cod4x::rml {
struct BroadcastPlayer {
    Rml::String key, name, health, slot, weapon, utility, fill="0%", top="0dp"; // style bindings need valid values
    bool present=false, condensed=false, fresh=false, axis=false, alive=false, observed=false, frag=false;
};
class BroadcastView {
public:
    bool Prepare(const rml_host_t& host, int width, int height, double seconds);
    bool Reload();
    void Render();
    void Reset();
    // The followed player's plate is on screen (it replaces the engine's follow text).
    bool FollowShown() const { return document && observer && follow.present; }
private:
    Rml::Context* context=nullptr;
    Rml::ElementDocument* document=nullptr;
    Rml::DataModelHandle model;
    std::vector<BroadcastPlayer> players;
    BroadcastPlayer follow;
    DvarSnapshot values;
    // Row and header heights in dp, measured from the stylesheet on each document load.
    float expanded_height=42, condensed_height=34, dead_height=30, header_height=28;
    void MeasureCards();
    Rml::String team_style, team_factions, left_score, right_score, clock, round, left_alive, right_alive, name_color8, name_color9, header_top;
    enum class CardMode { Expanded, Compact, Hidden };
    CardMode last_visible_mode=CardMode::Expanded;
    bool observer=false, relative=false, compact=false, hidden=false, planted=false, armor_visible=false;
    int armor_charges=0;
    double retry_at=0;
};
}
