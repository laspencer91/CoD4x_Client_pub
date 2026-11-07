#include "economy_preview.hpp"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cstdlib>

namespace cod4x::rml {
namespace {
const std::map<Rml::String,Rml::String> defaults = {
    {"economy_repeat_order","attachment,frag,armor"},{"economy_repeat_attachment","none"},{"economy_repeat_frag","0"},{"economy_repeat_armor","0"},
    {"economy_cash","250"},{"economy_buy_open","1"},{"economy_purchase_allowed","1"},{"flat_has_class","1"},
    {"flat_buy_seconds","24"},{"flat_team","ATTACK"},{"mapname","mp_crash"},{"loadout_class","assault"},
    {"loadout_primary","ak47"},{"loadout_secondary","deserteagle"},{"loadout_primary_attachment","none"},{"loadout_secondary_attachment","none"},
    {"flat_utility_name","SMOKE"},{"flat_camo_label","Woodland"},{"flat_camo_material","ui_camoskin_bshdwl"},{"flat_primary_rows","7"},
    {"economy_armor_price","100"},{"economy_armor_charges","0"},{"economy_armor_capacity","2"},{"economy_can_buy_armor","1"},
    {"economy_frag_price","100"},{"economy_can_buy_frag","1"},{"economy_suppressor_price","50"},
    {"economy_red_dot_price","100"},{"economy_red_dot_supported","1"},{"economy_can_buy_red_dot","1"},
    {"economy_demolition_available","1"},{"economy_demolition_price","300"},{"flat_status",""},
    {"flat_assault_capacity","3 / 5"},{"flat_assault_allowed","1"},{"flat_specops_capacity","1 / 2"},{"flat_specops_allowed","1"},
    {"flat_sniper_capacity","0 / 1"},{"flat_sniper_allowed","1"},{"flat_demolitions_capacity","0 / 1"},
    {"flat_primary_0_ref","m16"},{"flat_primary_1_ref","ak47"},{"flat_primary_2_ref","m4"},{"flat_primary_3_ref","g3"},
    {"flat_primary_4_ref","g36c"},{"flat_primary_5_ref","m14"},{"flat_primary_6_ref","mp44"},
    {"flat_mate_0_exists","1"},{"flat_mate_0_name","YOU"},{"flat_mate_0_class","Assault"},{"flat_mate_0_cash","$250"},{"flat_mate_0_primary","ak47"},
    {"flat_mate_1_exists","1"},{"flat_mate_1_name","MAVERICK"},{"flat_mate_1_class","Spec Ops"},{"flat_mate_1_cash","$50"},{"flat_mate_1_can_give","1"},
    {"flat_mate_1_give","openscriptmenu flat_loadout give:player_1_2"},
    {"flat_mate_1_primary","mp5"},{"flat_mate_1_utility","weapon_flashbang"},
    {"flat_mate_2_exists","1"},{"flat_mate_2_name","GHOST"},{"flat_mate_2_class","Scope"},{"flat_mate_2_cash","$100"},{"flat_mate_2_can_give","1"},
    {"flat_mate_2_give","openscriptmenu flat_loadout give:player_1_3"},{"flat_mate_2_primary","m40a3"}
};

const std::map<Rml::String, Rml::StringList> pools = {
    {"assault", {"m16","ak47","m4","g3","g36c","m14","mp44"}},
    {"specops", {"mp5","uzi","ak74u"}}, {"sniper", {"m40a3","remington700"}},
    {"demolitions", {"winchester1200","m1014"}}
};
}
void EconomyPreview::Reset() { fields = defaults; WeaponRows(); Sync(); }
Rml::String EconomyPreview::Value(const Rml::String& name) {
    if (fields.empty()) Reset();
    const auto found = fields.find(name);
    if (found != fields.end()) return found->second;
    if (name.compare(0,11,"weap_allow_")==0 || name.compare(0,13,"attach_allow_")==0) return "1";
    return {};
}
int EconomyPreview::Number(const Rml::String& name) { return std::atoi(fields[name].c_str()); }
bool EconomyPreview::Spend(int amount) {
    const int cash = Number("economy_cash");
    if (cash < amount) { fields["flat_status"]="NEED $"+std::to_string(amount-cash)+" MORE"; return false; }
    fields["economy_cash"] = std::to_string(cash-amount); return true;
}
void EconomyPreview::WeaponRows() {
    const auto found = pools.find(fields["loadout_class"]); if (found == pools.end()) return;
    fields["flat_primary_rows"] = std::to_string(found->second.size());
    for (int i=0;i<7;++i) fields["flat_primary_"+std::to_string(i)+"_ref"] = size_t(i)<found->second.size() ? found->second[i] : "";
    if (std::find(found->second.begin(), found->second.end(), fields["loadout_primary"]) == found->second.end())
        fields["loadout_primary"] = found->second.front();
}
void EconomyPreview::Sync() {
    const bool eligible = Number("economy_buy_open") && Number("economy_purchase_allowed");
    const int cash = Number("economy_cash"), armor = Number("economy_armor_charges");
    fields["economy_armor_price"] = armor==1 ? "50" : "100";
    fields["economy_can_buy_armor"] = eligible && armor<2 && cash>=Number("economy_armor_price") ? "1" : "0";
    fields["economy_can_buy_frag"] = eligible && !Number("economy_frag_owned") && cash>=Number("economy_frag_price") ? "1" : "0";
    const auto& gun = fields["loadout_primary"];
    const bool optic = fields["loadout_class"]!="sniper" && gun!="uzi" && gun!="mp44";
    fields["economy_red_dot_supported"] = optic ? "1" : "0";
    fields["economy_can_buy_red_dot"] = eligible && optic && cash>=Number("economy_red_dot_price") ? "1" : "0";
    // Mirrors economy_items.gsc::suppressorEligible for the offline fixture.
    const auto& role = fields["loadout_class"]; const auto& pistol = fields["loadout_secondary"];
    fields["economy_suppressor_primary_supported"] = (role=="assault" || role=="specops") && gun!="mp44" ? "1" : "0";
    fields["economy_suppressor_secondary_supported"] = pistol!="deserteagle" && pistol!="deserteaglegold" ? "1" : "0";
    fields["flat_mate_0_cash"] = "$"+fields["economy_cash"];
    fields["flat_mate_0_primary"] = gun; fields["flat_mate_0_class"] = fields["loadout_class"];
    fields["flat_mate_0_frag"] = fields["economy_frag_owned"];
    fields["flat_mate_0_utility"] = fields["flat_utility_name"]=="SMOKE" ? "weapon_smokegrenade" : "weapon_flashbang";
    for (int i=1;i<3;++i) {
        const auto prefix="flat_mate_"+std::to_string(i)+"_";
        const int recipient = std::atoi(fields[prefix+"cash"].c_str()+1);
        fields[prefix+"can_give"] = eligible && cash>=50 && recipient<=450 ? "1" : "0";
    }
}
void EconomyPreview::Apply(const Rml::String& action) {
    if (fields.empty()) Reset();
    fields["flat_status"].clear();
    if (action=="preview-credit") fields["economy_cash"]=std::to_string(std::min(500,Number("economy_cash")+250));
    else if (action=="spectate") { fields["flat_has_class"]="0"; fields["flat_team"]="SPECTATE"; }
    else if (action.compare(0,7,"select:")==0) {
        const auto role=action.substr(7);
        if (!pools.count(role)) return;
        if (role=="demolitions" && !Number("economy_demolition_owned")) {
            if (!Spend(Number("economy_demolition_price"))) { Sync(); return; }
            fields["economy_demolition_owned"]="1";
        }
        fields["flat_has_class"]="1"; fields["flat_team"]="ATTACK"; fields["loadout_class"]=role; WeaponRows();
    } else if (action.compare(0,13,"pick_primary:")==0) {
        const int row=std::atoi(action.c_str()+13);
        if (row>=0 && row<Number("flat_primary_rows")) fields["loadout_primary"]=fields["flat_primary_"+std::to_string(row)+"_ref"];
    } else if (action.compare(0,10,"secondary:")==0) {
        fields["loadout_secondary"]=action.substr(10);
        if ((fields["loadout_secondary"]=="deserteagle" || fields["loadout_secondary"]=="deserteaglegold") && Number("paid_pistol")) {
            fields["economy_cash"]=std::to_string(Number("economy_cash")+Number("paid_pistol")); fields["paid_pistol"]="0";
            fields["loadout_secondary_attachment"]="none"; fields["economy_suppressor_secondary_owned"]="0";
            fields["flat_status"]="SUPPRESSOR REFUNDED";
        }
    } else if (action=="buy_armor") {
        if (Number("economy_can_buy_armor") && Spend(Number("economy_armor_price"))) {
            fields["paid_armor"]=std::to_string(Number("paid_armor")+Number("economy_armor_price"));
            fields["economy_armor_charges"]="2"; fields["economy_armor_refundable"]="1"; fields["economy_repeat_armor"]="2";
        }
    } else if (action=="preview_frag") {
        if (Number("economy_can_buy_frag") && Spend(Number("economy_frag_price"))) {
            fields["paid_frag"]=fields["economy_frag_price"]; fields["economy_frag_owned"]="1";
            fields["economy_frag_refundable"]="1"; fields["economy_repeat_frag"]="1";
        }
    } else if (action=="refund_frag" || action=="refund_armor") {
        const auto item=action=="refund_frag" ? "frag" : "armor";
        fields["economy_cash"]=std::to_string(Number("economy_cash")+Number(Rml::String("paid_")+item));
        fields[Rml::String("paid_")+item]="0"; fields[Rml::String("economy_repeat_")+item]="0";
        if (action=="refund_frag") { fields["economy_frag_owned"]="0"; fields["economy_frag_refundable"]="0"; }
        else { fields["economy_armor_charges"]="0"; fields["economy_armor_refundable"]="0"; }
    } else if (action.compare(0,11,"attachment:")==0 || action.compare(0,18,"pistol_attachment:")==0 ||
               action=="refund_silencer" || action=="refund_reflex" || action=="refund_pistol_silencer") {
        const bool pistol=action.compare(0,18,"pistol_attachment:")==0 || action=="refund_pistol_silencer";
        const auto attachment=action.compare(0,7,"refund_")==0 ? "none" : action.substr(pistol ? 18 : 11);
        const auto paid=pistol ? "paid_pistol" : "paid_primary";
        const int price=attachment=="none" ? 0 : Number(attachment=="reflex" ? "economy_red_dot_price" : "economy_suppressor_price");
        if (Number("economy_cash")+Number(paid)<price) { fields["flat_status"]="INSUFFICIENT CASH"; Sync(); return; }
        fields["economy_cash"]=std::to_string(Number("economy_cash")+Number(paid)-price); fields[paid]=std::to_string(price);
        fields[pistol ? "loadout_secondary_attachment" : "loadout_primary_attachment"]=attachment;
        fields[pistol ? "economy_suppressor_secondary_owned" : "economy_suppressor_primary_owned"]=attachment=="silencer" ? "1" : "0";
        if (!pistol) { fields["economy_red_dot_owned"]=attachment=="reflex" ? "1" : "0"; fields["economy_repeat_attachment"]=attachment; }
    } else if (action=="repeat_buy") fields["economy_repeat_enabled"]=Number("economy_repeat_enabled") ? "0" : "1";
    else if (action=="toggle_tactical") fields["flat_utility_name"]=fields["flat_utility_name"]=="SMOKE" ? "FLASH" : "SMOKE";
    else if (action=="camo:next") {
        const Rml::StringList labels={"None","Desert","Woodland","Digital","Red Tiger","Blue Tiger","Gold"};
        const Rml::StringList materials={"white","ui_camoskin_brock","ui_camoskin_bshdwl","ui_camoskin_bwmrpt","ui_camoskin_cmdtgr","ui_camoskin_stagger","ui_camoskin_gold"};
        auto found=std::find(labels.begin(),labels.end(),fields["flat_camo_label"]);
        size_t index=found==labels.end() ? 0 : (size_t(found-labels.begin())+1)%labels.size();
        if (index==6 && fields["loadout_primary"]!="ak47" && fields["loadout_primary"]!="uzi" && fields["loadout_primary"]!="m1014") index=0;
        fields["flat_camo_label"]=labels[index]; fields["flat_camo_material"]=materials[index];
    } else if (action.compare(0,9,"rebuy_up:")==0 || action.compare(0,11,"rebuy_down:")==0) {
        const bool up=action.compare(0,9,"rebuy_up:")==0; const auto item=action.substr(up ? 9 : 11);
        Rml::StringList order; Rml::StringUtilities::ExpandString(order, fields["economy_repeat_order"], ',');
        auto found=std::find(order.begin(),order.end(),item);
        if (found!=order.end()) {
            const int index=int(found-order.begin()); int next=index+(up ? -1 : 1);
            while (next>=0 && next<int(order.size())) {
                const auto& candidate=order[next];
                if (candidate=="attachment" ? fields["economy_repeat_attachment"]!="none" : Number("economy_repeat_"+candidate)>0) break;
                next+=up ? -1 : 1;
            }
            if (next>=0 && next<int(order.size())) std::swap(order[index],order[next]);
            fields["economy_repeat_order"]=order[0]+","+order[1]+","+order[2];
        }
    } else if (action.compare(0,5,"give:")==0) {
        for (int i=1;i<3;++i) {
            const auto prefix="flat_mate_"+std::to_string(i)+"_";
            if (fields[prefix+"give"]=="openscriptmenu flat_loadout "+action && Number(prefix+"can_give") && Spend(50))
                fields[prefix+"cash"]="$"+std::to_string(std::atoi(fields[prefix+"cash"].c_str()+1)+50);
        }
    } else if (action=="deploy") fields["flat_status"]="PREVIEW LOADOUT READY";
    Sync();
}
}
