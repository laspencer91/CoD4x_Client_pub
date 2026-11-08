#include "broadcast.hpp"
#include "avatars.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstdio>
#include <cstdlib>

namespace cod4x::rml {
namespace {
constexpr int kCardGap=4;
// Faction colours are dark scoreboard banner tints; lift them toward white for text over the scene.
Rml::String LiftColor(const Rml::String& hex) {
    const unsigned long rgb=std::strtoul(hex.c_str()+1,nullptr,16);
    int c[3]={int(rgb>>16&255),int(rgb>>8&255),int(rgb&255)};
    for (int& v : c) v+=(255-v)*35/100;
    char out[8]; std::snprintf(out,sizeof(out),"#%02x%02x%02x",c[0],c[1],c[2]); return out;
}
// Up to two characters of the name without colour codes or spaces, upper case: a tile without an image.
Rml::String Initials(const Rml::String& name) {
    Rml::String out; int characters=0;
    for (const unsigned char c : StripColors(name)) {
        if (c==' ') continue;
        if ((c & 0xC0)!=0x80 && ++characters>2) break; // count UTF-8 lead bytes only
        out+=char(c>='a' && c<='z' ? c-'a'+'A' : c);
    }
    return out;
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
// Probes the stylesheet's row heights so stacking never hard-codes them.
void BroadcastView::MeasureCards() {
    auto* stage=document->QuerySelector(".broadcast-stage"); if (!stage) return;
    float* targets[]={&expanded_height,&condensed_height,&dead_height};
    const char* classes[]={"player-card","player-card condensed","player-card dead"};
    Rml::Element* probes[3]={};
    for (int i=0;i<3;++i) {
        auto probe=document->CreateElement("div"); probe->SetClassNames(classes[i]);
        probe->SetProperty("display","none"); probes[i]=stage->AppendChild(std::move(probe));
    }
    context->Update();
    for (int i=0;i<3;++i) {
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
        ROW(key); ROW(name); ROW(plain); ROW(health); ROW(slot); ROW(weapon); ROW(utility); ROW(initials); ROW(avatar); ROW(ghost); ROW(fill); ROW(top);
        ROW(present); ROW(condensed); ROW(fresh); ROW(axis); ROW(alive); ROW(observed); ROW(frag);
        ROW(own); ROW(other); ROW(low); ROW(hit);
#undef ROW
        binding.RegisterArray<std::vector<BroadcastPlayer>>();
        auto line=binding.RegisterStruct<CornerLine>();
        line.RegisterMember("text",&CornerLine::text); line.RegisterMember("label",&CornerLine::label); line.RegisterMember("present",&CornerLine::present);
        binding.RegisterArray<std::vector<CornerLine>>();
#define BIND(field) binding.Bind(#field,&field)
        BIND(players); BIND(follow); BIND(corner_lines); BIND(corner_labels); BIND(lag_on); BIND(lag_snc); BIND(lag_frames); BIND(lag_snaps); BIND(lag_delayed); BIND(left_score); BIND(right_score); BIND(clock); BIND(round);
        BIND(left_alive); BIND(right_alive); BIND(observer); BIND(relative); BIND(team_style); BIND(compact); BIND(hidden); BIND(planted);
        BIND(armor_visible); BIND(armor_charges);
#undef BIND
        players.resize(10); corner_lines.resize(16);
        model=binding.GetModelHandle();
    }
    if (!document && seconds<retry_at) return false;
    if (!document && !Reload()) { retry_at=seconds+2; return false; }
    const bool before_compact=compact, before_hidden=hidden;
    const auto card_mode=static_cast<CardMode>(std::max(0,std::min(number("cg_rmlCards"),2)));
    if (card_mode!=CardMode::Hidden) last_visible_mode=card_mode;
    compact=last_visible_mode==CardMode::Compact; hidden=card_mode==CardMode::Hidden;
    // Everything here is already on the client: team, scores, clock and every client's name and team from the engine
    // (CL_Rml_Value), the bomb and round from server info (ui_bomb_timer, ui_round), and the roster from Promod's
    // shoutcaster slots (shout_*N).
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
    planted=number("ui_bomb_timer")!=0;
    // The local player's body armor (economy mode); the HUD shows one pip per charge of the two-charge vest.
    armor_charges=std::max(0,std::min(number("economy_armor_charges"),2));
    armor_visible=!observer && number("economy_armor_capacity")>0 && armor_charges>0;
    clock=value("rml_seconds_left").empty() ? Rml::String() : FormatClock(std::min(number("rml_seconds_left"),5999),1);
    const auto followed_client=value("rml_follow_client");
    const int followed=followed_client.empty() ? -1 : std::atoi(followed_client.c_str());
    // Rows. shoutcast.gsc sendSlot() fills slots 0-4 (allies) and 5-9 (axis): every slot for a spectator, only their
    // own team's for a player. A player's other-team tiles come from the engine's client list and show identity only.
    // Rows are read untracked; a changed row dirties only the rows.
    auto raw=[&](const Rml::String& name) { return DvarSnapshot::Fetch(host,name); };
    std::vector<Rml::String> others;
    if (relative) { std::istringstream list(raw("rml_other_clients")); for (Rml::String client; others.size()<5 && list>>client;) others.push_back(client); }
    const auto previous=players; const auto previous_follow=follow; const auto previous_alive=left_alive+"/"+right_alive;
    int alive[2]={0,0};
    for (int i=0;i<10;++i) {
        auto& row=players[i]; const auto& old=previous[i]; row={};
        const bool other=relative && (i>=5)!=(viewer=="axis");
        const auto slot=std::to_string(i);
        Rml::String name, client;
        if (other && size_t(i%5)<others.size()) { client=others[i%5]; name=raw("rml_client_name_"+client); }
        else if (!other && (observer || relative)) { name=raw("shout_name"+slot); client=raw("shout_client"+slot); }
        if (name.empty() || client.empty()) continue;
        row.present=true; row.key=client+":"+name; row.fresh=old.key!=row.key;
        row.name=name; row.plain=StripColors(name); row.axis=i>=5; row.own=relative && !other; row.other=other;
        if (relative) { row.initials=Initials(name); row.avatar=!row.fresh && !old.avatar.empty() ? old.avatar : avatars::Source(name,seconds); }
        if (other) continue;
        const int health=std::max(0,std::min(int(std::lround(std::atof(raw("shout_health"+slot).c_str())*100)),100));
        row.alive=health>0; alive[row.axis]+=row.alive; row.low=row.alive && health<=25;
        row.hit=row.alive && !row.fresh && health<std::atoi(old.health.c_str()); // one frame; the stylesheet fades it out
        row.observed=followed>=0 && std::atoi(client.c_str())==followed;
        row.health=std::to_string(health); row.slot=std::to_string(i==9 ? 0 : i+1);
        row.fill=std::to_string(health)+"%";
        row.condensed=compact && row.alive;
        row.weapon=ImagePath(raw("shout_weapon"+slot));
        row.utility=UtilityImage(raw("shout_utility"+slot));
        row.frag=std::atoi(raw("shout_frag"+slot).c_str())!=0;
    }
    left_alive=std::to_string(alive[0]); right_alive=std::to_string(alive[1]); // observers: allies left
    // Each stack hangs from the roster's top (under the minimap); deaths collapse rows towards it.
    const float live_height=compact ? condensed_height : expanded_height;
    for (int team=0;team<2;++team) {
        int top=0;
        for (int slot=0;slot<5;++slot) {
            auto& row=players[team*5+slot]; if (!row.present) continue;
            row.top=std::to_string(top)+"dp"; top+=int(std::lround(row.alive ? live_height : dead_height))+kCardGap;
        }
    }
    // The followed player's plate: anyone an observer follows, or the teammate a dead player follows (only own rows
    // can be observed for a player).
    follow={};
    for (const auto& row : players) if (row.present && row.observed) { follow=row; break; }
    if (follow.present && observer) follow.avatar=avatars::Source(follow.name,seconds); // observers' rows skip images
    follow.ghost=avatars::Ghost(follow.avatar);
    // The stock FPS readout (Options: FPS Counter) in this HUD's style: the engine's own lines, which it skips drawing
    // while this HUD is drawn. A fixed 16 rows, as the engine prints at most a handful.
    const auto previous_corner=corner_lines;
    const int corner_count=std::max(0,std::min(std::atoi(raw("rml_corner_lines").c_str()),16));
    for (int i=0;i<16;++i) {
        const auto n=std::to_string(i);
        corner_lines[i]=i<corner_count ? CornerLine{raw("rml_corner_text_"+n),raw("rml_corner_label_"+n),true} : CornerLine{};
    }
    corner_labels=number("cg_drawFPSLabels")!=0;
    // The stock lagometer in this HUD's style: the engine's own samples, read only while it is on.
    const bool lag_was_on=lag_on;
    lag_on=raw("rml_lagometer_on")=="1";
    if (lag_on) {
        auto frames=raw("rml_lagometer_frames"), snaps=raw("rml_lagometer_snaps"), delayed=raw("rml_lagometer_delayed");
        const bool snc=raw("rml_lagometer_snc")=="1";
        if (frames!=lag_frames || snaps!=lag_snaps || delayed!=lag_delayed) {
            lag_frames=std::move(frames); lag_snaps=std::move(snaps); lag_delayed=std::move(delayed);
            model.DirtyVariable("lag_frames"); model.DirtyVariable("lag_snaps"); model.DirtyVariable("lag_delayed");
        }
        if (snc!=lag_snc) { lag_snc=snc; model.DirtyVariable("lag_snc"); }
    }
    if (lag_on!=lag_was_on) model.DirtyVariable("lag_on");
    if (corner_lines!=previous_corner) model.DirtyVariable("corner_lines");
    if (players!=previous) model.DirtyVariable("players");
    if (follow!=previous_follow) model.DirtyVariable("follow");
    if (left_alive+"/"+right_alive!=previous_alive) { model.DirtyVariable("left_alive"); model.DirtyVariable("right_alive"); }
    if (before_compact!=compact || before_hidden!=hidden) model.DirtyAllVariables();
    // Measured against 1440 x 1080 rather than 1920 x 1080: 16:9 and wider keep the 1080p scale, while 4:3 and 5:4
    // (common in competitive CoD4) scale by height instead of shrinking to fit 1920 dp across. Everything fits in 1440 dp.
    UpdateStage(context,FitStage(context,width,height,1440.f,1080.f,.25f)); return true;
}
void BroadcastView::Render() { if (context && document) context->Render(); }
}
