#ifndef CL_TICKRATE_CORE_H
#define CL_TICKRATE_CORE_H

#include "client.h"
#include "cg_shared.h"
#include <stdint.h>

/* Engine-free part of the high-rate client. Everything here is exercised by
 * tests/test_cl_tickrate.c and, through the tickrate_replay DLL, by
 * tests/replay_client_tickrate.py against the unmodified 1.7 decoder. Keep the
 * exported signatures and sizeof(tickrate_frame_t) stable for that harness. */

enum {
    TICKRATE_PROTOCOL = 1,
    TICKRATE_HISTORY = 256,     /* retained delta bases; advertised as cl_deltaSlots */
    TICKRATE_MIN_MSEC = 8,      /* fastest compatible server frame (125 Hz) */
    TICKRATE_STOCK_MSEC = 50,   /* stock server frame; deadline pacing applies below it */
    TICKRATE_MAX_RATE = 1000 / TICKRATE_MIN_MSEC,
    PARSE_RING = 2048           /* stock parseEntities/parseClients ring length */
};

/* Keep the executable's fixed clientActive layout. The native parser uses its
 * original rings as scratch; each retained delta base owns a complete copy. */
typedef struct {
    clSnapshot_t snap;
    entityState_t entities[MAX_GENTITIES];
    clientState_t clients[MAX_CLIENTS];
} tickrate_frame_t;

/* Mirror of cl.outPackets deep enough for 100 pps at any playable ping. */
typedef struct {
    outPacket_t packet;
    int sequence;
    qboolean valid;
} tickrate_sent_packet_t;

typedef struct {
    uint64_t next;
    uint32_t lastTime;
    int rate;
} tickrate_packet_clock_t;

void Tickrate_ClearHistory(tickrate_frame_t *frames);
qboolean Tickrate_StageDelta(tickrate_frame_t *frames, clientActive_t *active, int current, int delta);
qboolean Tickrate_SaveSnapshot(tickrate_frame_t *frames, const clientActive_t *active);
qboolean Tickrate_GetSnapshot(const tickrate_frame_t *frames, int current, int number, snapshot_t *out);
void Tickrate_PacketSent(tickrate_sent_packet_t *sent, int sequence, const outPacket_t *packet);
int Tickrate_Ping(const tickrate_sent_packet_t *sent, int outgoingSequence, int commandTime, int realtime);
qboolean Tickrate_PacketDue(tickrate_packet_clock_t *clock, uint32_t now, int rate);
int Tickrate_ParseFrameMsec(const char *protocol, const char *milliseconds);

#endif
