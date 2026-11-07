#pragma once
#include "rml_host.h"
#include <RmlUi/Core/Types.h>

namespace cod4x::rml {
// Semantic UI cues: one hover cue per control change, rate limited, and muted briefly after a click.
class UiAudio {
public:
    void Hover(const Rml::String& control, bool audible, const rml_host_t& host, double now) {
        if (control == current) return;
        current = control;
        if (!audible || now < muted_until || now - last_hover < 0.075 || !host.play_sound) return;
        host.play_sound("mouse_over"); last_hover = now;
    }
    void Click(const rml_host_t& host, double now) {
        muted_until = now + 0.35;
        if (host.play_sound) host.play_sound("mouse_click");
    }
    void Leave() { current.clear(); }
    // Treat `now` as a recent cue so opening a screen under the cursor stays silent.
    void Reset(double now = -1) { current.clear(); last_hover = now; muted_until = -1; }
private:
    Rml::String current;
    double last_hover = -1, muted_until = -1;
};
}
