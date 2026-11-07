#include "q_shared.h"
#include "qcommon.h"
#include "client.h"
#include "cl_input.h"
#include "r_shared.h"
#include "ui_shared.h"
#include "snd_system.h"
#include "keys.h"
#include "cl_rml.h"
#include "rml/rml_host.h"
#include "rml/native_keys.h"
#include "win_sys.h"
#include <stdlib.h>

/* The native mouse route uses bit 0x10 for modal menus. */
#define RML_MENU_CATCHER 0x10
static cvar_t *rml_enabled;
static cvar_t *rml_economy;
static qboolean rml_captured;
static int rml_previous_catcher, rml_modifiers;
static struct { menuDef_t *menu; int item_count; } rml_menus[4];
static char rml_response[96];
static int rml_retry_at;
static Material *rml_cursor;
static int rml_cursor_x, rml_cursor_y;
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
    if (!rml_menus[0].menu) { CL_Rml_Hide(); return; }
    RmlHost_ClearInput();
    rml_modifiers = 0;
    CL_ShowSystemCursor(1);
}

static qboolean CL_Rml_Show(rml_screen_t screen)
{
    POINT cursor;
    const rml_host_t host = {FS_ReadFile, FS_FreeFile, CL_Rml_Log, CL_Rml_Sound, CL_Rml_Time, Cvar_GetString, CL_Rml_Response};
    if (!RmlHost_Init(&host) || !RmlHost_Open(screen)) {
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
    CL_ShowSystemCursor(!rml_cursor || Material_IsDefault(rml_cursor));
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
    if (!rml_menus[0].menu || rml_response[0] || !RmlHost_ActionAllowed(response)) return 0;
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

void CL_Rml_MenuOpened(menuDef_t *menu)
{
    static const char *names[] = {"flat_loadout", "flat_primary_picker", "flat_secondary_picker", "flat_demolition_confirm"};
    int i;
    if (!rml_economy || !rml_economy->boolean || !rml_enabled->boolean || (!Cvar_GetInt("flat_economy_live") && !(Cvar_GetInt("flat_loadout_ready") && !Q_stricmp(Cvar_GetString("flat_loadout_mode"), "standard"))) || Sys_Milliseconds() < rml_retry_at) return;
    for (i = 0; i < 4; ++i) if (!Q_stricmp(menu->window.name, names[i])) break;
    if (i == 4 || (i && !rml_menus[0].menu)) return;
    if (!i) { RmlHost_EconomyDialog(0); if (!CL_Rml_Show(RML_SCREEN_ECONOMY)) { rml_retry_at=Sys_Milliseconds()+1000; return; } rml_retry_at=0; }
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
    if (!rml_response[0]) return;
    Q_strncpyz(response, rml_response, sizeof(response)); rml_response[0] = 0;
    if (!rml_menus[0].menu) return;
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
    rml_enabled = Cvar_RegisterBool("r_rmlUi", qtrue, 0, "Enable the optional RmlUi host");
    rml_economy = Cvar_RegisterBool("r_rmlEconomy", qtrue, 0, "Use RmlUi for class/loadout menus, with native fallback");
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
    if (!rml_enabled || !rml_enabled->boolean || !RmlHost_Visible()) {
        if (rml_captured) CL_Rml_Hide();
        return;
    }
    ticket = RmlHost_Record(_vidConfig.displayWidth, _vidConfig.displayHeight, Sys_Milliseconds() / 1000.0);
    if (ticket && !R_QueueRmlFrame(ticket)) RmlHost_Discard(ticket);
    if (ticket && IN_AppActive() && !(clientUIActives.keyCatchers & KEYCATCH_CONSOLE) && rml_cursor && !Material_IsDefault(rml_cursor)) {
        const float size = 32.0f * _vidConfig.displayHeight / 480.0f;
        /* ui_cursor's arrow tip is at the centre of its transparent texture. */
        R_AddCmdDrawStretchPic(rml_cursor_x - size * 0.5f, rml_cursor_y - size * 0.5f,
            size, size, 0, 0, 1, 1, colorWhite, rml_cursor);
        CL_ShowSystemCursor(0);
    }
    if (!RmlHost_Visible()) CL_Rml_Hide();
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
    CL_ShowSystemCursor(!rml_cursor || Material_IsDefault(rml_cursor));
    return 1;
}
