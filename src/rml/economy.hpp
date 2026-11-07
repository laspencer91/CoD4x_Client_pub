#pragma once
#include "rml_host.h"
#include "economy_preview.hpp"
#include <RmlUi/Core.h>
#include <map>

namespace cod4x::rml {
struct EconomyOption {
    Rml::String key, name, detail, state, price, token, icon, utility;
    bool enabled = false, selected = false, owned = false;
};
struct RebuyOption {
    Rml::String key, name, priority, price;
    bool can_move_up = false, can_move_down = false;
};
class EconomyView {
public:
    void Init(Rml::Context* context);
    void Reset();
    void Update(const rml_host_t& services, bool fixture, double seconds);
    bool Event(Rml::Event& event, const rml_host_t& services, bool fixture, double seconds);
    void Dialog(bool value);
    bool Shortcut(int key, const rml_host_t& services, bool fixture, double seconds);
    bool ResolveFocus();
    void Dismiss(const Rml::String& action, const rml_host_t& host, bool fixture, double seconds);
private:
    bool rebuy_empty = true;
    Rml::DataModelHandle model;
    Rml::Context* ui_context = nullptr;
    int pending_focus = 0;
    void TogglePicker(bool primary);
    std::vector<EconomyOption> roles, weapons, pistols, mates;
    std::vector<RebuyOption> rebuy_rows;
    EconomyPreview preview_state;
    Rml::String held_rebuy;
    std::map<Rml::String, Rml::String> values;
    Rml::String wallet, phase, countdown, map, team, primary, secondary;
    Rml::String armor_price, frag_price, tactical, camo, feedback, deploy_label;
    Rml::String demo_price, focus_body, balance_change;
    Rml::String primary_icon, secondary_icon, tactical_icon, camo_icon;
    Rml::String suppressor_price, reflex_price, pistol_price, class_family;
    int teammate_count = 0;
    bool scope = false, buy_warning = false, buy_urgent = false;
    bool buy_open = false, purchase_allowed = false, has_class = false, armor_buy = false, frag_buy = false;
    bool armor_one = false, armor_two = false, armor_refund = false, frag_owned = false, frag_refund = false;
    bool suppressor_buy = false, reflex_buy = false, pistol_buy = false, repeat = false;
    bool primary_silencer = false, primary_reflex = false, secondary_silencer = false, primary_none = true;
    bool confirmation = false, primary_picker = false, pistol_picker = false, pending = false, preview = false;
    bool debit = false;
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
