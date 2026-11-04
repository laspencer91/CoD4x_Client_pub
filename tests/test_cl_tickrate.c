#include "cl_tickrate_core.h"
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static void test_history(void)
{
    tickrate_frame_t *frames = calloc(TICKRATE_HISTORY, sizeof(*frames));
    clientActive_t *active = calloc(1, sizeof(*active));
    snapshot_t *out = calloc(1, sizeof(*out));
    const unsigned ages[] = {1, 31, 32, 33, 127, 128, 129, 254, 255};
    unsigned n;
    int i;
    CHECK(frames && active && out);
    if (!frames || !active || !out) exit(2);

    /* The binary reader hook must never change these stock offsets. */
    CHECK(offsetof(clientActive_t, snapshots) == 0x69850);
    CHECK(offsetof(clientActive_t, parseEntities) == 0x105ad0);
    CHECK(offsetof(clientActive_t, parseClients) == 0x17fad0);
    CHECK(sizeof(clientActive_t) == 0x1b1bdc);

    active->snap.valid = 1;
    active->snap.messageNum = 300;
    active->snap.serverTime = 3000;
    active->snap.serverCommandNum = 23;
    active->snap.numEntities = MAX_GENTITIES;
    active->snap.numClients = MAX_CLIENTS;
    active->snap.parseEntitiesNum = 2000;
    active->snap.parseClientsNum = 2040;
    active->snap.ps.commandTime = 2975;
    for (i = 0; i < MAX_GENTITIES; ++i) {
        entityState_t *entity = &active->parseEntities[(2000 + i) & 2047];
        entity->number = i;
        entity->eventSequence = i * 3;
        entity->lerp.pos.trBase[0] = (float)i + .25f;
    }
    for (i = 0; i < MAX_CLIENTS; ++i) {
        clientState_t *client = &active->parseClients[(2040 + i) & 2047];
        client->clientIndex = i;
        client->modelindex = i * 11;
    }
    CHECK(Tickrate_SaveSnapshot(frames, active));

    /* The native pools have wrapped repeatedly since the old base. Restore
     * it exactly, including entities beyond cgame's 512-entity output cap. */
    memset(active->parseEntities, 0xcc, sizeof(active->parseEntities));
    memset(active->parseClients, 0xdd, sizeof(active->parseClients));
    active->parseEntitiesNum = 90001;
    active->parseClientsNum = 17003;
    for (n = 0; n < ARRAY_LEN(ages); ++n) {
        clSnapshot_t *native;
        CHECK(Tickrate_StageDelta(frames, active, 300 + ages[n], 300));
        native = &active->snapshots[300 & 31];
        CHECK(native->messageNum == 300);
        CHECK(active->parseEntitiesNum - native->parseEntitiesNum == MAX_GENTITIES);
        CHECK(active->parseClientsNum - native->parseClientsNum == MAX_CLIENTS);
        for (i = 0; i < MAX_GENTITIES; ++i) {
            const entityState_t *entity = &active->parseEntities[((uint32_t)native->parseEntitiesNum + i) & 2047];
            CHECK(entity->number == i && entity->eventSequence == i * 3);
            CHECK(entity->lerp.pos.trBase[0] == (float)i + .25f);
        }
        for (i = 0; i < MAX_CLIENTS; ++i)
            CHECK(active->parseClients[((uint32_t)native->parseClientsNum + i) & 2047].modelindex == i * 11);
        CHECK(Tickrate_GetSnapshot(frames, 300 + ages[n], 300, out));
        CHECK(out->numEntities == 512 && out->numClients == MAX_CLIENTS);
        CHECK(out->entities[511].number == 511 && out->clients[63].modelindex == 693);
        CHECK(out->ps.commandTime == 2975 && out->serverCommandNum == 23);
    }
    CHECK(!Tickrate_StageDelta(frames, active, 556, 300));
    CHECK(!active->snapshots[300 & 31].valid);
    CHECK(!Tickrate_GetSnapshot(frames, 556, 300, out));
    CHECK(!Tickrate_GetSnapshot(frames, 299, 300, out));
    CHECK(!Tickrate_GetSnapshot(frames, 301, 299, out));

    /* Removal and roster changes replace only this snapshot's copies. */
    active->snap.messageNum = 301;
    active->snap.snapFlags ^= SNAPFLAG_SERVERCOUNT; /* fast restart, same message stream */
    active->snap.numEntities = 0;
    active->snap.numClients = 0;
    CHECK(Tickrate_SaveSnapshot(frames, active));
    CHECK(Tickrate_GetSnapshot(frames, 301, 301, out) && out->numEntities == 0 && out->numClients == 0);
    CHECK(Tickrate_GetSnapshot(frames, 301, 300, out) && out->numEntities == 512 && out->numClients == 64);
    CHECK(out->snapFlags == 0); /* pre-restart bases remain available to the decoder */
    CHECK(Tickrate_GetSnapshot(frames, 301, 301, out) && out->snapFlags == SNAPFLAG_SERVERCOUNT);
    CHECK(Tickrate_StageDelta(frames, active, 302, 300));
    active->snap.numEntities = MAX_GENTITIES + 1;
    CHECK(!Tickrate_SaveSnapshot(frames, active));
    active->snap.numEntities = 0;
    active->snap.numClients = MAX_CLIENTS + 1;
    CHECK(!Tickrate_SaveSnapshot(frames, active));
    active->snap.numClients = 0;
    active->snap.valid = 0;
    CHECK(!Tickrate_SaveSnapshot(frames, active));

    /* Unsigned sequence distance also works across a 32-bit wrap. */
    active->snap.valid = 1;
    active->snap.messageNum = -2;
    CHECK(Tickrate_SaveSnapshot(frames, active));
    CHECK(Tickrate_GetSnapshot(frames, 3, -2, out));
    CHECK(Tickrate_StageDelta(frames, active, 3, -2));
    Tickrate_ClearHistory(frames);
    CHECK(!Tickrate_GetSnapshot(frames, 3, -2, out));
    CHECK(!Tickrate_StageDelta(frames, active, 301, 300));
    free(out);
    free(active);
    free(frames);
}

static void test_packet_clock(void)
{
    const int fps[] = {30, 60, 100, 125, 144, 240, 333};
    unsigned f;
    for (f = 0; f < ARRAY_LEN(fps); ++f) {
        tickrate_packet_clock_t clock = {0};
        int frame, sent = 0, previous = -1;
        for (frame = 0; frame < fps[f] * 10; ++frame) {
            const int now = frame * 1000 / fps[f];
            int caller;
            /* Native and source send paths may both poll during one frame. */
            for (caller = 0; caller < 2; ++caller) {
                if (Tickrate_PacketDue(&clock, now, 100)) {
                    CHECK(now != previous);
                    previous = now;
                    ++sent;
                }
            }
        }
        CHECK(abs(sent - (fps[f] < 100 ? fps[f] * 10 : 1000)) <= 1);
    }
    {
        tickrate_packet_clock_t clock = {0};
        CHECK(Tickrate_PacketDue(&clock, 100, 100));
        CHECK(!Tickrate_PacketDue(&clock, 100, 100));
        CHECK(!Tickrate_PacketDue(&clock, 108, 100));
        CHECK(Tickrate_PacketDue(&clock, 116, 100));
        CHECK(Tickrate_PacketDue(&clock, 124, 100));
        CHECK(Tickrate_PacketDue(&clock, 5000, 100));
        CHECK(!Tickrate_PacketDue(&clock, 5000, 100));
        CHECK(!Tickrate_PacketDue(&clock, 5001, 100));
        CHECK(Tickrate_PacketDue(&clock, 10, 100)); /* map/clock reset */
        CHECK(Tickrate_PacketDue(&clock, 10, 125)); /* changed preference */
        CHECK(!Tickrate_PacketDue(&clock, 11, 0));
        CHECK(!Tickrate_PacketDue(&clock, 11, 126));
        CHECK(Tickrate_PacketDue(&clock, UINT_MAX - 1, 100));
        CHECK(Tickrate_PacketDue(&clock, 3, 100));
    }
}

static void test_ping(void)
{
    tickrate_sent_packet_t sent[TICKRATE_HISTORY];
    outPacket_t packet = {0, 0, 0};
    int sequence;
    memset(sent, 0, sizeof(sent));
    CHECK(Tickrate_Ping(sent, 5, 1000, 2000) == -1);
    /* 300 packets at 10 ms: serverTime 10*seq, realtime 10*seq. */
    for (sequence = 1; sequence <= 300; ++sequence) {
        packet.p_cmdNumber = sequence;
        packet.p_serverTime = sequence * 10;
        packet.p_realtime = sequence * 10;
        Tickrate_PacketSent(sent, sequence, &packet);
    }
    /* Newest packet whose command the server has run, 500 ms deep: beyond 32 slots. */
    CHECK(Tickrate_Ping(sent, 301, 2500, 3100) == 600);
    CHECK(Tickrate_Ping(sent, 301, 3000, 3005) == 5);
    /* Slot reused by a later sequence no longer answers for the old one. */
    CHECK(Tickrate_Ping(sent, 301, 440, 3100) == -1);
    /* A sequence consumed without CL_WritePacket leaves a stale tag; it is skipped. */
    CHECK(Tickrate_Ping(sent, 302, 3000, 3005) == 5);
    sent[(uint32_t)300 & (TICKRATE_HISTORY - 1)].valid = qfalse;
    CHECK(Tickrate_Ping(sent, 301, 3000, 3005) == 15);
}

int main(void)
{
    test_ping();
    CHECK(Tickrate_ParseFrameMsec("1", "10") == 10);
    CHECK(Tickrate_ParseFrameMsec("1", "50") == 50);
    CHECK(Tickrate_ParseFrameMsec("1", "8") == 8);
    CHECK(!Tickrate_ParseFrameMsec("0", "10"));
    CHECK(!Tickrate_ParseFrameMsec("2", "10"));
    CHECK(!Tickrate_ParseFrameMsec("1", "16"));
    CHECK(!Tickrate_ParseFrameMsec("1", "0"));
    CHECK(!Tickrate_ParseFrameMsec("1", "10bad"));
    CHECK(!Tickrate_ParseFrameMsec("1", "99999999999999999999"));
    CHECK(!Tickrate_ParseFrameMsec(NULL, "10"));
    test_history();
    test_packet_clock();
    printf("Client tickrate: %u checks, %u failures; history %u bytes\n", checks, failures,
        (unsigned)(sizeof(tickrate_frame_t) * TICKRATE_HISTORY));
    return failures ? 1 : 0;
}
