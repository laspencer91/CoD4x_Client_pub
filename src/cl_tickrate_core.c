#include "cl_tickrate_core.h"
#include <stddef.h>
#include <string.h>

/* The CL_GetSnapshot hook and the staging below address clientActive_t through
 * the stock executable's offsets; the DLL build must fail if the layout drifts. */
_Static_assert(offsetof(clientActive_t, snapshots) == 0x69850, "clientActive_t.snapshots");
_Static_assert(offsetof(clientActive_t, parseEntities) == 0x105ad0, "clientActive_t.parseEntities");
_Static_assert(offsetof(clientActive_t, parseClients) == 0x17fad0, "clientActive_t.parseClients");
_Static_assert(sizeof(clientActive_t) == 0x1b1bdc, "clientActive_t size");
_Static_assert(ARRAY_LEN(((clientActive_t *)0)->parseEntities) == PARSE_RING, "parseEntities ring");
_Static_assert(ARRAY_LEN(((clientActive_t *)0)->parseClients) == PARSE_RING, "parseClients ring");
_Static_assert(sizeof(tickrate_frame_t) == 268436, "tickrate_frame_t size pinned by replay_client_tickrate.py");

static const tickrate_frame_t *find_frame(const tickrate_frame_t *frames, int current, int number)
{
    const tickrate_frame_t *frame;
    if ((uint32_t)current - (uint32_t)number >= TICKRATE_HISTORY) return NULL;
    frame = &frames[(uint32_t)number & (TICKRATE_HISTORY - 1)];
    return (frame->snap.valid && frame->snap.messageNum == number) ? frame : NULL;
}

/* A run of count elements in a PARSE_RING ring wraps at most once, so it is
 * at most two memcpy spans in either direction. */
static void ring_write(void *ring, size_t size, uint32_t start, const void *src, int count)
{
    uint32_t first = start & (PARSE_RING - 1);
    int head = (uint32_t)count < PARSE_RING - first ? count : (int)(PARSE_RING - first);
    memcpy((byte *)ring + first * size, src, head * size);
    memcpy(ring, (const byte *)src + head * size, (count - head) * size);
}

static void ring_read(void *dst, const void *ring, size_t size, uint32_t start, int count)
{
    uint32_t first = start & (PARSE_RING - 1);
    int head = (uint32_t)count < PARSE_RING - first ? count : (int)(PARSE_RING - first);
    memcpy(dst, (const byte *)ring + first * size, head * size);
    memcpy((byte *)dst + head * size, ring, (count - head) * size);
}

void Tickrate_ClearHistory(tickrate_frame_t *frames)
{
    int i;
    for (i = 0; i < TICKRATE_HISTORY; ++i) frames[i].snap.valid = qfalse;
}

qboolean Tickrate_StageDelta(tickrate_frame_t *frames, clientActive_t *active, int current, int delta)
{
    const tickrate_frame_t *frame = find_frame(frames, current, delta);
    clSnapshot_t *native = &active->snapshots[(uint32_t)delta & PACKET_MASK];
    if (!frame) {
        native->valid = qfalse;
        return qfalse;
    }

    *native = frame->snap;
    /* Rebase both streams immediately before the native write cursor. This
     * satisfies its age guards without changing counters, masks or pointers.
     * At most 1024 old + 1024 new entities occupy the 2048-entry scratch ring. */
    native->parseEntitiesNum = (int)((uint32_t)active->parseEntitiesNum - (uint32_t)native->numEntities);
    ring_write(active->parseEntities, sizeof(active->parseEntities[0]),
        (uint32_t)native->parseEntitiesNum, frame->entities, native->numEntities);
    native->parseClientsNum = (int)((uint32_t)active->parseClientsNum - (uint32_t)native->numClients);
    ring_write(active->parseClients, sizeof(active->parseClients[0]),
        (uint32_t)native->parseClientsNum, frame->clients, native->numClients);
    return qtrue;
}

qboolean Tickrate_SaveSnapshot(tickrate_frame_t *frames, const clientActive_t *active)
{
    const clSnapshot_t *snap = &active->snap;
    tickrate_frame_t *frame;
    /* Bounds guard the frame arrays when the harness drives this with arbitrary
     * memory; after a successful native parse they always hold. */
    if (!snap->valid || snap->numEntities < 0 || snap->numEntities > MAX_GENTITIES ||
        snap->numClients < 0 || snap->numClients > MAX_CLIENTS) return qfalse;
    frame = &frames[(uint32_t)snap->messageNum & (TICKRATE_HISTORY - 1)];
    frame->snap = *snap;
    ring_read(frame->entities, active->parseEntities, sizeof(active->parseEntities[0]),
        (uint32_t)snap->parseEntitiesNum, snap->numEntities);
    ring_read(frame->clients, active->parseClients, sizeof(active->parseClients[0]),
        (uint32_t)snap->parseClientsNum, snap->numClients);
    return qtrue;
}

qboolean Tickrate_GetSnapshot(const tickrate_frame_t *frames, int current, int number, snapshot_t *out)
{
    const tickrate_frame_t *frame = find_frame(frames, current, number);
    const int maxEntities = ARRAY_LEN(out->entities);
    if (!frame) return qfalse;
    out->snapFlags = frame->snap.snapFlags;
    out->ping = frame->snap.ping;
    out->serverTime = frame->snap.serverTime;
    out->ps = frame->snap.ps;
    out->serverCommandNum = frame->snap.serverCommandNum;
    /* Native CL_GetSnapshot also truncates to cgame's 512-entity snapshot. */
    out->numEntities = frame->snap.numEntities > maxEntities ? maxEntities : frame->snap.numEntities;
    out->numClients = frame->snap.numClients;
    memcpy(out->entities, frame->entities, out->numEntities * sizeof(out->entities[0]));
    memcpy(out->clients, frame->clients, out->numClients * sizeof(out->clients[0]));
    return qtrue;
}

void Tickrate_PacketSent(tickrate_sent_packet_t *sent, int sequence, const outPacket_t *packet)
{
    tickrate_sent_packet_t *slot = &sent[(uint32_t)sequence & (TICKRATE_HISTORY - 1)];
    slot->packet = *packet;
    slot->sequence = sequence;
    slot->valid = qtrue;
}

/* Same search as the native parser's over cl.outPackets, newest first, but
 * TICKRATE_HISTORY deep. The sequence tag skips slots whose sequence was
 * consumed by a transmit that did not go through CL_WritePacket. Returns -1
 * when no sent packet precedes the acknowledged command. */
int Tickrate_Ping(const tickrate_sent_packet_t *sent, int outgoingSequence, int commandTime, int realtime)
{
    int i;
    for (i = 0; i < TICKRATE_HISTORY; ++i) {
        const int number = (int)((uint32_t)outgoingSequence - 1 - (uint32_t)i);
        const tickrate_sent_packet_t *slot = &sent[(uint32_t)number & (TICKRATE_HISTORY - 1)];
        if (slot->valid && slot->sequence == number && slot->packet.p_serverTime <= commandTime)
            return realtime - slot->packet.p_realtime;
    }
    return -1;
}

qboolean Tickrate_PacketDue(tickrate_packet_clock_t *clock, uint32_t now, int rate)
{
    uint64_t scaled;
    if (rate < 1 || rate > TICKRATE_MAX_RATE) return qfalse;
    scaled = (uint64_t)now * (uint32_t)rate;
    if (clock->rate != rate || now < clock->lastTime || now - clock->lastTime > 1000) {
        clock->rate = rate;
        clock->next = scaled;
    }
    clock->lastTime = now;
    if (scaled < clock->next) return qfalse;
    clock->next += 1000;
    /* Drop missed slots after a hitch; never emit a catch-up packet burst. */
    if (clock->next <= scaled) clock->next = scaled + 1000;
    return qtrue;
}

/* Strict unsigned decimal in 0..limit; -1 for empty, non-digit or too large. */
static int parse_bounded(const char *text, int limit)
{
    int value = 0;
    if (!text || !*text) return -1;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return -1;
        value = value * 10 + (*text - '0');
        if (value > limit) return -1;
    }
    return value;
}

int Tickrate_ParseFrameMsec(const char *protocol, const char *milliseconds)
{
    const int value = parse_bounded(milliseconds, TICKRATE_STOCK_MSEC);
    if (parse_bounded(protocol, TICKRATE_PROTOCOL) != TICKRATE_PROTOCOL) return 0;
    return (value >= TICKRATE_MIN_MSEC && 1000 % value == 0) ? value : 0;
}
