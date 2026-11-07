#include "modal.hpp"
#include <RmlUi/Core.h>

namespace cod4x::rml {
namespace {
class Modal final : public Rml::Element {
public:
    explicit Modal(const Rml::String& tag) : Element(tag) {}
protected:
    void ProcessDefaultAction(Rml::Event& event) override {
        Element::ProcessDefaultAction(event);
        if (event.GetType() != "click" || event.GetParameter<int>("button", 0) != 0 || !HasAttribute("dismiss-on-backdrop")) return;
        const auto setting = GetAttribute<Rml::String>("dismiss-on-backdrop", "");
        if (setting == "false" || setting == "0") return;
        for (auto* target = event.GetTargetElement(); target && target != this; target = target->GetParentNode())
            if (target->HasAttribute("modal-panel")) return;
        DispatchEvent("modalclose", {{"reason", Rml::Variant(Rml::String("backdrop"))}});
        event.StopPropagation();
    }
};
}
void RegisterModal() {
    static Rml::ElementInstancerGeneric<Modal> instancer;
    Rml::Factory::RegisterElementInstancer("ui-modal", &instancer);
}
}
