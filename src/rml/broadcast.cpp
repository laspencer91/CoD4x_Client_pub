#include "broadcast.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cod4x::rml {
namespace {
constexpr int kCardGap=4, kHeaderGap=3;
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
    context=nullptr; document=nullptr; model={}; players.clear(); follow={}; values.Clear(); retry_at=0;
    observer=compact=hidden=planted=armor_visible=false; armor_charges=0; last_visible_mode=CardMode::Expanded;
    team_factions.clear(); team_style.clear();
}
bool BroadcastView::Reload() {
    if (!context) return true;
    auto* next=context->LoadDocument("ui/rml/broadcast.rml");
    if (!next) return false;
    if (document) document->Close();
    document=next; document->Show(); MeasureCards();
    if (model) model.DirtyAllVariables(); // row heights may have changed with the stylesheet
    return true;
}
// Probes the stylesheet's row and header heights so stacking never hard-codes them.
void BroadcastView::MeasureCards() {
    auto* stage=document->QuerySelector(".broadcast-stage"); if (!stage) return;
    float* targets[]={&expanded_height,&condensed_height,&dead_height,&header_height};
    const char* classes[]={"player-card","player-card condensed","player-card dead","roster-heading"};
    Rml::Element* probes[4]={};
    for (int i=0;i<4;++i) {
        auto probe=document->CreateElement("div"); probe->SetClassNames(classes[i]);
        probe->SetProperty("display","none"); probes[i]=stage->AppendChild(std::move(probe));
    }
    context->Update();
    for (int i=0;i<4;++i) {
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
        ROW(key); ROW(name); ROW(health); ROW(slot); ROW(weapon); ROW(utility); ROW(fill); ROW(top);
        ROW(present); ROW(condensed); ROW(fresh); ROW(axis); ROW(alive); ROW(observed); ROW(frag);
#undef ROW
        binding.RegisterArray<std::vector<BroadcastPlayer>>();
#define BIND(field) binding.Bind(#field,&field)
        BIND(players); BIND(follow); BIND(left_score); BIND(right_score); BIND(clock); BIND(round);
        BIND(left_alive); BIND(right_alive); BIND(observer); BIND(relative); BIND(team_style); BIND(compact); BIND(hidden); BIND(planted);
        BIND(armor_visible); BIND(armor_charges);
        BIND(name_color8); BIND(name_color9); BIND(header_top);
#undef BIND
        players.resize(10); header_top="0dp";
        model=binding.GetModelHandle();
    }
    if (!document && seconds<retry_at) return false;
    if (!document && !Reload()) { retry_at=seconds+2; return false; }
    const bool before_compact=compact, before_hidden=hidden;
    const auto card_mode=static_cast<CardMode>(std::max(0,std::min(number("cg_rmlCards"),2)));
    if (card_mode!=CardMode::Hidden) last_visible_mode=card_mode;
    compact=last_visible_mode==CardMode::Compact; hidden=card_mode==CardMode::Hidden;
    // Everything here is already on the client: team, scores and clock from the engine (CL_Rml_Value), the bomb
    // and round from server info (ui_bomb_timer, ui_round), and the observer roster from Promod's shoutcaster
    // slots (shout_*N).
    // Players see their own team on the left in Promod's friendly/enemy colours; observers see Allies/Axis.
    const auto viewer=value("rml_local_team");
    observer=viewer=="spectator";
    relative=viewer=="allies" || viewer=="axis";
    const Rml::String left=viewer=="axis" ? "axis" : "allies", right=left=="axis" ? "allies" : "axis";
    const int left_points=number("rml_score_"+left), right_points=number("rml_score_"+right);
    left_score=std::to_string(left_points); right_score=std::to_string(right_points);
    round=value("ui_round");
    // Observers use the scoreboard's faction colours; the stylesheet's team tokens remain the fallback.
    const Rml::String allies=observer ? value("rml_team_color_allies") : Rml::String(), axis=observer ? value("rml_team_color_axis") : Rml::String();
    if (allies+axis!=team_factions) { team_factions=allies+axis; team_style=TeamStyle(allies,axis); }
    planted=number("ui_bomb_timer")!=0; name_color8=value("rml_name_color_8"); name_color9=value("rml_name_color_9");
    // The local player's body armor (economy mode); the HUD shows one pip per charge of the two-charge vest.
    armor_charges=std::max(0,std::min(number("economy_armor_charges"),2));
    armor_visible=!observer && number("economy_armor_capacity")>0 && armor_charges>0;
    clock=value("rml_seconds_left").empty() ? Rml::String() : FormatClock(std::min(number("rml_seconds_left"),5999),1);
    const auto followed_client=value("rml_follow_client");
    const int followed=followed_client.empty() ? -1 : std::atoi(followed_client.c_str());
    int alive[2]={0,0};
    for (int i=0;i<10;++i) {
        auto& row=players[i]; const auto old_key=row.key; const bool was_fresh=row.fresh; row={};
        if (!observer) continue;
        // shoutcast.gsc sendSlot(): name, health (0..1), client number, weapon, utility and frag per slot.
        const auto slot=std::to_string(i), name=value("shout_name"+slot), client=value("shout_client"+slot);
        if (name.empty() || client.empty()) continue;
        row.present=true; row.key=client+":"+name; row.fresh=old_key!=row.key;
        if (was_fresh!=row.fresh) model.DirtyVariable("players");
        row.name=name; row.axis=i>=5;
        const int health=std::max(0,std::min(int(std::lround(std::atof(value("shout_health"+slot).c_str())*100)),100));
        row.alive=health>0; alive[row.axis]+=row.alive;
        row.observed=followed>=0 && std::atoi(client.c_str())==followed;
        row.health=std::to_string(health); row.slot=std::to_string(i==9 ? 0 : i+1);
        row.fill=std::to_string(health)+"%";
        row.condensed=compact && row.alive;
        row.weapon=ImagePath(value("shout_weapon"+slot));
        row.utility=UtilityImage(value("shout_utility"+slot));
        row.frag=number("shout_frag"+slot)!=0;
    }
    left_alive=std::to_string(alive[0]); right_alive=std::to_string(alive[1]); // observers: allies left
    // Both headers sit where a full stack of five living players would be centred on the screen's middle (the
    // roster's origin); rows hang below them, so deaths collapse rows without moving the headers.
    const float live_height=compact ? condensed_height : expanded_height;
    const int stack_top=-int(std::lround((header_height+kHeaderGap+5*live_height+4*kCardGap)/2));
    header_top=std::to_string(stack_top)+"dp";
    for (int team=0;team<2;++team) {
        int top=stack_top+int(std::lround(header_height))+kHeaderGap;
        for (int slot=0;slot<5;++slot) {
            auto& row=players[team*5+slot]; if (!row.present) continue;
            row.top=std::to_string(top)+"dp"; top+=int(std::lround(row.alive ? live_height : dead_height))+kCardGap;
        }
    }
    // The followed player's plate; Promod sends roster slots only to spectators.
    follow={};
    for (const auto& row : players) if (row.present && row.observed) { follow=row; break; }
    if (before_compact!=compact || before_hidden!=hidden) model.DirtyAllVariables();
    UpdateStage(context,FitStage(context,width,height,1920.f,1080.f,.25f)); return true;
}
void BroadcastView::Render() { if (context && document) context->Render(); }
}
