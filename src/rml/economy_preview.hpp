#pragma once
#include <RmlUi/Core/Types.h>
#include <map>

namespace cod4x::rml {
// UI-only fixture. Live cash and transactions always come from the game server.
class EconomyPreview {
public:
    Rml::String Value(const Rml::String& name);
    void Apply(const Rml::String& action);
    void Reset();
private:
    std::map<Rml::String, Rml::String> fields;
    int Number(const Rml::String& name);
    bool Spend(int amount);
    void Sync();
    void WeaponRows();
};
}
