#include "cl_tickrate.h"
#include "cl_tickrate_core.h"
#include "sys_patch.h"
#include <windows.h>
#include <string.h>

/* Native CL_GetSnapshot: `mov eax, [cl.snap.messageNum]`, replaced whole by a jmp. */
#define CL_GETSNAPSHOT_ENTRY 0x45ab90
static const byte getSnapshotPrologue[] = { 0xa1, 0x44, 0xf9, 0xc5, 0x00 };

static tickrate_frame_t *history;          /* non-NULL once the hook is installed */
static const char *patchFailure;           /* why not, printed once the console exists */
static qboolean patchAttempted;
static tickrate_sent_packet_t sent[TICKRATE_HISTORY];
static tickrate_packet_clock_t packetClock;
static int activeMsec;                     /* compatible server frame, 0 on ordinary servers */
static cvar_t *activeMsecDvar;
static cvar_t *snapsDvar;
static struct { int snapshots, packets, maxDelta, invalidDeltas, fullSnapshots; } stats;

static qboolean CL_Tickrate_GetSnapshot(int number, snapshot_t *out)
{
    /* Same contract as native: a future number is a cgame bug and drops. */
    if (number > cl.snap.messageNum)
        Com_Error(ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl->snapshot.messageNum");
    return Tickrate_GetSnapshot(history, cl.snap.messageNum, number, out);
}

/* Runs inside Patch_MainModule's writable window, before Com_Init. */
void CL_Tickrate_Patch(void)
{
    patchAttempted = qtrue;
    if (history) return;
    if (memcmp((const void *)CL_GETSNAPSHOT_ENTRY, getSnapshotPrologue, sizeof(getSnapshotPrologue))) {
        patchFailure = "snapshot reader signature mismatch";
        return;
    }
    history = VirtualAlloc(NULL, sizeof(*history) * TICKRATE_HISTORY, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!history) {
        patchFailure = "history allocation failed";
        return;
    }
    SetJump(CL_GETSNAPSHOT_ENTRY, CL_Tickrate_GetSnapshot);
}

static void SetActiveMsec(int msec)
{
    activeMsec = msec;
    Cvar_SetInt(activeMsecDvar, msec);
}

static void CL_Tickrate_Status_f(void)
{
    const int rate = activeMsec ? 1000 / activeMsec : 0;
    Com_Printf(CON_CHANNEL_CLIENT,
        "TICKRATE_CLIENT_STATUS installed=%d protocol=%d slots=%d active_ms=%d rate=%d snapshots=%d message=%d server_time=%d packets=%d outgoing=%d max_delta=%d invalid_delta=%d full=%d snaps=%d maxpackets=%d maxfps=%d history_bytes=%u failure=%s\n",
        history != NULL, history ? TICKRATE_PROTOCOL : 0,
        history ? TICKRATE_HISTORY : PACKET_BACKUP, activeMsec,
        cl_maxpackets->integer > rate ? cl_maxpackets->integer : rate,
        stats.snapshots, cl.snap.messageNum, cl.snap.serverTime, stats.packets, clc.netchan.outgoingSequence,
        stats.maxDelta, stats.invalidDeltas, stats.fullSnapshots, snapsDvar->integer,
        cl_maxpackets->integer, com_maxfps ? com_maxfps->integer : 0,
        history ? (unsigned)(sizeof(*history) * TICKRATE_HISTORY) : 0,
        patchFailure ? patchFailure : "none");
}

/* ROM so neither config nor console can claim a capability the hook lacks. */
static void RegisterCapability(const char *name, int value, int min, int max, const char *description)
{
    cvar_t *capability = Cvar_RegisterInt(name, value, min, max, CVAR_ROM | CVAR_USERINFO, description);
    Cvar_SetInt(capability, value);
}

/* Called from CL_InitOnceForAllClients, right after "snaps" and cl_maxpackets
 * are registered and after Patch_Other has run. */
void CL_Tickrate_Init(void)
{
    if (!patchAttempted) Com_Error(ERR_FATAL, "CL_Tickrate_Init before CL_Tickrate_Patch");
    if (patchFailure)
        Com_PrintWarning(CON_CHANNEL_CLIENT, "Tickrate: %s; retaining %d-slot capability\n", patchFailure, PACKET_BACKUP);
    RegisterCapability("cl_tickrateVersion", history ? TICKRATE_PROTOCOL : 0, 0, TICKRATE_PROTOCOL,
        "Installed high-rate client protocol");
    RegisterCapability("cl_deltaSlots", history ? TICKRATE_HISTORY : PACKET_BACKUP, PACKET_BACKUP, TICKRATE_HISTORY,
        "Installed snapshot decoder history capacity");
    activeMsecDvar = Cvar_RegisterInt("cl_tickrate_active_ms", 0, 0, TICKRATE_STOCK_MSEC, CVAR_ROM,
        "Active compatible server frame duration (zero: ordinary server)");
    snapsDvar = Cvar_FindVar("snaps");
    Cmd_AddCommand("tickrate_status", CL_Tickrate_Status_f);
}

void CL_Tickrate_Clear(void)
{
    if (!activeMsecDvar) return; /* dedicated: CL_Tickrate_Init never ran */
    if (history) Tickrate_ClearHistory(history);
    memset(sent, 0, sizeof(sent));
    memset(&packetClock, 0, sizeof(packetClock));
    memset(&stats, 0, sizeof(stats));
    SetActiveMsec(0);
}

void CL_Tickrate_SystemInfoChanged(const char *systemInfo)
{
    /* Info_ValueForKey alternates two static buffers, so two calls may be nested. */
    SetActiveMsec(history ? Tickrate_ParseFrameMsec(Info_ValueForKey(systemInfo, "sv_tickrate_protocol"),
                                                    Info_ValueForKey(systemInfo, "sv_tickrate_active_ms")) : 0);
    /* snaps is only a server preference, not a local receive-rate limit. The
     * matching server enforces its minimum without rewriting archived prefs. */
}

void CL_Tickrate_PacketSent(int sequence, const outPacket_t *packet)
{
    Tickrate_PacketSent(sent, sequence, packet);
    ++stats.packets;
}

/* Deadline pacing applies only when the server frame is shorter than stock's;
 * at or above TICKRATE_STOCK_MSEC the stock spacing rule stays in charge. */
int CL_Tickrate_PacketDue(int now, int requestedRate)
{
    int floor;
    if (!activeMsec || activeMsec >= TICKRATE_STOCK_MSEC) return -1;
    floor = 1000 / activeMsec;
    return Tickrate_PacketDue(&packetClock, (uint32_t)now, requestedRate > floor ? requestedRate : floor);
}

void CL_Tickrate_ParseSnapshot(msg_t *msg)
{
    msg_t peek;
    int delta, sequence, ping;
    if (!history) {
        CL_ParseSnapshot(msg);
        return;
    }
    /* The native parser (0x473710) reads serverTime:long then deltaNum:byte and
     * immediately indexes snapshots[(messageNum - deltaNum) & 31] and checks the
     * base's ring age, so the base must be staged before it runs. Only this
     * header is re-read; the body is decoded by native code alone. */
    peek = *msg;
    MSG_ReadLong(&peek);
    delta = MSG_ReadByte(&peek);               /* -1 on overflow */
    sequence = clc.serverMessageSequence;
    if (delta > 0 && sequence > delta && !Tickrate_StageDelta(history, &cl, sequence, sequence - delta))
        ++stats.invalidDeltas;

    CL_ParseSnapshot(msg);
    if (msg->overflowed || !cl.snap.valid || cl.snap.messageNum != sequence) return;
    if (delta < 0 || cl.snap.deltaNum != (delta ? (int)((uint32_t)sequence - (uint32_t)delta) : -1)) {
        Com_Error(ERR_DROP, "Tickrate: native snapshot header disagrees with staged delta base");
        return;
    }
    if (delta > stats.maxDelta) stats.maxDelta = delta;
    if (!delta) ++stats.fullSnapshots;
    /* Native searched only 32 sent packets; at 100 pps that is 320 ms of ping. */
    ping = Tickrate_Ping(sent, clc.netchan.outgoingSequence, cl.snap.ps.commandTime, cls.realtime);
    if (ping >= 0) cl.snap.ping = ping;
    if (!Tickrate_SaveSnapshot(history, &cl))
        Com_Error(ERR_DROP, "Tickrate: parsed snapshot has inconsistent entity/client counts");
    ++stats.snapshots;
}
