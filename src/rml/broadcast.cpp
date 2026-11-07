#include "broadcast.hpp"
#include <algorithm>
#include <cstdlib>

namespace cod4x::rml {
void BroadcastView::Reset() {
    context=nullptr; document=nullptr; model={}; players.clear(); values.clear(); retry_at=0;
    observer=compact=hidden=planted=false; last_visible_mode=CardMode::Expanded;
}
bool BroadcastView::Reload() {
    if (!context) return true;
    auto* next=context->LoadDocument("ui/rml/broadcast.rml");
    if (!next) return false;
    if (document) document->Close();
    document=next; document->Show(); return true;
}
bool BroadcastView::Prepare(const rml_host_t& host, int width, int height, double seconds) {
    auto value=[&](const Rml::String& name) {
        const char* raw=host.get_value ? host.get_value(name.c_str()) : nullptr;
        Rml::String result=raw ? raw : "";
        if (values[name]!=result) { values[name]=result; if (model) model.DirtyAllVariables(); }
        return result;
    };
    auto number=[&](const Rml::String& name) { return std::atoi(value(name).c_str()); };
    if (!context) {
        context=Rml::CreateContext("cod4x-broadcast",{width,height}); if (!context) return false;
        auto binding=context->CreateDataModel("broadcast");
        auto row=binding.RegisterStruct<BroadcastPlayer>();
#define ROW(field) row.RegisterMember(#field,&BroadcastPlayer::field)
        ROW(key); ROW(name); ROW(health); ROW(slot); ROW(weapon); ROW(utility); ROW(fill); ROW(bottom);
        ROW(present); ROW(condensed); ROW(fresh); ROW(axis); ROW(alive); ROW(observed); ROW(frag);
#undef ROW
        binding.RegisterArray<std::vector<BroadcastPlayer>>();
#define BIND(field) binding.Bind(#field,&field)
        BIND(players); BIND(left_score); BIND(right_score); BIND(clock); BIND(round); BIND(target);
        BIND(left_alive); BIND(right_alive); BIND(phase); BIND(observer); BIND(compact); BIND(hidden); BIND(planted);
        BIND(name_color8); BIND(name_color9); BIND(left_header_bottom); BIND(right_header_bottom);
#undef BIND
        players.resize(10);
        model=binding.GetModelHandle();
    }
    if (!document && seconds<retry_at) return false;
    const bool before_compact=compact, before_hidden=hidden;
    const auto card_mode=static_cast<CardMode>(std::max(0,std::min(number("cg_rmlCards"),2)));
    if (card_mode!=CardMode::Hidden) last_visible_mode=card_mode;
    compact=last_visible_mode==CardMode::Compact; hidden=card_mode==CardMode::Hidden;
    observer=value("broadcast_mode")=="observer";
    left_score=value("broadcast_score_allies"); right_score=value("broadcast_score_axis");
    left_alive=value("broadcast_alive_allies"); right_alive=value("broadcast_alive_axis");
    round=value("broadcast_round"); target=value("broadcast_target"); phase=value("broadcast_phase");
    planted=phase=="BOMB PLANTED"; name_color8=value("rml_name_color_8"); name_color9=value("rml_name_color_9");
    const int remaining=std::max(0,std::min(number("broadcast_seconds"),5999));
    clock=std::to_string(remaining/60)+":"+(remaining%60<10 ? "0" : "")+std::to_string(remaining%60);
    const auto follow=value("rml_follow_client");
    const int followed=follow.empty() ? -1 : std::atoi(follow.c_str());
    for (int i=0;i<10;++i) {
        auto& row=players[i]; const auto old_key=row.key; const bool was_fresh=row.fresh; row={}; row.fill="0%"; row.bottom="0dp";
        if (!observer) continue;
        const Rml::String prefix="broadcast_player_"+std::to_string(i)+"_";
        const auto key=value(prefix+"key"); if (key.empty()) continue;
        row.present=true; row.key=key; row.fresh=old_key!=key;
        if (was_fresh!=row.fresh) model.DirtyVariable("players"); row.name=value(prefix+"name"); row.axis=i>=5;
        row.alive=number(prefix+"alive")!=0; row.observed=followed>=0 && number(prefix+"entity")==followed;
        const int health=row.alive ? std::max(0,std::min(number(prefix+"health"),100)) : 0;
        row.health=std::to_string(health); row.slot=std::to_string(i==9 ? 0 : i+1);
        row.fill=std::to_string(health)+"%";
        row.condensed=compact || !row.alive;
        const auto weapon=value(prefix+"weapon");
        if (!weapon.empty() && weapon.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==Rml::String::npos) row.weapon="images/"+weapon+".tga";
        row.utility=number(prefix+"flash") ? "images/eq-flash.tga" : "images/eq-smoke.tga";
        row.frag=number(prefix+"frag")!=0;
    }
    for (int team=0;team<2;++team) {
        int bottom=36;
        for (int slot=4;slot>=0;--slot) {
            auto& row=players[team*5+slot]; if (!row.present) continue;
            row.bottom=std::to_string(bottom)+"dp"; bottom+=(row.condensed ? 34 : 66)+4;
        }
        (team==0 ? left_header_bottom : right_header_bottom)=std::to_string(bottom+3)+"dp";
    }
    if (before_compact!=compact || before_hidden!=hidden) model.DirtyAllVariables();
    if (!document && !Reload()) { retry_at=seconds+2; return false; }
    const float density=std::max(.25f,std::min(width/1920.f,height/1080.f));
    const bool resized=context->GetDimensions()!=Rml::Vector2i(width,height) || context->GetDensityIndependentPixelRatio()!=density;
    context->SetDimensions({width,height}); context->SetDensityIndependentPixelRatio(density);
    context->Update(); if (resized) context->Update(); return true;
}
void BroadcastView::Render() { if (context && document) context->Render(); }
}
