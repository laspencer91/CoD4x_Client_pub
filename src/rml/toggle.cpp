#include "toggle.hpp"
#include <RmlUi/Core.h>

namespace cod4x::rml {
namespace {
class Toggle final : public Rml::Element {
public:
    explicit Toggle(const Rml::String& tag) : Element(tag) {
        auto hit = Rml::Factory::InstanceElement(this, "input", "input", {});
        hit->SetAttribute("type", "button"); hit->SetAttribute("value", "");
        hit->SetClass("control", true); hit->SetClass("toggle-hit", true);
        button = AppendChild(std::move(hit));
        auto label = Rml::Factory::InstanceElement(this, "span", "span", {});
        label->SetClass("toggle-label", true);
        auto text = Rml::Factory::InstanceElement(label.get(), "#text", "#text", {});
        label_text = static_cast<Rml::ElementText*>(text.get());
        label->AppendChild(std::move(text)); AppendChild(std::move(label));
        auto track = Rml::Factory::InstanceElement(this, "span", "span", {});
        track->SetClass("toggle-track", true);
        auto thumb = Rml::Factory::InstanceElement(track.get(), "span", "span", {});
        thumb->SetClass("toggle-thumb", true); track->AppendChild(std::move(thumb)); AppendChild(std::move(track));
    }
protected:
    void OnAttributeChange(const Rml::ElementAttributes& attributes) override {
        Element::OnAttributeChange(attributes);
        for (const auto& entry : attributes) {
            const auto& name = entry.first;
            if (name == "disabled" || name == "data-econ" || name == "data-action" || name == "data-help" || name == "data-value") {
                if (HasAttribute(name)) button->SetAttribute(name, *GetAttribute(name));
                else button->RemoveAttribute(name);
            }
        }
    }
    void OnUpdate() override {
        Element::OnUpdate();
        const bool checked = HasAttribute("checked");
        SetClass("checked", checked); SetClass("disabled", HasAttribute("disabled"));
        const auto label = GetAttribute<Rml::String>(checked ? "on-label" : "off-label", checked ? "ON" : "OFF");
        if (label != displayed) { displayed = label; label_text->SetText(label); }
    }
private:
    Rml::Element* button;
    Rml::ElementText* label_text;
    Rml::String displayed;
};
}
void RegisterToggle() {
    static Rml::ElementInstancerGeneric<Toggle> instancer;
    Rml::Factory::RegisterElementInstancer("ui-toggle", &instancer);
}
}
