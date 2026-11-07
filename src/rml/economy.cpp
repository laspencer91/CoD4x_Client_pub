#include "economy.hpp"
#include "native_keys.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace cod4x::rml {
namespace {
const char* role_keys[] = {"assault", "specops", "sniper", "demolitions"};
const char* role_names[] = {"ASSAULT", "SPEC OPS", "SCOPE", "DEMOLITION"};
const char* pistol_keys[] = {"deserteagle", "deserteaglegold", "colt45", "usp", "beretta"};
Rml::String WeaponName(const Rml::String& key) {
    static const std::map<Rml::String,Rml::String> names = {{"ak47","AK-47"},{"m4","M4 CARBINE"},{"m16","M16A4"},
        {"g3","G3"},{"g36c","G36C"},{"m14","M14"},{"mp44","MP44"},{"mp5","MP5"},{"ak74u","AK-74u"},
        {"uzi","MINI-UZI"},{"m40a3","M40A3"},{"remington700","R700"},{"winchester1200","W1200"},{"m1014","M1014"},
        {"deserteagle","DESERT EAGLE"},{"deserteaglegold","GOLD EAGLE"},{"colt45","M1911"},{"usp","USP .45"},{"beretta","M9"}};
    auto found = names.find(key); return found == names.end() ? key : found->second;
}
Rml::Element* Control(Rml::Element* element) {
    while (element && !element->HasAttribute("data-econ")) element = element->GetParentNode();
    return element;
}
const std::map<Rml::String,Rml::String> fixture = {
    {"economy_cash","250"},{"economy_buy_open","1"},{"economy_purchase_allowed","1"},{"flat_has_class","1"},
    {"flat_buy_seconds","24"},{"flat_team","ATTACK"},{"mapname","mp_crash"},{"loadout_class","assault"},
    {"loadout_primary","ak47"},{"loadout_secondary","deserteagle"},{"loadout_primary_attachment","none"},{"loadout_secondary_attachment","none"},
    {"flat_class_name","Assault"},{"flat_utility_name","SMOKE"},{"flat_camo_label","Woodland"},{"flat_primary_rows","7"},
    {"economy_armor_price","100"},{"economy_armor_charges","0"},{"economy_armor_capacity","2"},{"economy_can_buy_armor","1"},
    {"economy_frag_price","100"},{"economy_can_buy_frag","1"},{"economy_suppressor_price","50"},
    {"economy_red_dot_price","100"},{"economy_red_dot_supported","1"},{"economy_can_buy_red_dot","1"},
    {"economy_demolition_available","1"},{"economy_demolition_price","300"},{"flat_status",""},
    {"flat_assault_capacity","3 / 5"},{"flat_assault_allowed","1"},{"flat_specops_capacity","1 / 2"},{"flat_specops_allowed","1"},
    {"flat_sniper_capacity","1 / 1"},{"flat_sniper_allowed","0"},{"flat_demolitions_capacity","0 / 1"},
    {"flat_primary_0_ref","m16"},{"flat_primary_1_ref","ak47"},{"flat_primary_2_ref","m4"},{"flat_primary_3_ref","g3"},
    {"flat_primary_4_ref","g36c"},{"flat_primary_5_ref","m14"},{"flat_primary_6_ref","mp44"},
    {"flat_mate_0_exists","1"},{"flat_mate_0_name","YOU"},{"flat_mate_0_class","Assault"},{"flat_mate_0_cash","$250"},
    {"flat_mate_1_exists","1"},{"flat_mate_1_name","MAVERICK"},{"flat_mate_1_class","Spec Ops"},{"flat_mate_1_cash","$50"},{"flat_mate_1_can_give","1"},
    {"flat_mate_1_give","openscriptmenu flat_loadout give:player_1_2"},
    {"flat_mate_2_exists","1"},{"flat_mate_2_name","GHOST"},{"flat_mate_2_class","Scope"},{"flat_mate_2_cash","$100"},{"flat_mate_2_can_give","1"},
    {"flat_mate_2_give","openscriptmenu flat_loadout give:player_1_3"}
};
}

bool EconomyActionAllowed(const char* input) {
    if (!input) return false;
    const Rml::String action(input);
    static const char* exact[] = {"close","deploy","spectate","repeat_buy","toggle_tactical","camo:next","preview_frag","buy_armor",
        "refund_frag","refund_armor","refund_silencer","refund_reflex","refund_pistol_silencer","confirm_demolition","cancel_demolition",
        "attachment:none","attachment:silencer","attachment:reflex","pistol_attachment:none","pistol_attachment:silencer"};
    for (const char* value : exact) if (action == value) return true;
    for (const char* value : role_keys) if (action == Rml::String("select:") + value) return true;
    for (const char* value : pistol_keys) if (action == Rml::String("secondary:") + value) return true;
    if (action.size() == 14 && action.compare(0, 13, "pick_primary:") == 0) return action[13] >= '0' && action[13] <= '6';
    if (action.compare(0, 12, "give:player_") == 0 && action.size() < 80)
        return std::all_of(action.begin() + 12, action.end(), [](unsigned char c) { return std::isdigit(c) || c == '_'; });
    return false;
}

void EconomyView::Init(Rml::Context* context) {
    auto constructor = context->CreateDataModel("economy");
    auto row = constructor.RegisterStruct<EconomyOption>();
#define ROW(field) row.RegisterMember(#field, &EconomyOption::field)
    ROW(key); ROW(name); ROW(detail); ROW(state); ROW(price); ROW(token); ROW(icon); ROW(enabled); ROW(selected); ROW(owned);
#undef ROW
    constructor.RegisterArray<std::vector<EconomyOption>>();
#define BIND(field) constructor.Bind(#field, &field)
    BIND(roles); BIND(weapons); BIND(pistols); BIND(mates); BIND(wallet); BIND(phase); BIND(countdown); BIND(map); BIND(team);
    BIND(primary); BIND(secondary); BIND(attachment); BIND(pistol_attachment); BIND(armor_state); BIND(armor_price); BIND(frag_state); BIND(frag_price);
    BIND(tactical); BIND(camo); BIND(feedback); BIND(deploy_label); BIND(repeat_label); BIND(demo_price); BIND(focus_title); BIND(focus_body);
    BIND(suppressor_label); BIND(reflex_label); BIND(pistol_label);
    BIND(primary_icon); BIND(secondary_icon); BIND(tactical_icon);
    BIND(buy_open); BIND(purchase_allowed); BIND(has_class); BIND(armor_buy); BIND(frag_buy); BIND(armor_one); BIND(armor_two);
    BIND(armor_refund); BIND(frag_owned); BIND(frag_refund); BIND(suppressor_buy); BIND(reflex_buy); BIND(reflex_free); BIND(pistol_buy); BIND(repeat);
    BIND(primary_silencer); BIND(primary_reflex); BIND(secondary_silencer); BIND(primary_none); BIND(secondary_none);
    BIND(confirmation); BIND(primary_picker); BIND(pistol_picker); BIND(pending); BIND(preview); BIND(debit); BIND(credit); BIND(balance_change);
#undef BIND
    model = constructor.GetModelHandle();
    focus_title = ""; focus_body = "";
}
void EconomyView::Reset() { model = {}; values.clear(); roles.clear(); weapons.clear(); pistols.clear(); mates.clear(); previous_cash = -1; pending = false; }
void EconomyView::Dialog(bool value) { confirmation = value; primary_picker = pistol_picker = false; if (model) model.DirtyVariable("confirmation"); }
Rml::String EconomyView::Value(const rml_host_t& host, const Rml::String& name, bool use_fixture) {
    Rml::String result;
    if (use_fixture) {
        auto it = fixture.find(name); if (it != fixture.end()) result = it->second;
        else if (name.compare(0, 11, "weap_allow_") == 0 || name.compare(0, 13, "attach_allow_") == 0) result = "1";
    } else if (host.get_value) { const char* value = host.get_value(name.c_str()); if (value) result = value; }
    auto found = values.find(name);
    if (found == values.end() || found->second != result) { values[name] = result; model.DirtyAllVariables(); }
    return result;
}
void EconomyView::Update(const rml_host_t& host, bool use_fixture, double seconds) {
    if (!model) return;
    auto value = [&](const Rml::String& key) { return Value(host, key, use_fixture); };
    auto number = [&](const Rml::String& key) { return std::atoi(value(key).c_str()); };
    preview = use_fixture;
    const auto before = values;
    const int cash = number("economy_cash"); wallet = "$" + std::to_string(cash);
    buy_open = number("economy_buy_open") != 0; purchase_allowed = number("economy_purchase_allowed") != 0;
    has_class = number("flat_has_class") != 0;
    phase = !buy_open ? "BUY CLOSED" : !purchase_allowed ? "WAIT FOR SPAWN" : "BUY PHASE";
    const int seconds_left = std::max(0, std::min(number("flat_buy_seconds"), 999));
    countdown = (seconds_left / 60 < 10 ? "0" : "") + std::to_string(seconds_left / 60) + ":" +
        (seconds_left % 60 < 10 ? "0" : "") + std::to_string(seconds_left % 60);
    if (!buy_open) countdown = "";
    map = value("mapname"); if (map.compare(0,3,"mp_") == 0) map.erase(0,3);
    std::transform(map.begin(), map.end(), map.begin(), [](unsigned char c){return char(std::toupper(c));});
    team = value("flat_team"); demo_price = "$" + value("economy_demolition_price");
    const auto selected = value("loadout_class");
    roles.clear();
    for (int i = 0; i < 4; ++i) {
        EconomyOption row; row.key = role_keys[i]; row.name = role_names[i]; row.selected = selected == row.key;
        static const char* role_icons[] = {"ak47", "mp5", "m40a3", "winchester1200"};
        row.icon = Rml::String("images/") + role_icons[i] + ".tga";
        row.detail = value("flat_" + row.key + "_capacity"); row.enabled = number("flat_" + row.key + "_allowed") || row.selected;
        if (i == 3) { row.owned = number("economy_demolition_owned"); row.price = row.owned ? "RESERVED" : demo_price;
            row.enabled = row.selected || row.owned || (buy_open && purchase_allowed && number("economy_demolition_available") && cash >= number("economy_demolition_price")); }
        row.state = row.selected ? "SELECTED" : !row.enabled ? (i == 3 && cash < number("economy_demolition_price") ? "NEED " + demo_price : "UNAVAILABLE") : "AVAILABLE";
        roles.push_back(std::move(row));
    }
    primary = WeaponName(value("loadout_primary")); secondary = WeaponName(value("loadout_secondary"));
    auto icon = [](const Rml::String& key) { return key.empty() ? Rml::String() : "images/" + key + ".tga"; };
    primary_icon = icon(value("loadout_primary")); secondary_icon = icon(value("loadout_secondary"));
    attachment = value("loadout_primary_attachment"); pistol_attachment = value("loadout_secondary_attachment");
    primary_none = attachment == "none"; primary_silencer = attachment == "silencer"; primary_reflex = attachment == "reflex";
    secondary_none = pistol_attachment == "none"; secondary_silencer = pistol_attachment == "silencer";
    suppressor_buy = has_class && (selected == "assault" || selected == "specops") && value("loadout_primary") != "mp44" &&
        number("attach_allow_" + selected + "_silencer") && (number("economy_suppressor_primary_owned") || (buy_open && purchase_allowed && cash >= number("economy_suppressor_price")));
    reflex_free = number("economy_red_dot_free");
    reflex_buy = has_class && number("economy_red_dot_supported") && (number("economy_can_buy_red_dot") || number("economy_red_dot_owned") || reflex_free);
    pistol_buy = has_class && value("loadout_secondary") != "deserteagle" && value("loadout_secondary") != "deserteaglegold" && number("attach_allow_pistol_silencer") &&
        (number("economy_suppressor_secondary_owned") || (buy_open && purchase_allowed && cash >= number("economy_suppressor_price")));
    suppressor_label = number("economy_suppressor_primary_owned") ? "SUPPRESSOR / OWNED" : "SUPPRESSOR / $" + value("economy_suppressor_price");
    reflex_label = reflex_free ? "RED DOT / INCLUDED" : number("economy_red_dot_owned") ? "RED DOT / OWNED" : "RED DOT / $" + value("economy_red_dot_price");
    pistol_label = number("economy_suppressor_secondary_owned") ? "SUPPRESSOR / OWNED" : "SUPPRESSOR / $" + value("economy_suppressor_price");
    weapons.clear(); const int count = std::max(0, std::min(number("flat_primary_rows"), 7));
    for (int i=0; i<count; ++i) { EconomyOption row; const auto ref = value("flat_primary_" + std::to_string(i) + "_ref");
        row.key = std::to_string(i); row.name = WeaponName(ref); row.icon = icon(ref); row.enabled = has_class && !ref.empty(); row.selected = ref == value("loadout_primary"); weapons.push_back(row); }
    pistols.clear();
    for (const char* ref : pistol_keys) { EconomyOption row; row.key=ref; row.name=WeaponName(ref); row.icon=icon(ref); row.enabled=has_class && number("weap_allow_"+row.key); row.selected=row.key==value("loadout_secondary"); pistols.push_back(row); }
    const int charges = number("economy_armor_charges"); armor_one = charges >= 1; armor_two = charges >= 2;
    armor_state = charges >= 2 ? "2 / 2 CHARGES" : charges == 1 ? "1 / 2 CHARGES" : "NO ARMOR";
    armor_price = "$" + value("economy_armor_price"); armor_buy = has_class && number("economy_can_buy_armor");
    armor_refund = buy_open && purchase_allowed && number("economy_armor_refundable");
    frag_owned = number("economy_frag_owned"); frag_state = frag_owned ? "OWNED / x1" : "NOT EQUIPPED";
    frag_price = "$" + value("economy_frag_price"); frag_buy = has_class && number("economy_can_buy_frag"); frag_refund = number("economy_frag_refundable");
    tactical = value("flat_utility_name"); camo = value("flat_camo_label"); repeat = number("economy_repeat_enabled"); repeat_label = repeat ? "AUTO-REBUY / ON" : "AUTO-REBUY / OFF";
    tactical_icon = icon(tactical == "SMOKE" ? "smoke" : "flash");
    feedback = value("flat_status"); deploy_label = buy_open ? "DEPLOY" : "SAVE & CLOSE";
    mates.clear();
    for (int i=0; i<5; ++i) { const Rml::String prefix = "flat_mate_"+std::to_string(i)+"_"; EconomyOption row;
        row.key=std::to_string(i); row.name=value(prefix+"name"); row.detail=value(prefix+"class"); row.price=value(prefix+"cash");
        row.owned=number(prefix+"frag"); row.enabled=buy_open && number(prefix+"can_give"); row.selected=number(prefix+"exists");
        const auto command=value(prefix+"give"); const Rml::String expected="openscriptmenu flat_loadout give:";
        if(command.compare(0,expected.size(),expected)==0) row.token=command.substr(expected.size());
        row.enabled = row.enabled && EconomyActionAllowed(("give:"+row.token).c_str());
        if(!row.selected) {row.name="";row.detail="";row.price="";} mates.push_back(row); }
    if (previous_cash >= 0 && previous_cash != cash) { int delta=cash-previous_cash; credit=delta>0; debit=delta<0;
        balance_change=(delta>0?"+ $":"- $")+std::to_string(std::abs(delta)); model.DirtyVariable("balance_change"); }
    previous_cash=cash;
    if(pending && (before != values || seconds-submitted>2.0)) {pending=false; model.DirtyVariable("pending");}
    if(before != values) model.DirtyAllVariables();
}
bool EconomyView::Submit(const Rml::String& action, const rml_host_t& host, bool use_fixture, double seconds) {
    if (!EconomyActionAllowed(action.c_str())) return false;
    if(use_fixture) {
        if(action=="close") return true;
        focus_title="DESIGN PREVIEW"; focus_body="Preview actions do not spend money or change your live loadout.";
        model.DirtyVariable("focus_title"); model.DirtyVariable("focus_body"); return false;
    }
    if(host.menu_response && host.menu_response(action.c_str())) { pending=true; submitted=seconds; model.DirtyVariable("pending"); }
    return false;
}
bool EconomyView::Event(Rml::Event& event, const rml_host_t& host, bool use_fixture, double seconds) {
    auto* control=Control(event.GetTargetElement()); if(!control) return false;
    const auto action=control->GetAttribute<Rml::String>("data-econ", "");
    if(event.GetType()=="mousedown" && event.GetParameter<int>("button",0)==1) {
        const auto refund=control->GetAttribute<Rml::String>("data-refund", "");
        if((refund=="refund_armor" && armor_refund) || (refund=="refund_frag" && frag_refund) ||
            (buy_open && purchase_allowed && (refund=="refund_silencer" || refund=="refund_reflex" || refund=="refund_pistol_silencer"))) Submit(refund,host,use_fixture,seconds);
        return false;
    }
    if(event.GetType()=="mousedown" || event.GetType()=="keydown") held_gift=control->GetAttribute<Rml::String>("data-token", "");
    if(event.GetType()=="mouseover" || event.GetType()=="focus") {
        const auto identity=action+control->GetAttribute<Rml::String>("data-value", "")+control->GetAttribute<Rml::String>("data-token", "");
        if(identity!=hovered) {hovered=identity;if(seconds-last_hover>=0.075 && host.play_sound){host.play_sound("mouse_over");last_hover=seconds;}}
        focus_title=control->GetAttribute<Rml::String>("data-title", ""); focus_body=control->GetAttribute<Rml::String>("data-help", "");
        model.DirtyVariable("focus_title"); model.DirtyVariable("focus_body"); return false;
    }
    if(control->HasAttribute("disabled")) return false;
    if(event.GetType()!="click" && event.GetType()!="contextmenu") return false;
    if(event.GetType()=="contextmenu") { const auto refund=control->GetAttribute<Rml::String>("data-refund", ""); if(!refund.empty()) Submit(refund,host,use_fixture,seconds); return false; }
    if(host.play_sound) host.play_sound("mouse_click");
    if(action=="primary-picker") {primary_picker=!primary_picker;pistol_picker=false;model.DirtyVariable("primary_picker");model.DirtyVariable("pistol_picker");return false;}
    if(action=="pistol-picker") {pistol_picker=!pistol_picker;primary_picker=false;model.DirtyVariable("primary_picker");model.DirtyVariable("pistol_picker");return false;}
    Rml::String response=action;
    if(action=="class") response="select:"+control->GetAttribute<Rml::String>("data-value", "");
    if(action=="primary") response="pick_primary:"+control->GetAttribute<Rml::String>("data-value", "");
    if(action=="secondary") response="secondary:"+control->GetAttribute<Rml::String>("data-value", "");
    if(action=="give") response="give:"+held_gift;
    if(action=="primary" || action=="secondary") {primary_picker=pistol_picker=false;model.DirtyVariable("primary_picker");model.DirtyVariable("pistol_picker");}
    return Submit(response,host,use_fixture,seconds);
}
bool EconomyView::Shortcut(int key, const rml_host_t& host, bool use_fixture, double seconds) {
    if (key == RML_KEY_ESCAPE && (primary_picker || pistol_picker)) {
        primary_picker = pistol_picker = false;
        model.DirtyVariable("primary_picker"); model.DirtyVariable("pistol_picker"); return true;
    }
    if (confirmation || primary_picker || pistol_picker) return key >= '1' && key <= '5';
    if(key>='1' && key<='4') {Submit(Rml::String("select:")+role_keys[key-'1'],host,use_fixture,seconds);return true;}
    if(key=='5') {Submit("spectate",host,use_fixture,seconds);return true;}
    return false;
}
}
