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

}

bool EconomyActionAllowed(const char* input) {
    if (!input) return false;
    const Rml::String action(input);
    static const char* exact[] = {"close","deploy","spectate","repeat_buy","toggle_tactical","camo:next","preview_frag","buy_armor",
        "refund_frag","refund_armor","refund_silencer","refund_reflex","refund_pistol_silencer","confirm_demolition","cancel_demolition",
        "attachment:none","attachment:silencer","attachment:reflex","pistol_attachment:none","pistol_attachment:silencer"};
    for (const char* value : exact) if (action == value) return true;
    for (const char* item : {"attachment", "frag", "armor"})
        if (action == Rml::String("rebuy_up:") + item || action == Rml::String("rebuy_down:") + item) return true;
    for (const char* value : role_keys) if (action == Rml::String("select:") + value) return true;
    for (const char* value : pistol_keys) if (action == Rml::String("secondary:") + value) return true;
    if (action.size() == 14 && action.compare(0, 13, "pick_primary:") == 0) return action[13] >= '0' && action[13] <= '6';
    if (action.compare(0, 12, "give:player_") == 0 && action.size() > 12 && action.size() < 80)
        return std::all_of(action.begin() + 12, action.end(), [](unsigned char c) { return std::isdigit(c) || c == '_'; });
    return false;
}

void EconomyView::Init(Rml::Context* context) {
    ui_context = context;
    auto constructor = context->CreateDataModel("economy");
    auto row = constructor.RegisterStruct<EconomyOption>();
#define ROW(field) row.RegisterMember(#field, &EconomyOption::field)
    ROW(key); ROW(name); ROW(detail); ROW(state); ROW(price); ROW(token); ROW(icon); ROW(utility); ROW(enabled); ROW(selected); ROW(owned);
#undef ROW
    constructor.RegisterArray<std::vector<EconomyOption>>();
    auto rebuy = constructor.RegisterStruct<RebuyOption>();
    rebuy.RegisterMember("key", &RebuyOption::key); rebuy.RegisterMember("name", &RebuyOption::name);
    rebuy.RegisterMember("priority", &RebuyOption::priority); rebuy.RegisterMember("price", &RebuyOption::price);
    rebuy.RegisterMember("can_move_up", &RebuyOption::can_move_up); rebuy.RegisterMember("can_move_down", &RebuyOption::can_move_down);
    constructor.RegisterArray<std::vector<RebuyOption>>();
#define BIND(field) constructor.Bind(#field, &field)
    BIND(roles); BIND(weapons); BIND(pistols); BIND(mates); BIND(wallet); BIND(phase); BIND(countdown); BIND(map); BIND(team);
    BIND(primary); BIND(secondary);    BIND(armor_price);  BIND(frag_price);
    BIND(tactical); BIND(camo); BIND(feedback); BIND(deploy_label);  BIND(demo_price);  BIND(focus_body);

    BIND(primary_icon); BIND(secondary_icon); BIND(tactical_icon); BIND(camo_icon); BIND(name_color8); BIND(name_color9);
    BIND(suppressor_price); BIND(reflex_price); BIND(pistol_price); BIND(class_family);
    BIND(economy_mode); BIND(teammate_count); BIND(scope); BIND(buy_warning); BIND(buy_urgent);
    BIND(rebuy_rows);
    BIND(rebuy_empty);
    BIND(buy_open); BIND(purchase_allowed); BIND(has_class); BIND(armor_buy); BIND(frag_buy); BIND(armor_one); BIND(armor_two);
    BIND(armor_refund); BIND(frag_owned); BIND(frag_refund); BIND(suppressor_buy); BIND(reflex_buy);  BIND(pistol_buy); BIND(repeat);
    BIND(primary_silencer); BIND(primary_reflex); BIND(secondary_silencer); BIND(primary_none);
    BIND(preview); BIND(debit);  BIND(balance_change);
#undef BIND
    constructor.BindFunc("primary_picker", [this](Rml::Variant& v) { v = overlay == Overlay::PrimaryPicker; });
    constructor.BindFunc("pistol_picker", [this](Rml::Variant& v) { v = overlay == Overlay::PistolPicker; });
    constructor.BindFunc("confirmation", [this](Rml::Variant& v) { v = overlay == Overlay::Confirmation; });
    // Background controls disable themselves with data-attrif-disabled="modal_open".
    constructor.BindFunc("modal_open", [this](Rml::Variant& v) { v = overlay != Overlay::None; });
    model = constructor.GetModelHandle();
    focus_body = "";
}
void EconomyView::Reset() {
    model = {}; ui_context = nullptr; pending_focus = Focus::None;
    values.Clear(); roles.clear(); weapons.clear(); pistols.clear(); mates.clear(); rebuy_rows.clear();
    preview_state.Reset();
    held_action.clear(); audio.Reset();
    previous_cash = -1; overlay = Overlay::None;
}
void EconomyView::Dialog(bool value) { SetOverlay(value ? Overlay::Confirmation : Overlay::None, Focus::None); }
void EconomyView::SetOverlay(Overlay next, Focus focus) {
    pending_focus = focus;
    if (overlay == next) return;
    overlay = next;
    if (model) for (const char* name : {"primary_picker", "pistol_picker", "confirmation", "modal_open"}) model.DirtyVariable(name);
}
Rml::String EconomyView::Value(const rml_host_t& host, const Rml::String& name, bool use_fixture) {
    return use_fixture ? values.Track(name, preview_state.Value(name), model) : values.Read(host, name, model);
}
void EconomyView::Update(const rml_host_t& host, bool use_fixture, double /*seconds*/) {
    if (!model) return;
    auto value = [&](const Rml::String& key) { return Value(host, key, use_fixture); };
    auto number = [&](const Rml::String& key) { return std::atoi(value(key).c_str()); };
    auto money = [&](const Rml::String& key) { return "$" + value(key); };
    name_color8=value("rml_name_color_8"); name_color9=value("rml_name_color_9");
    preview = use_fixture;
    const bool next_economy_mode = use_fixture || value("flat_loadout_mode") != "standard";
    if (economy_mode != next_economy_mode) { focus_body.clear(); model.DirtyAllVariables(); }
    economy_mode = next_economy_mode;
    const int cash = number("economy_cash"); wallet = "$" + std::to_string(cash);
    buy_open = number("economy_buy_open") != 0; purchase_allowed = number("economy_purchase_allowed") != 0;
    has_class = number("flat_has_class") != 0;
    phase = !buy_open ? "BUY CLOSED" : !purchase_allowed ? "WAIT FOR SPAWN" : "BUY PHASE";
    const int seconds_left = std::max(0, std::min(number("flat_buy_seconds"), 999));
    buy_warning = buy_open && seconds_left <= 10; buy_urgent = buy_open && seconds_left <= 5;
    countdown = buy_open ? FormatClock(seconds_left, 2) : "";
    map = value("mapname"); if (map.compare(0,3,"mp_") == 0) map.erase(0,3);
    std::transform(map.begin(), map.end(), map.begin(), [](unsigned char c){return char(std::toupper(c));});
    team = value("flat_team"); demo_price = money("economy_demolition_price");
    const auto selected = value("loadout_class");
    scope = selected == "sniper";
    class_family = scope ? "SNIPER RIFLE" : selected == "specops" ? "SUBMACHINE GUN" : selected == "demolitions" ? "SHOTGUN" : "ASSAULT RIFLE";
    roles.clear();
    for (int i = 0; i < 4; ++i) {
        EconomyOption row; row.key = role_keys[i]; row.name = role_names[i]; row.selected = selected == row.key;
        row.token = "0" + std::to_string(i + 1);
        row.detail = value("flat_" + row.key + "_capacity"); row.enabled = number("flat_" + row.key + "_allowed") || row.selected;
        const auto divider = row.detail.find('/');
        if (!row.enabled && divider != Rml::String::npos && std::atoi(row.detail.c_str() + divider + 1) > 0 &&
            std::atoi(row.detail.c_str()) >= std::atoi(row.detail.c_str() + divider + 1)) row.detail = "FULL";
        if (i == 3 && economy_mode) { row.owned = number("economy_demolition_owned"); row.price = row.owned ? "RESERVED" : demo_price;
            row.enabled = row.selected || row.owned || (buy_open && purchase_allowed && number("economy_demolition_available") && cash >= number("economy_demolition_price")); }
        roles.push_back(std::move(row));
    }
    primary = WeaponName(value("loadout_primary")); secondary = WeaponName(value("loadout_secondary"));
    primary_icon = ImagePath(value("loadout_primary")); secondary_icon = ImagePath(value("loadout_secondary"));
    const auto attachment = value("loadout_primary_attachment"), pistol_attachment = value("loadout_secondary_attachment");
    primary_none = attachment == "none"; primary_silencer = attachment == "silencer"; primary_reflex = attachment == "reflex";
    secondary_silencer = pistol_attachment == "silencer";
    // Eligibility comes from the server's own rules (economy_items.gsc); the client adds only cash and phase.
    const bool primary_suppressor_supported = number("economy_suppressor_primary_supported");
    const bool secondary_suppressor_supported = number("economy_suppressor_secondary_supported");
    auto suppressor = [&](bool supported, const char* owned) {
        return has_class && supported && (!economy_mode || number(owned) || (buy_open && purchase_allowed && cash >= number("economy_suppressor_price")));
    };
    suppressor_buy = suppressor(primary_suppressor_supported, "economy_suppressor_primary_owned");
    const bool reflex_free = number("economy_red_dot_free");
    reflex_buy = has_class && number("economy_red_dot_supported") && (number("economy_can_buy_red_dot") || number("economy_red_dot_owned") || reflex_free);
    if (!economy_mode) reflex_buy = has_class && number("attach_allow_" + selected + "_reflex");
    pistol_buy = suppressor(secondary_suppressor_supported, "economy_suppressor_secondary_owned");
    suppressor_price = number("economy_suppressor_primary_owned") ? "OWNED" : money("economy_suppressor_price");
    reflex_price = reflex_free ? "FREE" : number("economy_red_dot_owned") ? "OWNED" : money("economy_red_dot_price");
    pistol_price = !secondary_suppressor_supported ? "N/A" :
        number("economy_suppressor_secondary_owned") ? "OWNED" : money("economy_suppressor_price");
    weapons.clear(); const int count = std::max(0, std::min(number("flat_primary_rows"), 7));
    for (int i=0; i<count; ++i) { EconomyOption row; const auto ref = value("flat_primary_" + std::to_string(i) + "_ref");
        row.key = std::to_string(i); row.detail = std::to_string(i + 1); row.name = WeaponName(ref); row.icon = ImagePath(ref); row.enabled = has_class && !ref.empty(); row.selected = ref == value("loadout_primary");
        row.state = row.selected ? "EQUIPPED" : economy_mode ? "FREE" : "";
        weapons.push_back(row); }
    pistols.clear();
    for (const char* ref : pistol_keys) { EconomyOption row; row.key=ref; row.detail=std::to_string(pistols.size()+1); row.name=WeaponName(ref); row.icon=ImagePath(ref); row.enabled=has_class && number("weap_allow_"+row.key); row.selected=row.key==value("loadout_secondary"); row.state=row.selected ? "EQUIPPED" : economy_mode ? "FREE" : ""; pistols.push_back(row); }
    const int charges = number("economy_armor_charges"); armor_one = charges >= 1; armor_two = charges >= 2;
    armor_price = money("economy_armor_price"); armor_buy = has_class && number("economy_can_buy_armor");
    armor_refund = buy_open && purchase_allowed && number("economy_armor_refundable");
    frag_owned = number(economy_mode ? "economy_frag_owned" : "weap_allow_frag_grenade");
    frag_price = money("economy_frag_price"); frag_buy = has_class && number("economy_can_buy_frag"); frag_refund = number("economy_frag_refundable");
    tactical = value("flat_utility_name"); camo = value("flat_camo_label"); repeat = number("economy_repeat_enabled");
    tactical_icon = ImagePath(tactical == "SMOKE" ? "eq-smoke" : "eq-flash");
    const auto material = value("flat_camo_material");
    static const char* camo_materials[] = {"ui_camoskin_brock", "ui_camoskin_bshdwl", "ui_camoskin_bwmrpt", "ui_camoskin_cmdtgr", "ui_camoskin_stagger", "ui_camoskin_gold"};
    camo_icon.clear();
    for (const char* candidate : camo_materials) if (material == candidate) { camo_icon = ImagePath(material); break; }
    Rml::StringList order;
    Rml::StringUtilities::ExpandString(order, value("economy_repeat_order"), ',');
    const bool rebuy_supported = order.size() == 3 && std::count(order.begin(), order.end(), "attachment") == 1 &&
        std::count(order.begin(), order.end(), "frag") == 1 && std::count(order.begin(), order.end(), "armor") == 1;
    if (!rebuy_supported) order = {"attachment", "frag", "armor"};
    rebuy_rows.clear();
    for (size_t i = 0; i < order.size(); ++i) {
        RebuyOption row; row.key = order[i];
        const auto saved = value("economy_repeat_" + row.key);
        if ((row.key == "attachment" && (saved.empty() || saved == "none")) || (row.key != "attachment" && std::atoi(saved.c_str()) <= 0)) continue;
        row.name = row.key == "attachment" ? (saved == "silencer" ? "SUPPRESSOR" : "RED DOT") : row.key == "frag" ? "FRAG" : "ARMOR";
        row.price = money(row.key == "attachment" ? (saved == "silencer" ? "economy_suppressor_price" : "economy_red_dot_price") : row.key == "frag" ? "economy_frag_price" : "economy_armor_price");
        rebuy_rows.push_back(row);
    }
    for (size_t i = 0; i < rebuy_rows.size(); ++i) {
        rebuy_rows[i].priority = "0" + std::to_string(i + 1);
        rebuy_rows[i].can_move_up = rebuy_supported && has_class && i > 0;
        rebuy_rows[i].can_move_down = rebuy_supported && has_class && i + 1 < rebuy_rows.size();
    }
    rebuy_empty = rebuy_rows.empty();
    feedback = value("flat_status"); deploy_label = !economy_mode ? "CONFIRM" : buy_open ? "DEPLOY" : "SAVE & CLOSE";
    mates.clear(); teammate_count = 0;
    for (int i=0; i<5; ++i) { const Rml::String prefix = "flat_mate_"+std::to_string(i)+"_"; EconomyOption row;
        row.key=std::to_string(i); row.name=value(prefix+"name"); row.detail=value(prefix+"class"); row.price=value(prefix+"cash");
        row.owned=number(prefix+"frag"); row.enabled=buy_open && number(prefix+"can_give"); row.selected=number(prefix+"exists");
        if (row.selected) ++teammate_count;
        row.icon = ImagePath(value(prefix+"primary"));
        row.utility = UtilityImage(value(prefix+"utility"));
        const auto command=value(prefix+"give"); const Rml::String expected="openscriptmenu flat_loadout give:";
        if(command.compare(0,expected.size(),expected)==0) row.token=command.substr(expected.size());
        row.enabled = row.enabled && EconomyActionAllowed(("give:"+row.token).c_str());
        if(!row.selected) {row.name="";row.detail="";row.price="";} mates.push_back(row); }
    if (previous_cash >= 0 && previous_cash != cash) { int delta=cash-previous_cash; debit=delta<0;
        balance_change=(delta>0?"+ $":"- $")+std::to_string(std::abs(delta)); model.DirtyVariable("balance_change"); }
    previous_cash=cash;
}
bool EconomyView::Submit(const Rml::String& action, const rml_host_t& host, bool use_fixture, double /*seconds*/) {
    if (!EconomyActionAllowed(action.c_str())) return false;
    if (use_fixture) {
        if (action == "close") return true;
        if (action == "select:demolitions" && preview_state.Value("economy_demolition_owned") != "1" &&
            std::atoi(preview_state.Value("economy_cash").c_str()) >= std::atoi(preview_state.Value("economy_demolition_price").c_str())) {
            SetOverlay(Overlay::Confirmation, Focus::ConfirmButton); return false;
        }
        if (action == "confirm_demolition") { preview_state.Apply("select:demolitions"); SetOverlay(Overlay::None, Focus::None); }
        else if (action == "cancel_demolition") SetOverlay(Overlay::None, Focus::None);
        else preview_state.Apply(action);
        model.DirtyAllVariables();
        return false;
    }
    if (host.menu_response) host.menu_response(action.c_str());
    return false;
}
bool EconomyView::Event(Rml::Event& event, const rml_host_t& host, bool use_fixture, double seconds) {
    auto* control=Control(event.GetTargetElement()); if(!control) return false;
    const auto action=control->GetAttribute<Rml::String>("data-econ", "");
    if (use_fixture && (action == "preview-reset" || action == "preview-credit") && event.GetType() == "click" && !control->HasAttribute("disabled")) {
        if (action == "preview-reset") preview_state.Reset(); else preview_state.Apply(action);
        values.Clear(); model.DirtyAllVariables(); return false;
    }
    if(event.GetType()=="mousedown" && event.GetParameter<int>("button",0)==1) {
        if (!economy_mode) return false;
        const auto refund=control->GetAttribute<Rml::String>("data-refund", "");
        if((refund=="refund_armor" && armor_refund) || (refund=="refund_frag" && frag_refund) ||
            (buy_open && purchase_allowed && (refund=="refund_silencer" || refund=="refund_reflex" || refund=="refund_pistol_silencer"))) Submit(refund,host,use_fixture,seconds);
        return false;
    }
    // Capture the markup-built response on press: a model refresh can reorder rows before the click arrives.
    if(event.GetType()=="mousedown" || event.GetType()=="keydown") held_action=action;
    if(event.GetType()=="mouseover" || event.GetType()=="focus") {
        if (event.GetType()=="mouseover" && (action=="primary" || action=="secondary") && !control->HasAttribute("disabled")) control->Focus();
        const auto identity=action+control->GetAttribute<Rml::String>("data-value", "");
        // Programmatic focus (picker open/close) updates identity without a cue.
        audio.Hover(identity, event.GetType()=="mouseover", host, seconds);
        focus_body=control->GetAttribute<Rml::String>("data-help", "");
        model.DirtyVariable("focus_body"); return false;
    }
    if(control->HasAttribute("disabled")) return false;
    if(event.GetType()!="click") return false;
    audio.Click(host, seconds);
    const Rml::String pressed=std::move(held_action); held_action.clear();
    if(action=="primary-picker") {TogglePicker(true);return false;}
    if(action=="pistol-picker") {TogglePicker(false);return false;}
    if (action=="primary" || action=="secondary") {
        ChooseWeapon(action=="primary",control->GetAttribute<Rml::String>("data-value", ""),host,use_fixture,seconds); return false;
    }
    return Submit(pressed.empty() ? action : pressed,host,use_fixture,seconds);
}
void EconomyView::ChooseWeapon(bool primary, const Rml::String& key, const rml_host_t& host, bool fixture, double seconds) {
    Submit((primary ? "pick_primary:" : "secondary:")+key,host,fixture,seconds);
    SetOverlay(Overlay::None, primary ? Focus::PrimaryCard : Focus::PistolCard);
}
void EconomyView::TogglePicker(bool primary) {
    const Overlay picker = primary ? Overlay::PrimaryPicker : Overlay::PistolPicker;
    if (overlay == picker) SetOverlay(Overlay::None, primary ? Focus::PrimaryCard : Focus::PistolCard);
    else SetOverlay(picker, primary ? Focus::PrimaryList : Focus::PistolList);
}
void EconomyView::Dismiss(const Rml::String& action, const rml_host_t& host, bool fixture, double seconds) {
    if (action == "primary-picker" && overlay == Overlay::PrimaryPicker) TogglePicker(true);
    else if (action == "pistol-picker" && overlay == Overlay::PistolPicker) TogglePicker(false);
    else if (action == "cancel_demolition" && overlay == Overlay::Confirmation) Submit(action, host, fixture, seconds);
}
bool EconomyView::ResolveFocus() {
    if (pending_focus == Focus::None || !ui_context) return false;
    const Focus target = pending_focus; pending_focus = Focus::None;
    auto* doc = ui_context->GetDocument(0); if (!doc) return false;
    if (target == Focus::ConfirmButton) { auto* button = doc->GetElementById("confirm-demolition"); return button && button->Focus(); }
    if (target == Focus::PrimaryCard || target == Focus::PistolCard) {
        auto* button = doc->GetElementById(target == Focus::PrimaryCard ? "primary-picker" : "secondary-picker");
        return button && !button->HasAttribute("disabled") && button->Focus();
    }
    auto* panel = doc->GetElementById(target == Focus::PrimaryList ? "primary-drawer" : "secondary-drawer");
    if (!panel) return false;
    Rml::ElementList buttons; panel->QuerySelectorAll(buttons, ".picker-row input");
    Rml::Element* first = nullptr;
    for (auto* button : buttons) if (!button->HasAttribute("disabled")) {
        if (!first) first = button;
        if (button->IsClassSet("selected")) return button->Focus();
    }
    return first && first->Focus();
}
bool EconomyView::Shortcut(int key, const rml_host_t& host, bool use_fixture, double seconds) {
    const bool primary_picker = overlay == Overlay::PrimaryPicker, pistol_picker = overlay == Overlay::PistolPicker;
    if (key == RML_KEY_ESCAPE && (primary_picker || pistol_picker)) {
        SetOverlay(Overlay::None, primary_picker ? Focus::PrimaryCard : Focus::PistolCard); return true;
    }
    if (overlay == Overlay::Confirmation) return key >= '1' && key <= '5';
    if ((key == 'q' || key == 'Q') && has_class && !pistol_picker) {
        TogglePicker(true); return true;
    }
    if ((key == 'e' || key == 'E') && has_class && !primary_picker) {
        TogglePicker(false); return true;
    }
    if (primary_picker || pistol_picker) {
        if ((key == RML_KEY_UPARROW || key == RML_KEY_DOWNARROW) && ui_context) {
            auto* doc = ui_context->GetDocument(0);
            auto* panel = doc ? doc->GetElementById(primary_picker ? "primary-drawer" : "secondary-drawer") : nullptr;
            Rml::ElementList buttons; if (panel) panel->QuerySelectorAll(buttons, ".picker-row input");
            int current = -1;
            for (size_t i=0; i<buttons.size(); ++i) if (buttons[i] == ui_context->GetFocusElement()) current=int(i);
            if (current < 0 && key == RML_KEY_UPARROW) current = int(buttons.size()); // nothing focused: Up starts at the last row
            for (size_t i=0; i<buttons.size(); ++i) {
                current = (current + (key == RML_KEY_DOWNARROW ? 1 : int(buttons.size())-1) + int(buttons.size())) % int(buttons.size());
                if (!buttons[current]->HasAttribute("disabled")) { buttons[current]->Focus(); break; }
            }
            return true;
        }
        if (key >= '1' && key <= '9') {
            auto& rows = primary_picker ? weapons : pistols;
            const size_t index = size_t(key - '1');
            if (index < rows.size() && rows[index].enabled) {
                ChooseWeapon(primary_picker,rows[index].key,host,use_fixture,seconds);
            }
            return true;
        }
        return false;
    }
    if (has_class && (key == 'c' || key == 'C')) { Submit("toggle_tactical",host,use_fixture,seconds); return true; }
    if (economy_mode && has_class && (key == 'x' || key == 'X')) {
        if (frag_buy) Submit("preview_frag",host,use_fixture,seconds);
        else if (frag_refund) Submit("refund_frag",host,use_fixture,seconds);
        return true;
    }
    if (economy_mode && has_class && (key == 'z' || key == 'Z')) {
        if (armor_buy) Submit("buy_armor",host,use_fixture,seconds);
        else if (armor_refund) Submit("refund_armor",host,use_fixture,seconds);
        return true;
    }
    if (has_class && key == RML_KEY_SPACE) { Submit("deploy",host,use_fixture,seconds); return true; }
    if(key>='1' && key<='4') {Submit(Rml::String("select:")+role_keys[key-'1'],host,use_fixture,seconds);return true;}
    if(key=='5') {Submit("spectate",host,use_fixture,seconds);return true;}
    return false;
}
}
