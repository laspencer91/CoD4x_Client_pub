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

/* The native mouse route uses bit 0x10 for modal menus. */
#define RML_MENU_CATCHER 0x10
static cvar_t *rml_enabled;
static qboolean rml_captured;
static int rml_previous_catcher, rml_modifiers;

static void CL_Rml_Log(const char *message) { Com_Printf(CON_CHANNEL_UI, "RmlUi: %s\n", message); }
static void CL_Rml_Sound(const char *alias) { SND_PlayLocalSoundAliasByName(0, alias); }

void CL_Rml_Hide(void)
{
    RmlHost_Hide();
    if (rml_captured) {
        Key_ClearStates(0);
        Key_SetCatcher((clientUIActives.keyCatchers & ~RML_MENU_CATCHER) | rml_previous_catcher);
        rml_captured = qfalse;
        rml_modifiers = 0;
        CL_ShowSystemCursor(0);
    }
}

static void CL_Rml_Show_f(void)
{
    const rml_host_t host = {FS_ReadFile, FS_FreeFile, CL_Rml_Log, CL_Rml_Sound};
    if (!RmlHost_Init(&host) || !RmlHost_Show()) {
        CL_Rml_Hide();
        CL_Rml_Log("diagnostic load failed; native UI remains available");
        return;
    }
    Cvar_SetBool(rml_enabled, qtrue);
    if (!rml_captured) {
        Key_ClearStates(0);
        rml_previous_catcher = clientUIActives.keyCatchers & RML_MENU_CATCHER;
        Key_SetCatcher(clientUIActives.keyCatchers | RML_MENU_CATCHER);
        rml_captured = qtrue;
    }
    Com_Printf(CON_CHANNEL_UI, "RmlUi: diagnostic opened\n");
}

static void CL_Rml_Reload_f(void)
{
    if (!RmlHost_Reload()) { CL_Rml_Hide(); CL_Rml_Log("reload failed; input capture released"); }
}

static void CL_Rml_Status_f(void)
{
    rml_stats_t stats;
    RmlHost_Stats(&stats);
    Com_Printf(CON_CHANNEL_DONT_FILTER,
        "RmlUi: visible=%d recorded=%u rendered=%u draws=%u uploads=%u/%u pending=%u result=0x%08x\n",
        stats.visible, stats.recorded_frames, stats.rendered_frames, stats.draw_calls,
        stats.geometry_uploads, stats.texture_uploads, stats.pending_frames, (unsigned)stats.last_render_result);
}

void CL_Rml_Init(void)
{
    rml_enabled = Cvar_RegisterBool("r_rmlUi", qfalse, 0, "Enable the optional RmlUi diagnostic host");
    Cmd_AddCommand("rml_show", CL_Rml_Show_f);
    Cmd_AddCommand("rml_hide", CL_Rml_Hide);
    Cmd_AddCommand("rml_reload", CL_Rml_Reload_f);
    Cmd_AddCommand("rml_status", CL_Rml_Status_f);
}

void CL_Rml_Shutdown(void)
{
    CL_Rml_Hide();
    R_SyncRenderThread();
    RmlHost_Shutdown();
}

void CL_Rml_Frame(void)
{
    uint32_t ticket;
    if (!rml_enabled || !rml_enabled->boolean || !IN_AppActive() || !RmlHost_Visible()) {
        if (rml_captured) CL_Rml_Hide();
        return;
    }
    ticket = RmlHost_Record(_vidConfig.displayWidth, _vidConfig.displayHeight, Sys_Milliseconds() / 1000.0);
    if (ticket && !R_QueueRmlFrame(ticket)) RmlHost_Discard(ticket);
    if (!RmlHost_Visible()) CL_Rml_Hide();
}

int CL_Rml_KeyEvent(int key, int down)
{
    int flag = 0;
    if (!rml_captured || !RmlHost_Visible() || (clientUIActives.keyCatchers & KEYCATCH_CONSOLE) || key == '`' || key == '~') return 0;
    if (key == K_CTRL) flag = 1;
    if (key == K_SHIFT) flag = 2;
    if (key == K_ALT) flag = 4;
    if (down) rml_modifiers |= flag;
    else rml_modifiers &= ~flag;
    RmlHost_Key(key, down, rml_modifiers);
    return 1;
}

int CL_Rml_CharEvent(unsigned int value)
{
    if (!rml_captured || !RmlHost_Visible() || (clientUIActives.keyCatchers & KEYCATCH_CONSOLE)) return 0;
    RmlHost_Text(value);
    return 1;
}

int CL_Rml_MouseEvent(int x, int y)
{
    if (!rml_captured || !RmlHost_Visible() || (clientUIActives.keyCatchers & KEYCATCH_CONSOLE)) return 0;
    RmlHost_MouseMove(x, y);
    CL_ShowSystemCursor(1);
    return 1;
}
