#pragma once
#include "rml_host.h"
#include <RmlUi/Core.h>
#include <map>

namespace cod4x::rml {
struct EconomyOption {
    Rml::String key, name, detail, state, price, token, icon;
    bool enabled = false, selected = false, owned = false;
};
class EconomyView {
public:
    void Init(Rml::Context* context);
    void Reset();
    void Update(const rml_host_t& services, bool fixture, double seconds);
    bool Event(Rml::Event& event, const rml_host_t& services, bool fixture, double seconds);
    void Dialog(bool value);
    bool Shortcut(int key, const rml_host_t& services, bool fixture, double seconds);
private:
    Rml::DataModelHandle model;
    std::vector<EconomyOption> roles, weapons, pistols, mates;
    std::map<Rml::String, Rml::String> values;
    Rml::String wallet, phase, countdown, map, team, primary, secondary, attachment, pistol_attachment;
    Rml::String armor_state, armor_price, frag_state, frag_price, tactical, camo, feedback, deploy_label, repeat_label;
    Rml::String demo_price, focus_title, focus_body, balance_change, refund_label;
    Rml::String suppressor_label, reflex_label, pistol_label;
    Rml::String primary_icon, secondary_icon, tactical_icon;
    bool buy_open = false, purchase_allowed = false, has_class = false, armor_buy = false, frag_buy = false;
    bool armor_one = false, armor_two = false, armor_refund = false, frag_owned = false, frag_refund = false;
    bool suppressor_buy = false, reflex_buy = false, reflex_free = false, pistol_buy = false, repeat = false;
    bool primary_silencer = false, primary_reflex = false, secondary_silencer = false, primary_none = true, secondary_none = true;
    bool confirmation = false, primary_picker = false, pistol_picker = false, pending = false, preview = false;
    bool debit = false, credit = false;
    double submitted = 0;
    int previous_cash = -1;
    Rml::String held_gift;
    Rml::String hovered;
    double last_hover = -1;
    Rml::String Value(const rml_host_t&, const Rml::String&, bool);
    bool Submit(const Rml::String&, const rml_host_t&, bool, double);
};
bool EconomyActionAllowed(const char* response);
}
