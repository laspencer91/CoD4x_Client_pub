#include "broadcast.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cod4x::rml {
namespace {
constexpr int kStackBase=36, kCardGap=4, kHeaderGap=3;
// Faction colours are dark scoreboard banner tints; lift them toward white for text over the scene.
Rml::String LiftColor(const Rml::String& hex) {
    const unsigned long rgb=std::strtoul(hex.c_str()+1,nullptr,16);
    int c[3]={int(rgb>>16&255),int(rgb>>8&255),int(rgb&255)};
    for (int& v : c) v+=(255-v)*35/100;
    char out[8]; std::snprintf(out,sizeof(out),"#%02x%02x%02x",c[0],c[1],c[2]); return out;
}
// Overrides the stylesheet's --team-* tokens with the scoreboard's faction colours.
Rml::String TeamStyle(const Rml::String& allies, const Rml::String& axis) {
    Rml::String style;
    for (const auto& [side, colour] : {std::pair<const char*, const Rml::String&>{"allies", allies}, {"axis", axis}}) {
        if (colour.size()!=7 || colour[0]!='#') continue;
        const Rml::String token=Rml::String("--team-")+side;
        style+=token+": "+colour+"; "+token+"-soft: "+colour+"70; "+token+"-fill: "+colour+"20; "+token+"-text: "+LiftColor(colour)+"; ";
    }
    return style;
}
}
void BroadcastView::Reset() {
    context=nullptr; document=nullptr; model={}; players.clear(); values.Clear(); retry_at=0;
    observer=compact=hidden=planted=false; last_visible_mode=CardMode::Expanded;
    team_factions.clear(); team_style.clear();
}
bool BroadcastView::Reload() {
    if (!context) return true;
    auto* next=context->LoadDocument("ui/rml/broadcast.rml");
    if (!next) return false;
    if (document) document->Close();
    document=next; document->Show(); MeasureCards(); return true;
}
// Probes the stylesheet's card heights so stacking never hard-codes them.
void BroadcastView::MeasureCards() {
    auto* stage=document->QuerySelector(".broadcast-stage"); if (!stage) return;
    float* targets[]={&expanded_height,&condensed_height};
    Rml::Element* probes[2]={};
    for (int i=0;i<2;++i) {
        auto probe=document->CreateElement("div"); probe->SetClassNames(i ? "player-card condensed" : "player-card");
        probe->SetProperty("display","none"); probes[i]=stage->AppendChild(std::move(probe));
    }
    context->Update();
    for (int i=0;i<2;++i) {
        const auto* height=probes[i]->GetProperty(Rml::PropertyId::Height);
        if (height && height->unit==Rml::Unit::DP) *targets[i]=height->Get<float>();
        else if (height && height->unit==Rml::Unit::PX) *targets[i]=height->Get<float>()/context->GetDensityIndependentPixelRatio();
        stage->RemoveChild(probes[i]);
    }
}
bool BroadcastView::Prepare(const rml_host_t& host, int width, int height, double seconds) {
    auto value=[&](const Rml::String& name) { return values.Read(host,name,model); };
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
        BIND(left_alive); BIND(right_alive); BIND(phase); BIND(observer); BIND(relative); BIND(team_style); BIND(compact); BIND(hidden); BIND(planted);
        BIND(name_color8); BIND(name_color9); BIND(left_header_bottom); BIND(right_header_bottom);
#undef BIND
        players.resize(10); left_header_bottom=right_header_bottom="0dp";
        for (auto& row : players) { row.fill="0%"; row.bottom="0dp"; }
        model=binding.GetModelHandle();
    }
    if (!document && seconds<retry_at) return false;
    if (!document && !Reload()) { retry_at=seconds+2; return false; }
    const bool before_compact=compact, before_hidden=hidden;
    const auto card_mode=static_cast<CardMode>(std::max(0,std::min(number("cg_rmlCards"),2)));
    if (card_mode!=CardMode::Hidden) last_visible_mode=card_mode;
    compact=last_visible_mode==CardMode::Compact; hidden=card_mode==CardMode::Hidden;
    observer=value("broadcast_mode")=="observer";
    // Players see their own team on the left in Promod's friendly/enemy colours; observers see Allies/Axis.
    const auto viewer=value("rml_local_team");
    relative=!observer && (viewer=="allies" || viewer=="axis");
    const Rml::String left=relative && viewer=="axis" ? "axis" : "allies", right=left=="axis" ? "allies" : "axis";
    left_score=value("broadcast_score_"+left); right_score=value("broadcast_score_"+right);
    left_alive=value("broadcast_alive_"+left); right_alive=value("broadcast_alive_"+right);
    // Observers use the scoreboard's faction colours; the stylesheet's team tokens remain the fallback.
    const Rml::String allies=observer ? value("rml_team_color_allies") : Rml::String(), axis=observer ? value("rml_team_color_axis") : Rml::String();
    if (allies+axis!=team_factions) { team_factions=allies+axis; team_style=TeamStyle(allies,axis); }
    round=value("broadcast_round"); target=value("broadcast_target"); phase=value("broadcast_phase");
    planted=phase=="BOMB PLANTED"; name_color8=value("rml_name_color_8"); name_color9=value("rml_name_color_9");
    const int remaining=std::max(0,std::min(number("broadcast_seconds"),5999));
    clock=FormatClock(remaining,1);
    const auto follow=value("rml_follow_client");
    const int followed=follow.empty() ? -1 : std::atoi(follow.c_str());
    for (int i=0;i<10;++i) {
        auto& row=players[i]; const auto old_key=row.key; const bool was_fresh=row.fresh; row={}; row.fill="0%"; row.bottom="0dp";
        if (!observer) continue;
        const Rml::String prefix="broadcast_player_"+std::to_string(i)+"_";
        const auto key=value(prefix+"key"); if (key.empty()) continue;
        row.present=true; row.key=key; row.fresh=old_key!=key;
        if (was_fresh!=row.fresh) model.DirtyVariable("players");
        row.name=value(prefix+"name"); row.axis=i>=5;
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
        int bottom=kStackBase;
        for (int slot=4;slot>=0;--slot) {
            auto& row=players[team*5+slot]; if (!row.present) continue;
            row.bottom=std::to_string(bottom)+"dp"; bottom+=int(std::lround(row.condensed ? condensed_height : expanded_height))+kCardGap;
        }
        (team==0 ? left_header_bottom : right_header_bottom)=std::to_string(bottom+kHeaderGap)+"dp";
    }
    if (before_compact!=compact || before_hidden!=hidden) model.DirtyAllVariables();
    UpdateStage(context,FitStage(context,width,height,1920.f,1080.f,.25f)); return true;
}
void BroadcastView::Render() { if (context && document) context->Render(); }
}
