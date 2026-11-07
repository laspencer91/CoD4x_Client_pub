#include "recoil/model.h"
#include <assert.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    recoil_contract_t contract, before;
    recoil_weapon_t ak = {"ak74u", 2500, 3, 7500};
    recoil_state_t state = {0}, checkpoint, replay, split, whole;
    char fingerprint[17], other[17];
    const char *bad[] = {"", "2:0", "1:2", "1:00", "1:0:ak74u,2500,3,7500",
        "1:1:", "1:1:ak74u,0,3,7500", "1:1:ak74u,2500,1,7500",
        "1:1:ak74u,2500,3,7500;ak74u,2500,3,7500", "1:1:ak74u,2500,3,7500;",
        "1:1:../ak74u,2500,3,7500", "1:1:ak74u,42949672960,3,7500",
        "1:1:ak74u,02500,3,7500", "1:1:ak74u,2500,3,7500\n"};
    unsigned i;
    FILE *corpus;
    char line[1024];
    assert(argc == 2);
    corpus = fopen(argv[1], "rb");
    assert(corpus);
    while (fgets(line, sizeof(line), corpus)) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) line[--length] = 0;
        assert(line[1] == '\t' && (line[0] == '0' || line[0] == '1'));
        assert(Recoil_Decode(line + 2, &contract) == line[0] - '0');
    }
    assert(!ferror(corpus)); fclose(corpus);
    assert(Recoil_Decode("1:0", &contract));
    assert(Recoil_Compatible(&contract, 0, 0)); /* legacy clients on native servers */
    assert(Recoil_Decode("1:1:ak74u,2500,3,7500", &contract));
    assert(contract.count == 1 && !strcmp(contract.weapons[0].weapon, "ak74u"));
    assert(Recoil_Compatible(&contract, 1, Recoil_RuntimeModels())); /* model 1 is integrated */
    assert(!Recoil_Compatible(&contract, 1, 1u << RECOIL_NATIVE));    /* a client without the hook */
    assert(!Recoil_Compatible(&contract, 0, 3));
    assert(Recoil_Compatible(&contract, 1, 3));
    assert(Recoil_Compatible(&contract, 2, 3)); /* newer reader retains v1 support */
    before = contract;
    for (i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        assert(!Recoil_Decode(bad[i], &contract));
        assert(!memcmp(&before, &contract, sizeof(contract)));
    }
    assert(!Recoil_Decode(NULL, &contract));
    assert(Recoil_Decode("1:1:ak74u,2500,3,7500/mp5,5000,4,5000", &contract));
    Recoil_Fingerprint("1:1:ak74u,2500,3,7500", fingerprint);
    Recoil_Fingerprint("1:1:ak74u,2500,3,7501", other);
    assert(strlen(fingerprint) == 16 && strcmp(fingerprint, other));

    {   /* PromodFlex opening: 25% rising to full strength on the fourth shot at AK74u cadence. */
        const recoil_weapon_t opening = {"ak74u", 2500, 4, 7500};
        recoil_state_t burst = {0};
        assert(Recoil_Fire(&burst, &opening, 78, 1000, 1) == 2500);
        assert(Recoil_Fire(&burst, &opening, 78, 1078, 2) == 5000);
        assert(Recoil_Fire(&burst, &opening, 78, 1156, 3) == 7500);
        assert(Recoil_Fire(&burst, &opening, 78, 1234, 4) == 10000);
        assert(Recoil_Fire(&burst, &opening, 78, 1312, 5) == 10000);
    }
    assert(Recoil_Fire(&state, &ak, 78, 1000, 1) == 2500);
    assert(Recoil_Fire(&state, &ak, 78, 1078, 2) == 6250);
    checkpoint = state;
    assert(Recoil_Fire(&state, &ak, 78, 1156, 3) == 10000);
    for (i = 4; i < 100; ++i)
        assert(Recoil_Fire(&state, &ak, 78, 1000 + (i - 1) * 78, i) == 10000);
    replay = checkpoint;
    assert(Recoil_Fire(&replay, &ak, 78, 1156, 3) == 10000);
    whole = replay;
    assert(Recoil_Fire(&replay, &ak, 78, 1156, 3) == 10000);
    assert(!memcmp(&whole, &replay, sizeof(whole))); /* duplicate event */
    assert(Recoil_Fire(&replay, &ak, 78, 1100, 4) == 0); /* stale time */
    assert(!memcmp(&whole, &replay, sizeof(whole)));
    split = whole;
    for (i = 0; i < 40; ++i) assert(Recoil_Advance(&split, &ak, 1156 + i * 8));
    assert(Recoil_Advance(&whole, &ak, 1156 + 39 * 8));
    assert(!memcmp(&whole, &split, sizeof(whole)));
    assert(Recoil_Fire(&replay, &ak, 78, 4000, 4) == 2500); /* actual recovery */
    memset(&state, 0, sizeof(state));
    assert(Recoil_Fire(&state, &ak, 78, UINT32_MAX - 77, UINT32_MAX) == 2500);
    assert(Recoil_Fire(&state, &ak, 78, 0, 0) == 6250); /* simulation time/sequence wrap */
    memset(&state, 0, sizeof(state));
    assert(Recoil_Fire(&state, &ak, 78, 0, 1) == 2500);
    assert(Recoil_Fire(&state, &ak, 78, 100, 2) == 6085); /* tapping recovers continuously */
    puts("recoil contract, compatibility, continuous recovery, cadence, and replay checks passed");
    return 0;
}
