#include "recoil/model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* AK-47 view kick from weapons/mp/ak47_mp: pitch -30..60, yaw 60..-60 deg/s, centre 1500 deg/s^2. */
static const float ak_view[5] = {-30.0f, 60.0f, 60.0f, -60.0f, 1500.0f};

static recoil_weapon_kick_t AkKick(void)
{
    recoil_weapon_kick_t kick;
    Recoil_WeaponKick(&kick, ak_view, ak_view, 100.0f, 100.0f, 100);
    return kick;
}

static recoil_step_t Step(const recoil_weapon_kick_t *def)
{
    recoil_step_t step = {1, def, 1, 1};
    return step;
}

static recoil_shot_t Shot(const recoil_weapon_kick_t *def, uint32_t time)
{
    recoil_shot_t shot = {time, 3, 1, 0, def, 1, 0};
    return shot;
}

/* Float port of one 5 ms slice of the stock CG_KickAngles (client bin 0x44FE20) for one axis. */
static void StockKick(float *angle, float *speed, float center)
{
    const float dt = 0.005f;
    float change, next;
    if (*angle == 0.0f && *speed == 0.0f) return;
    if (*angle != 0.0f) *speed += (*angle > 0 ? -1.0f : 1.0f) * center * dt;
    change = *speed * dt;
    if (*angle * change < 0) change *= 0.06f;
    next = *angle + change;
    if (*angle * next < 0 || next == 0.0f) { *angle = *speed = 0.0f; return; }
    *angle = next;
    if (*angle > 10.0f || *angle < -10.0f) { *angle = *angle > 0 ? 10.0f : -10.0f; *speed = 0.0f; }
}

static void TestContract(const char *corpus_path)
{
    recoil_contract_t contract, before;
    const char *bad[] = {"", "2:0", "1:2", "1:00", "1:0:ak74u,2500,3,7500", "1:1:", "1:1:ak74u,0,3,7500",
        "1:1:ak74u,2500,1,7500", "1:1:ak74u,2500,3,7500;ak74u,2500,3,7500", "1:1:ak74u,2500,3,7500;",
        "1:1:../ak74u,2500,3,7500", "1:1:ak74u,42949672960,3,7500", "1:1:ak74u,02500,3,7500",
        "1:1:ak74u,2500,3,7500\n", "1:1:ak74u,2500,3", "1:1:ak74u,2500,3,7500,1"};
    unsigned i;
    char line[1024];
    FILE *corpus = fopen(corpus_path, "rb");
    assert(corpus);
    while (fgets(line, sizeof(line), corpus)) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) line[--length] = 0;
        assert(line[1] == '\t' && (line[0] == '0' || line[0] == '1'));
        assert(Recoil_Decode(line + 2, &contract) == line[0] - '0');
    }
    assert(!ferror(corpus)); fclose(corpus);
    assert(Recoil_Decode(RECOIL_NATIVE_WIRE, &contract) && contract.model == RECOIL_NATIVE);
    assert(Recoil_Compatible(&contract, 0, 0)); /* stock clients on native servers */
    assert(Recoil_Decode("1:1:ak74u,2500,3,7500/mp5,5000,4,5000", &contract));
    assert(contract.count == 2 && !strcmp(contract.weapons[1].weapon, "mp5") && contract.weapons[1].param[2] == 5000);
    assert(Recoil_Compatible(&contract, RECOIL_PROTOCOL, Recoil_RuntimeModels()));
    assert(!Recoil_Compatible(&contract, 1, Recoil_RuntimeModels()));  /* client-applied builds would double the kick */
    assert(!Recoil_Compatible(&contract, RECOIL_PROTOCOL, 1u << RECOIL_NATIVE));
    assert(Recoil_WeaponParams(&contract, "ak74u_reflex_mp") == contract.weapons[0].param);
    assert(!Recoil_WeaponParams(&contract, "ak74") && !Recoil_WeaponParams(&contract, "ak74u") &&
           !Recoil_WeaponParams(&contract, "m4_mp") && !Recoil_WeaponParams(&contract, NULL));
    before = contract;
    for (i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        assert(!Recoil_Decode(bad[i], &contract));
        assert(!memcmp(&before, &contract, sizeof(contract)));
    }
    assert(!Recoil_Decode(NULL, &contract));
}

static void TestRamp(void)
{
    const recoil_ramp_t ak = {2500, 3, 7500};
    recoil_ramp_state_t state = {0}, split, whole;
    unsigned i;
    assert(Recoil_Fire(&state, &ak, 78, 1000) == 2500);
    assert(Recoil_Fire(&state, &ak, 78, 1078) == 6250);
    for (i = 3; i < 100; ++i) assert(Recoil_Fire(&state, &ak, 78, 1000 + (i - 1) * 78) == 10000);
    split = whole = state;
    for (i = 0; i < 40; ++i) assert(Recoil_RampAdvance(&split, &ak, 8722 + i * 8));
    assert(Recoil_RampAdvance(&whole, &ak, 8722 + 39 * 8));
    assert(!memcmp(&whole, &split, sizeof(whole)));
    assert(!Recoil_Fire(&state, &ak, 78, 1100));             /* stale time */
    assert(Recoil_Fire(&state, &ak, 78, 12000) == 2500);     /* recovered */
    memset(&state, 0, sizeof(state));
    assert(Recoil_Fire(&state, &ak, 78, UINT32_MAX - 77) == 2500);
    assert(Recoil_Fire(&state, &ak, 78, 0) == 6250);          /* time wraps */
    memset(&state, 0, sizeof(state));
    assert(Recoil_Fire(&state, &ak, 78, 0) == 2500);
    assert(Recoil_Fire(&state, &ak, 78, 100) == 6085);        /* tapping recovers continuously */
}

static void TestNativeShot(void)
{
    recoil_weapon_kick_t def = AkKick();
    recoil_shot_t shot = Shot(&def, 5000);
    recoil_shot_result_t a, b;
    unsigned i;
    assert(Recoil_Fixed(1.5f, 1.0) == 2 && Recoil_Fixed(-1.5f, 1.0) == -2 && Recoil_Fixed(-30.0f, 1000.0) == -30000);
    assert(def.pitch[RECOIL_ADS][0] == -30000 && def.yaw[RECOIL_ADS][1] == -60000 && def.center[RECOIL_HIP] == 1500);
    assert(def.reduced[RECOIL_ADS] == RECOIL_SCALE && def.fire_msec == 100);
    a = Recoil_NativeShot(&shot);
    /* Golden values: the client and server builds must reproduce these bit for bit. */
    assert(Recoil_Hash(1, &shot) == UINT64_C(0x61fbaaebfc5a0a55));
    assert(a.view[0] == 2647 && a.view[1] == 14776 && a.view[2] == -7388 && a.gun_scale == RECOIL_SCALE);
    shot.time += 7;
    b = Recoil_NativeShot(&shot);
    assert(!memcmp(&a, &b, sizeof(a))); /* the client picks the command time, so it must not move the roll */
    shot.number = 1;
    b = Recoil_NativeShot(&shot);
    assert(memcmp(&a, &b, sizeof(a))); /* the next shot differs */
    {   /* the distribution covers the range */
        int32_t low = INT32_MAX, high = INT32_MIN;
        for (i = 0; i < 2000; ++i) {
            shot.number = i;
            b = Recoil_NativeShot(&shot);
            if (b.view[1] < low) low = b.view[1];
            if (b.view[1] > high) high = b.view[1];
        }
        assert(low < -58000 && high > 58000);
    }
    def.reduced[RECOIL_ADS] = 5000;
    shot.reduced = 1;
    b = Recoil_NativeShot(&shot);
    shot.reduced = 0;
    a = Recoil_NativeShot(&shot);
    assert(b.view[1] == (int32_t)((int64_t)a.view[1] * 5000 / RECOIL_SCALE));
}

static void TestRecovery(void)
{
    recoil_weapon_kick_t def = AkKick();
    const recoil_step_t step = Step(&def);
    const recoil_model_t *native = Recoil_Model(RECOIL_NATIVE);
    recoil_sim_t sim = {0}, whole = {0}, split;
    int peak_int = 0, rest_int = 0, t;
    float angle = 0, speed = -60.0f, peak_float = 0;
    int rest_float = 0;

    /* Parity with the stock float integrator at its own 5 ms slice: same peak and settle time. */
    Recoil_Reset(&sim, 1000);
    sim.kick.speed[0] = -60000;
    for (t = 1; t <= 2000; ++t) {
        Recoil_Advance(&sim, native, NULL, &step, 1000 + (uint32_t)t);
        if (sim.kick.angle[0] < peak_int) peak_int = sim.kick.angle[0];
        if (!rest_int && Recoil_AtRest(&sim)) rest_int = t;
    }
    for (t = 5; t <= 2000; t += 5) {
        StockKick(&angle, &speed, 1500.0f);
        if (angle < peak_float) peak_float = angle;
        if (!rest_float && angle == 0.0f && speed == 0.0f) rest_float = t;
    }
    printf("recovery parity: peak %.4f deg (stock %.4f), rest at %d ms (stock %d ms)\n",
        peak_int / 1e6, peak_float, rest_int, rest_float);
    assert(abs(peak_int - (int)(peak_float * 1e6f)) <= 50); /* within 0.00005 degrees */
    assert(rest_int == rest_float);

    /* Split invariance: one interval, single milliseconds and irregular frames end identically. */
    Recoil_Reset(&whole, 1000);
    whole.kick.speed[0] = -60000; whole.kick.speed[1] = 41000; whole.kick.speed[2] = -20500;
    split = whole;
    Recoil_Advance(&whole, native, NULL, &step, 1150);
    for (t = 1001; t <= 1150; t += 1 + (t % 7))
        Recoil_Advance(&split, native, NULL, &step, (uint32_t)t);
    Recoil_Advance(&split, native, NULL, &step, 1150);
    assert(!memcmp(&whole, &split, sizeof(whole)));

    /* Ceiling, resets and gaps. */
    Recoil_Reset(&sim, 1000);
    sim.kick.speed[0] = -2000000;
    Recoil_Advance(&sim, native, NULL, &step, 1010);
    assert(sim.kick.angle[0] == -RECOIL_CEILING);
    Recoil_Advance(&sim, native, NULL, &step, 900);             /* time went backwards */
    assert(Recoil_AtRest(&sim) && sim.time == 900);
    sim.kick.speed[1] = 5000;
    Recoil_Advance(&sim, native, NULL, &step, 900 + RECOIL_MAX_GAP + 1);
    assert(Recoil_AtRest(&sim));
    sim.kick.speed[1] = 5000;
    {
        recoil_step_t dead = step;
        dead.alive = 0;
        Recoil_Advance(&sim, native, NULL, &dead, 4000);
        assert(Recoil_AtRest(&sim) && sim.time == 4000);
    }
}

static void TestContinuousModel(void)
{
    recoil_weapon_kick_t def = AkKick();
    recoil_contract_t contract;
    const recoil_model_t *model = Recoil_Model(RECOIL_CONTINUOUS);
    const uint32_t *param;
    /* The ramp's kick distance (3500, 5666, 7833, 10000) as the impulse scale that produces it. */
    const unsigned expected[] = {5916, 7527, 8850, 10000, 10000};
    recoil_sim_t sim = {0};
    unsigned i;
    def.fire_msec = 78;
    assert(Recoil_Decode("1:1:ak47,3500,4,7500", &contract));
    param = Recoil_WeaponParams(&contract, "ak47_mp");
    Recoil_Reset(&sim, 1000);
    for (i = 0; i < 5; ++i) {
        recoil_shot_t shot = Shot(&def, 1000 + i * 78);
        recoil_shot_result_t native;
        shot.number = sim.shots;
        native = Recoil_NativeShot(&shot);
        recoil_shot_result_t r;
        recoil_step_t step = Step(&def);
        Recoil_Advance(&sim, model, param, &step, shot.time);
        r = Recoil_Shot(&sim, model, param, &shot);
        assert(r.gun_scale == expected[i]);
        assert(r.view[1] == (int32_t)((int64_t)native.view[1] * expected[i] / RECOIL_SCALE));
        assert(!memcmp(sim.kick.speed, r.view, sizeof(sim.kick.speed)));
    }
    {   /* switching weapons restarts the opening; weapons without a profile stay native */
        recoil_shot_t other = Shot(&def, 1400);
        other.weapon = 2;
        assert(Recoil_Shot(&sim, model, param, &other).gun_scale == 5916);
        assert(Recoil_Shot(&sim, model, NULL, &other).gun_scale == RECOIL_SCALE);
    }
}

/* The opening is a share of kick distance: a scaled impulse must move the kick that share as far. */
/* The kick's highest pitch after an upward impulse of `speed` mdeg/s, through native recovery. */
static int32_t Peak(int32_t speed)
{
    const recoil_weapon_kick_t def = AkKick();
    const recoil_step_t step = Step(&def);
    recoil_sim_t sim = {0};
    int32_t peak = 0;
    uint32_t t;
    Recoil_Reset(&sim, 1000);
    sim.kick.speed[0] = speed;
    for (t = 1; t <= 400; ++t) {
        Recoil_Advance(&sim, Recoil_Model(RECOIL_NATIVE), NULL, &step, 1000 + t);
        if (sim.kick.angle[0] < peak) peak = sim.kick.angle[0];
    }
    return peak;
}

static void TestDistanceScale(void)
{
    static const unsigned distance[] = {2500, 3500, 6500};
    unsigned i;
    assert(Recoil_DistanceScale(0) == 0 && Recoil_DistanceScale(RECOIL_SCALE) == RECOIL_SCALE);
    assert(Recoil_DistanceScale(2500) == 5000 && Recoil_DistanceScale(3500) == 5916); /* sqrt, rounded down */
    assert(Peak(-60000) == -1350000);
    for (i = 0; i < 3; ++i) {
        const int32_t peak = Peak((int32_t)(-60000LL * Recoil_DistanceScale(distance[i]) / RECOIL_SCALE));
        assert(llabs((long long)peak * RECOIL_SCALE / -1350000 - distance[i]) <= 300); /* within 3 points */
    }
}

/* Engine rules both sides share: input sampling and the usercmd fold. */
static void TestEngine(void)
{
    const int32_t fold[RECOIL_AXES] = {-313, 235, -31};
    int angles[RECOIL_AXES] = {65500, 10, 0};
    recoil_step_t step = Recoil_SampleStep(RECOIL_WEAPFLAG_OFFHAND, 7, 3, 0.75f, 0, RECOIL_POF_PLAYER);
    recoil_shot_t shot = Recoil_SampleShot(0, 7, 3, 1.0f, 40);
    assert(step.weapon == 7 && step.ads_half && step.alive && !step.def);
    assert(shot.weapon == 3 && shot.ads_full && shot.reduced);
    assert(!Recoil_SampleStep(0, 0, 3, 0.0f, 0, RECOIL_POF_PLAYER | RECOIL_POF_FOLLOW).alive); /* follower */
    assert(!Recoil_SampleStep(0, 0, 3, 0.0f, 7, RECOIL_POF_PLAYER).alive);                    /* dead */
    assert(Recoil_SampleStep(0, 0, 3, 0.0f, RECOIL_PM_LASTSTAND, RECOIL_POF_PLAYER).alive);
    assert(Recoil_FireEvent(RECOIL_EV_FIRE_WEAPON | 0x100) && !Recoil_FireEvent(25));
    /* The event counter wraps at 256: a Pmove across the wrap added one event, not minus 255. */
    assert(Recoil_NewEvents(255, 0) == 1 && Recoil_NewEvents(254, 1) == 3 && Recoil_NewEvents(7, 7) == 0);
    assert(Recoil_NewEvents(250, 9) == RECOIL_PS_EVENTS && Recoil_NewEvents(10, 12) == 2);
    Recoil_FoldAngles(angles, fold, 1);
    assert(angles[0] == 65187 && angles[1] == 245 && angles[2] == 65505);
    Recoil_FoldAngles(angles, fold, -1);
    assert(angles[0] == 65500 && angles[1] == 10 && angles[2] == 0);
}

/* The shot count picks each roll: one step per shot, kept across a reset, and carried by the correction. */
static void TestShotCount(void)
{
    recoil_weapon_kick_t def = AkKick();
    const recoil_model_t *native = Recoil_Model(RECOIL_NATIVE);
    const recoil_shot_t shot = Shot(&def, 5000);
    recoil_shot_t second = shot;
    recoil_shot_result_t b, expected;
    recoil_sim_t sim = {0}, copy;
    uint32_t packed[RECOIL_PACKED];
    Recoil_Reset(&sim, 5000);
    Recoil_Shot(&sim, native, NULL, &shot);
    b = Recoil_Shot(&sim, native, NULL, &shot);
    second.number = 1;
    expected = Recoil_NativeShot(&second);
    assert(sim.shots == 2 && !memcmp(&b, &expected, sizeof(b))); /* the second identical shot rolled number 1 */
    Recoil_Reset(&sim, 6000);
    assert(sim.shots == 2 && Recoil_AtRest(&sim) && sim.time == 6000);
    sim.kick.angle[1] = -5;
    sim.model[3] = UINT64_C(0x123456789abcdef0);
    Recoil_Pack(&sim, packed);
    memset(&copy, 0xff, sizeof(copy));
    Recoil_Unpack(&copy, packed);
    assert(!memcmp(&copy, &sim, sizeof(sim)));
}

static void TestOffset(void)
{
    recoil_sim_t sim;
    int32_t off[RECOIL_AXES];
    memset(&sim, 0, sizeof(sim));
    sim.kick.angle[0] = RECOIL_CEILING;  /* 10 degrees = 1820.44 units */
    sim.kick.angle[1] = -RECOIL_CEILING;
    sim.kick.angle[2] = 2747;            /* just over half a unit (2746.58 micro-degrees) */
    Recoil_Offset(&sim, off);
    assert(off[0] == 1820 && off[1] == -1820 && off[2] == 1);
    sim.kick.angle[2] = 2746;
    Recoil_Offset(&sim, off);
    assert(off[2] == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    TestContract(argv[1]);
    TestRamp();
    TestNativeShot();
    TestRecovery();
    TestContinuousModel();
    TestDistanceScale();
    TestEngine();
    TestShotCount();
    TestOffset();
    puts("recoil contract, ramp, native shot, recovery parity, continuous model, distance scale, engine, shot count "
         "and offset checks passed");
    return 0;
}
