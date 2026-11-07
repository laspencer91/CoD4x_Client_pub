#include "decrypt_text.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cod4x::rml {
namespace {
uint32_t next_seed = 0;

class DecryptText final : public Rml::Element {
public:
    explicit DecryptText(const Rml::String& tag) : Element(tag), seed(++next_seed * 2654435761u) {
        reserve = AddLayer("decrypt-reserve");
        output = AddLayer("decrypt-output");
    }

protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override {
        Element::OnAttributeChange(changed);
        for (const char* name : {"text", "tick-ms", "cycles", "glitch", "instant", "replay"})
            if (changed.count(name)) restart = true;
    }

    void OnUpdate() override {
        Element::OnUpdate();
        const double now = Rml::GetSystemInterface()->GetElapsedTime();
        if (restart) {
            restart = false; start = now; ++generation;
            text = GetAttribute<Rml::String>("text", ""); reserve->SetText(text); displayed.clear();
            interval = std::clamp(GetAttribute<int>("tick-ms", 18), 8, 80) / 1000.0;
            cycles = std::clamp(GetAttribute<int>("cycles", 3), 2, 3);
            glyphs.clear();
            if (text.size() <= 4096) {
                const char* end = text.data() + text.size();
                for (const char* p = text.data(); p < end;) {
                    const char* next = Rml::StringUtilities::SeekForwardUTF8(p + 1, end);
                    glyphs.emplace_back(p, next); p = next;
                }
            }
            active = !text.empty() && glyphs.size() <= 256 && !glyphs.empty() && !HasAttribute("instant");
            if (!active) output->SetText(text);
        }
        if (!active) { SetClass("decrypting", false); SetClass("decrypt-shift", false); return; }

        size_t tick = size_t(std::min(100000.0, std::max(0.0, std::floor((now - start) / interval + 0.000001))));
        Rml::String shown;
        bool scrambling = false, finished = true;
        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto& glyph = glyphs[i];
            const bool alphanumeric = glyph.size() == 1 && ((glyph[0] >= 'A' && glyph[0] <= 'Z') ||
                (glyph[0] >= 'a' && glyph[0] <= 'z') || (glyph[0] >= '0' && glyph[0] <= '9'));
            if (!alphanumeric) { shown += glyph; continue; }
            if (tick >= size_t(cycles + 1)) { shown += glyph; tick -= cycles + 1; continue; }
            finished = false;
            if (tick == size_t(cycles)) shown += glyph;
            else { shown += Scramble(i, tick, glyph[0]); scrambling = true; }
            break;
        }
        if (finished) { shown = text; active = false; }
        if (shown != displayed) { output->SetText(shown); displayed = shown; }
        SetClass("decrypting", scrambling);
        SetClass("decrypt-shift", scrambling && HasAttribute("glitch") && (tick % 2 == 0));
        if (active && GetContext()) GetContext()->RequestNextUpdate(interval);
    }

private:
    Rml::ElementText* AddLayer(const char* class_name) {
        auto layer = Rml::Factory::InstanceElement(this, "span", "span", {});
        layer->SetClass(class_name, true);
        auto node = Rml::Factory::InstanceElement(layer.get(), "#text", "#text", {});
        auto* result = static_cast<Rml::ElementText*>(node.get());
        layer->AppendChild(std::move(node)); AppendChild(std::move(layer)); return result;
    }
    char Scramble(size_t index, size_t phase, char correct) const {
        Rml::String alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        alphabet.erase(std::remove(alphabet.begin(), alphabet.end(), correct), alphabet.end());
        uint32_t hash = seed ^ (generation * 2246822519u) ^ (uint32_t(index) * 3266489917u);
        hash ^= hash >> 16; hash *= 2246822519u; hash ^= hash >> 13;
        return alphabet[(hash % alphabet.size() + phase * 11) % alphabet.size()];
    }
    Rml::ElementText *reserve, *output;
    Rml::String text, displayed;
    std::vector<Rml::String> glyphs;
    uint32_t seed, generation = 0;
    double start = 0, interval = 0.018;
    int cycles = 3;
    bool restart = true, active = false;
};
}

void RegisterDecryptText() {
    static Rml::ElementInstancerGeneric<DecryptText> instancer;
    Rml::Factory::RegisterElementInstancer("decrypt-text", &instancer);
}
void ReplayDecryptText(Rml::Element* root) {
    if (!root) return;
    Rml::ElementList elements; root->GetElementsByTagName(elements, "decrypt-text");
    for (auto* element : elements) element->SetAttribute("replay", element->GetAttribute<int>("replay", 0) + 1);
}
}
