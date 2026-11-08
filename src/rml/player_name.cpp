#include "player_name.hpp"
#include "view_support.hpp"
#include <RmlUi/Core.h>
#include <cstdio>

namespace cod4x::rml {
namespace {
// Packed RGBA values from rb_backend.c; 8/9 are supplied by the viewer's team palette.
const unsigned colors[] = {4278190080u,4281545727u,4278255360u,4286644223u,4294901760u,
    4294967040u,4294925567u,4294967295u,4294967295u,4294967295u,4278190296u,
    2013313084u,4280128759u,4291131648u,4291346093u,4287973248u,4278213029u};
class PlayerName final : public Rml::Element {
public:
    explicit PlayerName(const Rml::String& tag) : Element(tag) {}
protected:
    void OnUpdate() override {
        Element::OnUpdate();
        auto value = GetAttribute<Rml::String>("text", "");
        auto eight = GetAttribute<Rml::String>("color-8", ""), nine = GetAttribute<Rml::String>("color-9", "");
        if (eight.empty()) eight="#67b5d1";
        if (nine.empty()) nine="#d45a59";
        if (value == previous && eight == color8 && nine == color9) return;
        previous = value; color8 = eight; color9 = nine;
        if (value.size() > 512) value = "PLAYER";
        SetInnerRML("");
        Rml::String run, color;
        auto flush = [&]() {
            if (run.empty()) return;
            auto span = Rml::Factory::InstanceElement(this, "span", "span", {});
            if (!color.empty()) span->SetProperty("color", color);
            auto text = Rml::Factory::InstanceElement(span.get(), "#text", "#text", {});
            static_cast<Rml::ElementText*>(text.get())->SetText(run);
            span->AppendChild(std::move(text)); AppendChild(std::move(span)); run.clear();
        };
        for (size_t i=0; i<value.size(); ++i) {
            if (IsColorEscape(value,i)) {
                flush(); const unsigned index = unsigned(value[++i]-'0');
                if (index==8) color=eight;
                else if (index==9) color=nine;
                else {
                    const auto c=colors[index]; char hex[10];
                    std::snprintf(hex,sizeof(hex),"#%02x%02x%02x%02x",c&255,(c>>8)&255,(c>>16)&255,(c>>24)&255); color=hex;
                }
            } else run+=value[i];
        }
        flush();
    }
private:
    Rml::String previous, color8, color9;
};
}
void RegisterPlayerName() {
    static Rml::ElementInstancerGeneric<PlayerName> instancer;
    Rml::Factory::RegisterElementInstancer("player-name", &instancer);
}
}
