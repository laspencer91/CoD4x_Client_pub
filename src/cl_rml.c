#include "q_shared.h"
#include "qcommon.h"
#include "client.h"
#include "cg_shared.h"
#include "cl_input.h"
#include "r_shared.h"
#include "ui_shared.h"
#include "snd_system.h"
#include "keys.h"
#include "cl_rml.h"
#include "rml/rml_host.h"
#include "rml/native_keys.h"
#include "win_sys.h"
#include "sys_patch.h"
#include <stdlib.h>

/* The native mouse route uses bit 0x10 for modal menus. */
#define RML_MENU_CATCHER 0x10
static cvar_t *rml_enabled;
static cvar_t *rml_economy, *rml_broadcast, *rml_cards;
static qboolean rml_captured;
static int rml_previous_catcher, rml_modifiers;
static struct { menuDef_t *menu; int item_count; } rml_menus[4];
static char rml_response[96];
static int rml_retry_at;
/* The buy key opens the economy screen locally one frame after its bind asks the server; the server's
 * menu then adopts the open screen. Times are Sys_Milliseconds; zero means none pending. */
static qboolean rml_open_pending, rml_first_frame_pending;
static int rml_buy_key_at, rml_local_open_at;
static int rml_scores_server_id = -1; /* map load (cl.serverId) whose team scores were last requested */
static qboolean rml_follow_drawn;
/* The stock FPS readout in the HUD's style (cl_rml_stock.asm). While the HUD is drawn, each line CG_DrawFPSDebugInfo
 * prints is kept here instead of drawn; the engine fills one buffer while the HUD reads the other. */
#define RML_CORNER_LINES 16
int rml_corner_capture;
int rml_hud_drawn; /* the match HUD was drawn last frame: it shows the stock readouts in its style */
static struct { char text[96], label[32]; } rml_corner[2][RML_CORNER_LINES];
static int rml_corner_count[2], rml_corner_front, rml_corner_time, rml_corner_patched;
static Material *rml_cursor;
static int rml_cursor_x, rml_cursor_y;
static int rml_other_thread_frames; /* screen updates from the render thread during loads, which RML skips */
#ifdef COD4X_TEST_ISOLATION
static int rml_test_x = -1, rml_test_y, rml_test_button, rml_test_deadline;
static void CL_Rml_TestClick_f(void)
{
    if (Cmd_Argc() < 3 || Cmd_Argc() > 4) return;
    rml_test_x = atoi(Cmd_Argv(1)); rml_test_y = atoi(Cmd_Argv(2));
    rml_test_button = Cmd_Argc() == 4 && !strcmp(Cmd_Argv(3), "right");
    rml_test_deadline = Sys_Milliseconds() + 15000;
}
#endif
static int CL_Rml_Response(const char *response);

static void CL_Rml_RestoreMenus(void)
{
    int i;
    for (i = 0; i < 4; ++i) if (rml_menus[i].menu) {
        rml_menus[i].menu->itemCount = rml_menus[i].item_count;
        rml_menus[i].menu = NULL;
    }
}

static void CL_Rml_Log(const char *message) { Com_Printf(CON_CHANNEL_UI, "RmlUi: %s\n", message); }
static void CL_Rml_Sound(const char *alias) { SND_PlayLocalSoundAliasByName(0, alias); }
static double CL_Rml_Time(void) { return Sys_Milliseconds() / 1000.0; }

void CL_Rml_Hide(void)
{
    CL_Rml_RestoreMenus();
    rml_response[0] = 0;
    rml_open_pending = rml_first_frame_pending = qfalse;
    rml_local_open_at = 0;
    RmlHost_Hide();
    if (rml_captured) {
        Key_ClearStates(0);
        Key_SetCatcher((clientUIActives.keyCatchers & ~RML_MENU_CATCHER) | rml_previous_catcher);
        rml_captured = qfalse;
        rml_modifiers = 0;
        CL_ShowSystemCursor(IN_AppActive() ? 0 : 1);
    }
}

void CL_Rml_FocusLost(void)
{
    if (!RmlHost_Visible()) return;
    RmlHost_ClearInput();
    rml_modifiers = 0;
    CL_ShowSystemCursor(1);
}

qboolean CG_GetClientName(int index, char *buf, int size);

float CL_Rml_CornerLine(const char *text, const char *label)
{
    const int back = !rml_corner_front, n = rml_corner_count[back];
    if (n < RML_CORNER_LINES) {
        Q_strncpyz(rml_corner[back][n].text, text ? text : "", sizeof(rml_corner[back][n].text));
        Q_strncpyz(rml_corner[back][n].label, label ? label : "", sizeof(rml_corner[back][n].label));
        rml_corner_count[back] = n + 1;
    }
    return 0.0f;
}
void CL_Rml_CornerBegin(void) { rml_corner_count[!rml_corner_front] = 0; rml_corner_capture = rml_hud_drawn; }
void CL_Rml_CornerEnd(void) { rml_corner_capture = 0; rml_corner_front = !rml_corner_front; rml_corner_time = Sys_Milliseconds(); }

void CL_Rml_CornerPrintStub(void);
void CL_Rml_CornerHeadingStub(void);
void CL_Rml_LagometerStub(void);
/* Changes a call only where the stock 1.7 executable has the expected one. */
static void CL_Rml_PatchCall(DWORD at, DWORD expected, void (*stub)(void))
{
    const byte *code = (const byte *)at;
    if (code[0] != 0xE8 || at + 5 + *(const int32_t *)(code + 1) != expected) return;
    SetCall(at, (void *)stub);
    ++rml_corner_patched;
}
void CL_Rml_Patch(void)
{
    /* CG_DrawFPSDebugInfo's calls to its line print (0x42B710) and section heading (0x42B800). */
    static const DWORD prints[] = {0x42BB2B, 0x42BB85, 0x42BC27, 0x42BC83, 0x42BCE0, 0x42BD35, 0x42BD7C, 0x42BDFF, 0x42BE8E};
    static const DWORD headings[] = {0x42BBCF, 0x42BDAA};
    unsigned i;
    for (i = 0; i < sizeof(prints) / sizeof(*prints); ++i) CL_Rml_PatchCall(prints[i], 0x42B710, CL_Rml_CornerPrintStub);
    for (i = 0; i < sizeof(headings) / sizeof(*headings); ++i) CL_Rml_PatchCall(headings[i], 0x42B800, CL_Rml_CornerHeadingStub);
    /* CG_Draw2D's call to the lagometer. */
    CL_Rml_PatchCall(0x42F77D, 0x42FAC0, CL_Rml_LagometerStub);
}

static const char* CL_Rml_Value(const char* name)
{
    static char color[10];
    if (!strcmp(name,"rml_follow_client")) {
        const snapshot_t* snap=cg.nextSnap ? cg.nextSnap : cg.snap;
        const int followed=snap && (snap->ps.otherFlags & 2) && !cg.inKillCam ? snap->ps.clientNum : -1;
        Com_sprintf(color,sizeof(color),"%d",followed); return color;
    }
    if (!strcmp(name,"rml_name_color_8") || !strcmp(name,"rml_name_color_9")) {
        unsigned char rgba[4];
        RB_LookupColor(name[15],(char*)rgba);
        Com_sprintf(color,sizeof(color),"#%02x%02x%02x%02x",rgba[0],rgba[1],rgba[2],rgba[3]);
        return color;
    }
    /* Engine state the HUD reads directly: team scores (setTeamScore and the score reply) and the round or bomb
     * clock (setGameEndTime). No end time means no clock. */
    if (!strcmp(name,"rml_score_allies")) { Com_sprintf(color,sizeof(color),"%d",cg.teamScores[2]); return color; }
    if (!strcmp(name,"rml_score_axis")) { Com_sprintf(color,sizeof(color),"%d",cg.teamScores[1]); return color; }
    if (!strcmp(name,"rml_seconds_left")) {
        if (cgs.gameEndTime <= 0) return "";
        Com_sprintf(color,sizeof(color),"%d",cgs.gameEndTime > cg.time ? (cgs.gameEndTime - cg.time + 999) / 1000 : 0); return color;
    }
    /* Every client's name and team reach every client (client info); the HUD's other-team tiles show only those. */
    if (!strcmp(name,"rml_other_clients")) {
        static char list[64 * 3 + 1];
        const team_t mine = cg.bgs.clientinfo[cg.clientNum].team;
        const team_t other = mine == TEAM_RED ? TEAM_BLUE : mine == TEAM_BLUE ? TEAM_RED : TEAM_FREE;
        int n, length = 0;
        list[0] = 0;
        for (n = 0; n < 64 && other != TEAM_FREE; ++n)
            if (cg.bgs.clientinfo[n].infoValid && cg.bgs.clientinfo[n].team == other)
                length += Com_sprintf(list + length, sizeof(list) - length, "%d ", n);
        return list;
    }
    if (!strncmp(name,"rml_client_name_",16)) {
        /* The name the engine's own HUD shows (CoD4x servers send longer names than client info holds). */
        static char client_name[64];
        const int n = atoi(name + 16);
        if (n < 0 || n >= 64 || !CG_GetClientName(n, client_name, sizeof(client_name))) client_name[0] = 0;
        return client_name;
    }
    /* The stock FPS readout's lines from the last frame the engine drew it (none once it stops). */
    if (!strcmp(name,"rml_corner_lines")) {
        Com_sprintf(color,sizeof(color),"%d",Sys_Milliseconds() - rml_corner_time < 500 ? rml_corner_count[rml_corner_front] : 0);
        return color;
    }
    if (!strncmp(name,"rml_corner_text_",16) || !strncmp(name,"rml_corner_label_",17)) {
        const qboolean text = name[11] == 't';
        const int n = atoi(name + (text ? 16 : 17));
        if (n < 0 || n >= rml_corner_count[rml_corner_front]) return "";
        return text ? rml_corner[rml_corner_front][n].text : rml_corner[rml_corner_front][n].label;
    }
    /* The stock lagometer's samples (lagometer_t at 0x7440D8, written on this thread once per frame and once per
     * snapshot), newest first, the 48 the stock graph draws. Shown under the stock rules: cg_drawLagometer, never on
     * a listen server; "snc" with cg_nopredict or g_synchronousClients. */
    if (!strcmp(name,"rml_lagometer_on")) return Cvar_GetInt("cg_drawLagometer") && !cgs.localServer ? "1" : "0";
    if (!strcmp(name,"rml_lagometer_snc")) return Cvar_GetInt("cg_nopredict") || Cvar_GetInt("g_synchronousClients") ? "1" : "0";
    if (!strcmp(name,"rml_lagometer_frames") || !strcmp(name,"rml_lagometer_snaps") || !strcmp(name,"rml_lagometer_delayed")) {
        static char list[48 * 8];
        const lagometer_t *lag = (const lagometer_t *)0x7440D8;
        const char which = name[14];
        int a, length = 0;
        list[0] = 0;
        for (a = 0; a < 48; ++a) {
            const int frame = lag->frameSamples[(lag->frameCount - 1 - a) & 127];
            const int snap = (lag->snapshotCount - 1 - a) & 127;
            if (which == 'd') list[length++] = lag->snapshotFlags[snap] & 1 ? '1' : '0';
            else length += Com_sprintf(list + length, sizeof(list) - length, "%d,", which == 'f' ? frame : lag->snapshotSamples[snap]);
        }
        list[length] = 0;
        return list;
    }
    if (!strcmp(name,"rml_local_team")) {
        static const char *teams[] = {"", "axis", "allies", "spectator"};
        const int team = cg.bgs.clientinfo[cg.clientNum].team;
        return team >= 0 && team < 4 ? teams[team] : "";
    }
    /* Faction colours set by _scoreboard.gsc, resolved the way the scoreboard resolves them. */
    if (!strcmp(name,"rml_team_color_allies") || !strcmp(name,"rml_team_color_axis")) {
        const char* source=!strcmp(name,"rml_team_color_allies") ? "g_TeamColor_Allies" : "g_TeamColor_Axis";
        float rgb[4];
        int i, c[3];
        if (!Cvar_GetString(source)[0]) return "";
        Cvar_GetUnpackedColorByName(source,rgb);
        for (i=0;i<3;++i) c[i]=rgb[i]<=0 ? 0 : rgb[i]>=1 ? 255 : (int)(rgb[i]*255+0.5f);
        Com_sprintf(color,sizeof(color),"#%02x%02x%02x",c[0],c[1],c[2]);
        return color;
    }
    return Cvar_GetString(name);
}
static int CL_Rml_EnsureHost(void)
{
    const rml_host_t host = {FS_ReadFile, FS_FreeFile, CL_Rml_Log, CL_Rml_Sound, CL_Rml_Time, CL_Rml_Value, CL_Rml_Response};
    return RmlHost_Init(&host);
}
static void CL_Rml_Cards_f(void) { Cvar_SetInt(rml_cards,(rml_cards->integer+1)%3); }
int CL_Rml_FollowDrawn(void) { return rml_follow_drawn; }
void CL_Rml_Disconnect(void)
{
    CL_Rml_Hide(); RmlHost_EnableBroadcast(0); rml_follow_drawn = rml_hud_drawn = qfalse;
    Cvar_SetStringByName("ui_rml_hud","0"); Cvar_SetStringByName("ui_rml_broadcast_drawn","0");
    rml_scores_server_id = -1;
    /* Server-published state that gates the local open and the HUD must not carry over to the next server. */
    Cvar_SetStringByName("flat_economy_live","0"); Cvar_SetStringByName("flat_loadout_ready","0");
    Cvar_SetStringByName("economy_armor_charges","0");
}

static qboolean CL_Rml_Show(rml_screen_t screen)
{
    POINT cursor;
    if (!CL_Rml_EnsureHost() || !RmlHost_Open(screen)) {
        CL_Rml_Hide();
        CL_Rml_Log("diagnostic load failed; native UI remains available");
        return qfalse;
    }
    Cvar_SetBool(rml_enabled, qtrue);
    rml_cursor = Material_RegisterHandle("ui_cursor", 3);
    if (GetCursorPos(&cursor) && ScreenToClient(g_wv.hWnd, &cursor)) {
        rml_cursor_x = cursor.x; rml_cursor_y = cursor.y;
        RmlHost_MouseMove(cursor.x, cursor.y);
    }
    CL_ShowSystemCursor(!IN_AppActive() || !rml_cursor || Material_IsDefault(rml_cursor));
    if (!rml_captured) {
        Key_ClearStates(0);
        rml_previous_catcher = clientUIActives.keyCatchers & RML_MENU_CATCHER;
        Key_SetCatcher(clientUIActives.keyCatchers | RML_MENU_CATCHER);
        rml_captured = qtrue;
    }
    Com_Printf(CON_CHANNEL_UI, "RmlUi: screen %d opened\n", screen);
    return qtrue;
}

static void CL_Rml_Show_f(void) { CL_Rml_RestoreMenus(); CL_Rml_Show(RML_SCREEN_DIAGNOSTIC); }
static void CL_Rml_Palette_f(void) { CL_Rml_RestoreMenus(); CL_Rml_Show(RML_SCREEN_PALETTE); }
static void CL_Rml_EconomyPreview_f(void) { CL_Rml_RestoreMenus(); RmlHost_EconomyDialog(0); CL_Rml_Show(RML_SCREEN_ECONOMY_PREVIEW); }

static int CL_Rml_Response(const char *response)
{
    if ((!rml_menus[0].menu && !rml_local_open_at) || rml_response[0] || !RmlHost_ActionAllowed(response)) return 0;
    /* Match the stock picker: preview the selected weapon immediately. */
    if (!strncmp(response, "pick_primary:", 13) && strlen(response) == 14) {
        char key[40];
        Com_sprintf(key, sizeof(key), "flat_primary_%c_ref", response[13]);
        if (Cvar_GetString(key)[0]) Cvar_SetStringByName("loadout_primary", Cvar_GetString(key));
    } else if (!strncmp(response, "secondary:", 10)) {
        Cvar_SetStringByName("loadout_secondary", response + 10);
    }
    Q_strncpyz(rml_response, response, sizeof(rml_response));
    return 1;
}

static qboolean CL_Rml_EconomyMenusEnabled(void)
{
    return rml_economy && rml_economy->boolean && rml_enabled->boolean && (Cvar_GetInt("flat_economy_live") ||
        (Cvar_GetInt("flat_loadout_ready") && !Q_stricmp(Cvar_GetString("flat_loadout_mode"), "standard")));
}

static void CL_Rml_OpenLocally(void)
{
    const char *team = CL_Rml_Value("rml_local_team");
    rml_open_pending = qfalse;
    if (rml_captured || clientUIActives.state != CA_ACTIVE || uiMem.uiInfo.uiDC.openMenuCount || !CL_Rml_EconomyMenusEnabled() ||
        (Q_stricmp(team, "allies") && Q_stricmp(team, "axis"))) return;
    RmlHost_EconomyDialog(0);
    if (!CL_Rml_Show(RML_SCREEN_ECONOMY)) return;
    rml_local_open_at = Sys_Milliseconds();
    rml_first_frame_pending = qtrue;
}

/* Wraps openScriptMenu: a request to open the class menu also opens the RML screen locally next frame,
 * instead of waiting for the server's reply. Binds, aliases and the console all pass through here. */
void CL_Rml_OpenScriptMenu_f(void)
{
    if (!rml_captured && Cmd_Argc() == 3 && !Q_stricmp(Cmd_Argv(2), "open") &&
        (!Q_stricmp(Cmd_Argv(1), "loadout") || !Q_stricmp(Cmd_Argv(1), "flat_loadout"))) {
        rml_open_pending = qtrue;
        rml_buy_key_at = Sys_Milliseconds();
    }
    CL_OpenScriptMenu_f();
}

void CL_Rml_MenuOpened(menuDef_t *menu)
{
    static const char *names[] = {"flat_loadout", "flat_primary_picker", "flat_secondary_picker", "flat_demolition_confirm"};
    int i;
    if (!CL_Rml_EconomyMenusEnabled() || Sys_Milliseconds() < rml_retry_at) return;
    for (i = 0; i < 4; ++i) if (!Q_stricmp(menu->window.name, names[i])) break;
    if (i == 4 || (i && !rml_menus[0].menu)) return;
    if (!i) {
        if (rml_local_open_at) {
            if (Cvar_GetInt("developer")) Com_Printf(CON_CHANNEL_UI, "RmlUi: server menu arrived %d ms after the buy key\n", Sys_Milliseconds() - rml_buy_key_at);
            rml_local_open_at = 0;
        } else if (!(rml_captured && RmlHost_Visible())) {
            RmlHost_EconomyDialog(0);
            if (!CL_Rml_Show(RML_SCREEN_ECONOMY)) { rml_retry_at=Sys_Milliseconds()+1000; return; }
        }
        rml_retry_at=0;
    }
    if (!rml_menus[i].menu) { rml_menus[i].menu = menu; rml_menus[i].item_count = menu->itemCount; }
    menu->itemCount = 0;
    if (i == 3) RmlHost_EconomyDialog(1);
}

void CL_Rml_MenuClosing(menuDef_t *menu)
{
    int i;
    for (i = 0; i < 4; ++i) if (rml_menus[i].menu == menu) {
        if (!i) { CL_Rml_Hide(); return; }
        menu->itemCount = rml_menus[i].item_count; rml_menus[i].menu = NULL;
        if (i == 3) RmlHost_EconomyDialog(0);
        return;
    }
}

static void CL_Rml_RunResponse(void)
{
    char response[96], script[160];
    itemDef_t item;
    menuDef_t *menu;
    /* A response made on a local open waits for the server's menu to adopt the screen. */
    if (!rml_response[0] || !rml_menus[0].menu) return;
    Q_strncpyz(response, rml_response, sizeof(response)); rml_response[0] = 0;
    if (!strcmp(response, "close")) {
        Menus_CloseByName(rml_menus[3].menu ? "flat_demolition_confirm" : "flat_loadout");
        if (!uiMem.uiInfo.uiDC.openMenuCount) Key_SetCatcher(clientUIActives.keyCatchers & ~RML_MENU_CATCHER);
        return;
    }
    menu = rml_menus[3].menu ? rml_menus[3].menu : rml_menus[0].menu;
    memset(&item, 0, sizeof(item)); item.parent = menu;
    Com_sprintf(script, sizeof(script), "scriptMenuResponse \"%s\";", response);
    Item_RunScript(&uiMem.uiInfo.uiDC, &item, script);
#ifdef COD4X_TEST_ISOLATION
    Com_Printf(CON_CHANNEL_UI, "RmlUi: submitted %s\n", response);
#endif
}

static void CL_Rml_Reload_f(void)
{
    if (!RmlHost_Reload()) CL_Rml_Log("reload failed; previous document retained");
    else CL_Rml_Log("screen reloaded");
}

static void CL_Rml_Status_f(void)
{
    rml_stats_t stats;
    RmlHost_Stats(&stats);
    Com_Printf(CON_CHANNEL_DONT_FILTER,
        "RmlUi: visible=%d recorded=%u rendered=%u draws=%u uploads=%u/%u pending=%u result=0x%08x reloads=%u failed=%u activations=%d\n",
        stats.visible, stats.recorded_frames, stats.rendered_frames, stats.draw_calls,
        stats.geometry_uploads, stats.texture_uploads, stats.pending_frames, (unsigned)stats.last_render_result,
        stats.source_reloads, stats.source_failures, stats.activations);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "RmlUi cost: record=%uus render=%uus per frame; skipped %d render-thread screen updates\n",
        stats.record_us, stats.render_us, rml_other_thread_frames);
    Com_Printf(CON_CHANNEL_UI,"RmlUi broadcast: ready=%d drawn=%d captured=%d hud=%d team=%s allies=%d axis=%d end=%d time=%d cards=%d\n",
        RmlHost_BroadcastReady(),Cvar_GetInt("ui_rml_broadcast_drawn"),rml_captured,Cvar_GetInt("ui_rml_hud"),CL_Rml_Value("rml_local_team"),
        cg.teamScores[2],cg.teamScores[1],cgs.gameEndTime,cg.time,rml_cards->integer);
    Com_Printf(CON_CHANNEL_UI,"RmlUi tiles: other team clients=%s\n",CL_Rml_Value("rml_other_clients"));
    Com_Printf(CON_CHANNEL_UI,"RmlUi stock readouts: %d of 12 calls patched (FPS 11, lagometer 1), hud=%d fps lines=%s lagometer=%s\n",
        rml_corner_patched,rml_hud_drawn,CL_Rml_Value("rml_corner_lines"),CL_Rml_Value("rml_lagometer_on"));
    Com_Printf(CON_CHANNEL_UI,"RmlUi economy: cash=%s frag=%s armor=%s buy=%d allowed=%d adopted=%d\n",
        Cvar_GetString("economy_cash"),Cvar_GetString("economy_frag_owned"),Cvar_GetString("economy_armor_charges"),
        Cvar_GetInt("economy_buy_open"),Cvar_GetInt("economy_purchase_allowed"),rml_menus[0].menu!=NULL);
}

void CL_Rml_Init(void)
{
#ifdef COD4X_TEST_ISOLATION
    const char *source = getenv("COD4X_RML_SOURCE");
    const char *mod_source = getenv("COD4X_RML_MOD_SOURCE");
    if (source && !RmlHost_SetSourceDirectory(source)) CL_Rml_Log("development source directory is unavailable");
    if (mod_source && !RmlHost_SetModSourceDirectory(mod_source)) CL_Rml_Log("mod authoring directory is unavailable");
#endif
    {
        /* Player images for the HUD: <save path>\avatars, beside the profiles (docs/rmlui.md). */
        char avatars[MAX_OSPATH * 3];
        if (WideCharToMultiByte(CP_UTF8, 0, FS_GetSavePath(), -1, avatars, sizeof(avatars) - 9, NULL, NULL) > 1) {
            strcat(avatars, "\\avatars");
            RmlHost_SetAvatarDirectory(avatars);
        }
    }
    rml_enabled = Cvar_RegisterBool("r_rmlUi", qtrue, 0, "Enable the optional RmlUi host");
    rml_economy = Cvar_RegisterBool("r_rmlEconomy", qtrue, 0, "Use RmlUi for class/loadout menus, with native fallback");
    rml_broadcast = Cvar_RegisterBool("cg_rmlBroadcast", qtrue, CVAR_ARCHIVE, "Use the passive match and spectator HUD");
    rml_cards = Cvar_RegisterInt("cg_rmlCards",0,0,2,CVAR_ARCHIVE,"Observer cards: 0 expanded, 1 compact, 2 hidden");
    Cmd_AddCommand("rml_cards_toggle", CL_Rml_Cards_f);
    Cmd_AddCommand("rml_show", CL_Rml_Show_f);
    Cmd_AddCommand("rml_palette", CL_Rml_Palette_f);
    Cmd_AddCommand("rml_economy_preview", CL_Rml_EconomyPreview_f);
    Cmd_AddCommand("rml_hide", CL_Rml_Hide);
    Cmd_AddCommand("rml_reload", CL_Rml_Reload_f);
    Cmd_AddCommand("rml_status", CL_Rml_Status_f);
#ifdef COD4X_TEST_ISOLATION
    Cmd_AddCommand("rml_test_click", CL_Rml_TestClick_f);
#endif
}

void CL_Rml_Shutdown(void)
{
    CL_Rml_Hide();
    rml_cursor = NULL;
    R_SyncRenderThread();
    RmlHost_Shutdown();
}

void CL_Rml_Frame(void)
{
    uint32_t ticket;
    int queued;
    /* During a load the render thread also runs the engine's screen update (iw3mp 0x6158A0 -> SCR_UpdateScreen
     * 0x475120 -> Scr_UpdateFrame) while the main thread loads. RML sets cvars, runs menu scripts and drives
     * RmlUi, all of which belong to the main thread. */
    if (!Sys_IsMainThread()) { ++rml_other_thread_frames; return; }
#ifdef COD4X_TEST_ISOLATION
    if (rml_test_x >= 0) {
        if (Sys_Milliseconds() > rml_test_deadline) { rml_test_x=-1; CL_Rml_Log("test click expired waiting for an eligible native economy menu"); }
        else if (Cvar_GetInt("economy_buy_open") && Cvar_GetInt("economy_purchase_allowed")) {
            menuDef_t *root=Menus_FindName(&uiMem.uiInfo.uiDC,"flat_loadout");
            if(root && (root->window.dynamicFlags[0]&4)) {
                CL_Rml_MenuOpened(root);
                if(rml_menus[0].menu && rml_test_x<_vidConfig.displayWidth && rml_test_y>=0 && rml_test_y<_vidConfig.displayHeight) {
                    ticket=RmlHost_Record(_vidConfig.displayWidth,_vidConfig.displayHeight,CL_Rml_Time());
                    if(ticket) RmlHost_Discard(ticket);
                    CL_Rml_MouseEvent(rml_test_x,rml_test_y);
                    CL_Rml_KeyEvent(RML_KEY_MOUSE1+rml_test_button,1);
                    CL_Rml_KeyEvent(RML_KEY_MOUSE1+rml_test_button,0);
                    Com_Printf(CON_CHANNEL_UI,"RmlUi: test click at %d,%d button=%d\n",rml_test_x,rml_test_y,rml_test_button);
                    rml_test_x=-1;
                }
            }
        }
    }
#endif
    if (rml_open_pending) CL_Rml_OpenLocally();
    /* A local open the server does not confirm (team change, menus disabled) must not linger. */
    if (rml_local_open_at && Sys_Milliseconds() - rml_local_open_at > 1500) {
        CL_Rml_Log("server did not open the class menu; closing the local screen");
        CL_Rml_Hide();
    }
    CL_Rml_RunResponse();
    if (!rml_captured && rml_enabled && rml_enabled->boolean && rml_economy->boolean && IN_AppActive() && uiMem.uiInfo.uiDC.openMenuCount) {
        UiContext_t *dc = &uiMem.uiInfo.uiDC;
        menuDef_t *top = dc->menuStack[dc->openMenuCount-1];
        if (top && top->window.name && !Q_strncmp(top->window.name,"flat_",5)) {
            menuDef_t *root=Menus_FindName(dc,"flat_loadout");
            if (root && (root->window.dynamicFlags[dc->localClientNum]&4)) {
                CL_Rml_MenuOpened(root);
                if (top!=root && rml_menus[0].menu) CL_Rml_MenuOpened(top);
            }
        }
    }
    if (rml_menus[0].menu && !rml_economy->boolean) CL_Rml_Hide();
    {
        const int draw_hud=clientUIActives.state == CA_ACTIVE && rml_enabled && rml_enabled->boolean && rml_broadcast->boolean &&
            Cvar_GetInt("ui_rml_hud") && Cvar_GetInt("cg_draw2D") && !cg.inKillCam && !uiMem.uiInfo.uiDC.openMenuCount && !(clientUIActives.keyCatchers & KEYCATCH_CONSOLE);
        if (draw_hud) CL_Rml_EnsureHost();
        /* Team scores arrive when they change; after joining or a map load, ask once for the current ones. */
        if (draw_hud && cl.serverId != rml_scores_server_id) { rml_scores_server_id = cl.serverId; CL_AddReliableCommand("score"); }
        RmlHost_EnableBroadcast(draw_hud);
    }
    if (!rml_enabled || !rml_enabled->boolean || (!RmlHost_Visible() && !Cvar_GetInt("ui_rml_hud"))) {
        Cvar_SetStringByName("ui_rml_broadcast_drawn","0"); rml_follow_drawn = rml_hud_drawn = qfalse;
        if (rml_captured) CL_Rml_Hide();
        return;
    }
    ticket = RmlHost_Record(_vidConfig.displayWidth, _vidConfig.displayHeight, Sys_Milliseconds() / 1000.0);
    queued=ticket && R_QueueRmlFrame(ticket);
    if (queued && rml_first_frame_pending) {
        rml_first_frame_pending = qfalse;
        if (Cvar_GetInt("developer")) Com_Printf(CON_CHANNEL_UI, "RmlUi: first frame %d ms after the buy key\n", Sys_Milliseconds() - rml_buy_key_at);
    }
    Cvar_SetStringByName("ui_rml_broadcast_drawn",queued && RmlHost_BroadcastReady() ? "1" : "0");
    rml_follow_drawn = queued && RmlHost_BroadcastFollowShown();
    rml_hud_drawn = queued && RmlHost_BroadcastReady();
    if (ticket && !queued) RmlHost_Discard(ticket);
    if (ticket && RmlHost_Visible() && IN_AppActive() && !(clientUIActives.keyCatchers & KEYCATCH_CONSOLE) && rml_cursor && !Material_IsDefault(rml_cursor)) {
        const float size = 32.0f * _vidConfig.displayHeight / 480.0f;
        /* ui_cursor's arrow tip is at the centre of its transparent texture. */
        R_AddCmdDrawStretchPic(rml_cursor_x - size * 0.5f, rml_cursor_y - size * 0.5f,
            size, size, 0, 0, 1, 1, colorWhite, rml_cursor);
        CL_ShowSystemCursor(0);
    }
    if (rml_captured && !RmlHost_Visible()) CL_Rml_Hide();
}

int CL_Rml_KeyEvent(int key, int down)
{
    int flag = 0;
    if ((clientUIActives.keyCatchers & KEYCATCH_CONSOLE) || key == '`' || key == '~') return 0;
#ifdef COD4X_TEST_ISOLATION
    if (rml_enabled && key == 173) {
        if (down) CL_Rml_EconomyPreview_f();
        return 1;
    }
    if (rml_enabled && key == RML_KEY_F8) {
        if (down) CL_Rml_Palette_f();
        return 1;
    }
#endif
    if (!IN_AppActive() || !rml_captured || !RmlHost_Visible()) return 0;
    if (key == RML_KEY_F9) {
        if (down) CL_Rml_Reload_f();
        return 1;
    }
    if (key == RML_KEY_CTRL) flag = 1;
    if (key == RML_KEY_SHIFT) flag = 2;
    if (key == RML_KEY_ALT) flag = 4;
    if (down) rml_modifiers |= flag;
    else rml_modifiers &= ~flag;
    RmlHost_Key(key, down, rml_modifiers);
    return 1;
}

int CL_Rml_CharEvent(unsigned int value)
{
    if (!IN_AppActive() || !rml_captured || !RmlHost_Visible() || (clientUIActives.keyCatchers & KEYCATCH_CONSOLE)) return 0;
    RmlHost_Text(value);
    return 1;
}

int CL_Rml_MouseEvent(int x, int y)
{
    if (!IN_AppActive() || !rml_captured || !RmlHost_Visible() || (clientUIActives.keyCatchers & KEYCATCH_CONSOLE)) return 0;
    RmlHost_MouseMove(x, y);
    rml_cursor_x = x; rml_cursor_y = y;
    CL_ShowSystemCursor(!IN_AppActive() || !rml_cursor || Material_IsDefault(rml_cursor));
    return 1;
}
