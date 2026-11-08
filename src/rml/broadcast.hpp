#pragma once
#include "rml_host.h"
#include <RmlUi/Core.h>
#include "view_support.hpp"

namespace cod4x::rml {
// One roster slot: an observer's card, or a player's tile beside the score bar. A player's own-team tile (own) shows
// alive state and health; the other team's tile (other) shows identity only.
struct BroadcastPlayer {
    Rml::String key, name, plain, health, slot, weapon, utility, initials, avatar, ghost, fill="0%", top="0dp"; // style bindings need valid values
    bool present=false, condensed=false, fresh=false, axis=false, alive=false, observed=false, frag=false;
    bool own=false, other=false, low=false, hit=false;
    bool operator==(const BroadcastPlayer& o) const {
        return key==o.key && name==o.name && plain==o.plain && health==o.health && slot==o.slot && weapon==o.weapon && utility==o.utility &&
            initials==o.initials && avatar==o.avatar && ghost==o.ghost && fill==o.fill && top==o.top && present==o.present &&
            condensed==o.condensed && fresh==o.fresh && axis==o.axis && alive==o.alive && observed==o.observed &&
            frag==o.frag && own==o.own && other==o.other && low==o.low && hit==o.hit;
    }
    bool operator!=(const BroadcastPlayer& o) const { return !(*this==o); }
};
// A line of the stock FPS readout as the engine formatted it (cl_rml.c rml_corner_*): its text and label.
struct CornerLine {
    Rml::String text, label;
    bool present=false;
    bool operator==(const CornerLine& o) const { return text==o.text && label==o.label && present==o.present; }
    bool operator!=(const CornerLine& o) const { return !(*this==o); }
};
class BroadcastView {
public:
    bool Prepare(const rml_host_t& host, int width, int height, double seconds);
    bool Reload();
    void Render();
    void Reset();
    // The followed player's plate is on screen (it replaces the engine's follow text): an observer's followed player
    // or a dead player's followed teammate.
    bool FollowShown() const { return document && follow.present; }
private:
    Rml::Context* context=nullptr;
    Rml::ElementDocument* document=nullptr;
    Rml::DataModelHandle model;
    std::vector<BroadcastPlayer> players;
    BroadcastPlayer follow;
    std::vector<CornerLine> corner_lines;
    bool corner_labels=true;
    // The stock lagometer's 48 newest samples (cl_rml.c rml_lagometer_*), drawn by <lag-graph>.
    Rml::String lag_frames, lag_snaps, lag_delayed;
    bool lag_on=false, lag_snc=false;
    DvarSnapshot values;
    // Row heights in dp, measured from the stylesheet on each document load.
    float expanded_height=42, condensed_height=34, dead_height=30;
    void MeasureCards();
    Rml::String team_style, team_factions, left_score, right_score, clock, round, left_alive, right_alive;
    enum class CardMode { Expanded, Compact, Hidden };
    CardMode last_visible_mode=CardMode::Expanded;
    bool observer=false, relative=false, compact=false, hidden=false, planted=false, armor_visible=false;
    int armor_charges=0;
    double retry_at=0;
};
}
