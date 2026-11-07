#include "client.h"
#include "cl_recoil.h"
#include "cl_tickrate.h"
#include "sys_patch.h"
#include "xassets/weapondef.h"
#include "recoil/model.h"

/* Server-authoritative recoil, client side (docs/recoil-models.md). View kick is a shared deterministic
 * simulation (recoil/sim.h) folded into each usercmd's angles:
 *  - CL_Recoil_FoldCommand adds the simulated offset to the new command (CL_FinishMove);
 *  - the prediction Pmove wrapper records each command's inputs and fire events;
 *  - the recoil calls keep native gun kick (scaled by the model) and drop native view kick;
 *  - authoritative mode (dedicated server contract): the wire carries aim only (CL_Recoil_WireCommands),
 *    the server folds its own simulation, and svc_recoil corrects ours;
 *  - local mode (listen server): the folded command goes on the wire, as stock does with its kick.
 * Each command's record keeps the inputs both sides see, so the state is rebuilt by replay from `base`. */
#define CG_FIRE_RECOIL_CALL 0x457d2e      /* cgame CG_FireWeapon: call BG_WeaponFireRecoil */
#define LISTEN_FIRE_RECOIL_CALL 0x4a9cb2  /* listen server G_PlayerEvent: call BG_WeaponFireRecoil */
#define PREDICT_PMOVE_CALL 0x4476a3       /* CG_PredictPlayerState_Internal: call Pmove, ebx = command */
#define BG_WEAPON_FIRE_RECOIL 0x41a7b0
#define PMOVE 0x414d10

enum { MAX_EVENTS = 4 };
typedef enum { RECOIL_MODE_NATIVE, RECOIL_MODE_LOCAL, RECOIL_MODE_AUTHORITATIVE } recoil_mode_t;

typedef struct { playerState_t *ps; usercmd_t cmd; } pmove_head_t; /* leading pmove_t fields */
typedef struct {
    int cmd;                    /* command number; another value = empty slot */
    uint32_t time;              /* raw usercmd time */
    recoil_step_t step;         /* player state before the command (def resolved on replay) */
    recoil_shot_t shot[MAX_EVENTS]; /* fire events after the command (time, index, def set on replay) */
    unsigned shots;
    int32_t fold[RECOIL_AXES];  /* the offset added to this command; immutable once sent */
} recoil_record_t;
typedef struct { uint32_t time; unsigned index; } shot_counter_t;

WeaponDef *BG_GetWeaponDef(int index);
int BG_GetNumWeapons(void);
void CL_Recoil_Stub_FireRecoil(void);
void CL_Recoil_Stub_ListenFireRecoil(void);
void CL_Recoil_Stub_PredictPmove(void);
void CL_Recoil_NativeFireRecoil(const playerState_t *ps, float *kick, float *gunKick);

static qboolean hooked;         /* the cgame hooks every non-native model needs */
static cvar_t *activeModel;
static recoil_mode_t mode;
static recoil_contract_t active;
static const recoil_model_t *model;
static recoil_record_t records[CMD_BACKUP];
static struct { recoil_sim_t sim; int cmd; } base;  /* state after every record up to cmd */
static struct { recoil_sim_t sim; qboolean pending; } correction;
static struct { unsigned received, compared, mismatched; } corrections; /* cl_recoilStatus */
static recoil_weapon_cache_t weapons[RECOIL_WEAPONS];

const recoil_contract_t *CL_RecoilContract(void) { return &active; }

/* Without the cgame hooks only native recoil is supported, whatever the shared registry lists. */
static unsigned SupportedModels(void)
{
    return hooked ? Recoil_RuntimeModels() : 1u << RECOIL_NATIVE;
}

static qboolean HookCall(unsigned address, unsigned target, void (*stub)(void))
{
    const byte *call = (const byte *)address;
    if (call[0] != 0xe8 || address + 5 + *(const int *)(call + 1) != target) return qfalse;
    SetCall(address, stub);
    return qtrue;
}

/* Runs inside Patch_MainModule's writable window, before Com_Init. The listen-server hook is optional: without
 * it a host's own gun kick stays native. */
void CL_Recoil_Patch(void)
{
    hooked = HookCall(CG_FIRE_RECOIL_CALL, BG_WEAPON_FIRE_RECOIL, CL_Recoil_Stub_FireRecoil);
    hooked = HookCall(PREDICT_PMOVE_CALL, PMOVE, CL_Recoil_Stub_PredictPmove) && hooked;
    HookCall(LISTEN_FIRE_RECOIL_CALL, BG_WEAPON_FIRE_RECOIL, CL_Recoil_Stub_ListenFireRecoil);
}

static const recoil_weapon_kick_t *WeaponKick(uint32_t index, const uint32_t **param)
{
    const WeaponDef *def;
    *param = NULL;
    if (!index || index >= RECOIL_WEAPONS || (int)index >= BG_GetNumWeapons() || !(def = BG_GetWeaponDef(index)))
        return NULL;
    return Recoil_CachedWeapon(&weapons[index], &active, def->szInternalName, &def->fHipViewKickPitchMin,
        &def->fAdsViewKickPitchMin, def->hipGunKickReducedKickPercent, def->adsGunKickReducedKickPercent,
        def->iFireTime, param);
}

/* Replays one command exactly as the server executes it: recover to its time, then its first `shots` fire
 * events. Returns the last applied shot's gun-kick scale. */
static uint32_t Apply(recoil_sim_t *sim, const recoil_record_t *r, unsigned shots)
{
    const uint32_t *param;
    recoil_step_t step = r->step;
    uint32_t scale = RECOIL_SCALE;
    unsigned i;
    step.def = WeaponKick(step.weapon, &param);
    Recoil_Advance(sim, model, param, &step, r->time);
    for (i = 0; i < r->shots && i < shots; ++i) {
        recoil_shot_t shot = r->shot[i];
        shot.time = r->time;
        shot.shooter = (uint32_t)cg.clientNum;
        shot.index = i;
        if ((shot.def = WeaponKick(shot.weapon, &param))) scale = Recoil_Shot(sim, model, param, &shot).gun_scale;
    }
    return scale;
}

static const recoil_record_t *Record(int cmd)
{
    const recoil_record_t *r = &records[cmd & CMD_MASK];
    return r->cmd == cmd ? r : NULL;
}

/* The newest recorded command at or before `time`, or 0 when none is in the history. */
static int FindCommand(uint32_t time)
{
    int cmd;
    for (cmd = cl.cmdNumber; cmd > cl.cmdNumber - CMD_BACKUP; --cmd) {
        const recoil_record_t *r = Record(cmd);
        if (r && (int)(r->time - time) <= 0) return cmd;
    }
    return 0;
}

/* The state after command `through`: base plus the recorded commands since. */
static recoil_sim_t Predict(int through)
{
    recoil_sim_t sim = base.sim;
    int cmd = base.cmd;
    if (through - cmd > CMD_BACKUP) cmd = through - CMD_BACKUP;
    while (cmd < through) {
        const recoil_record_t *r = Record(++cmd);
        if (r) Apply(&sim, r, MAX_EVENTS);
    }
    return sim;
}

/* Moves base forward: to a server correction, then over commands the server has executed (the snapshot's
 * commandTime), which prediction never replays again. */
static void Commit(void)
{
    const recoil_record_t *r;
    if (correction.pending) {
        const int cmd = FindCommand(correction.sim.time);
        correction.pending = qfalse;
        ++corrections.received;
        if ((r = Record(cmd)) && r->time == correction.sim.time && cmd >= base.cmd) {
            const recoil_sim_t predicted = Predict(cmd);
            ++corrections.compared;
            if (memcmp(&predicted, &correction.sim, sizeof(predicted))) {
                ++corrections.mismatched;
                Com_DPrintf(CON_CHANNEL_CLIENT, "recoil: corrected at t=%u\n", r->time);
            }
        }
        base.sim = correction.sim;
        base.cmd = cmd ? cmd : cl.cmdNumber - CMD_BACKUP;
    }
    /* Commands without a record were never folded (not our player, inactive) and never will be. */
    if (cl.cmdNumber - base.cmd > CMD_BACKUP) base.cmd = cl.cmdNumber - CMD_BACKUP;
    while (base.cmd < cl.cmdNumber) {
        if ((r = Record(base.cmd + 1))) {
            if ((int)(r->time - (uint32_t)cl.snap.ps.commandTime) > 0) break;
            Apply(&base.sim, r, MAX_EVENTS);
            if (r->shots)
                Com_DPrintf(CON_CHANNEL_CLIENT, "recoil: t=%u shots=%u fold=%d,%d,%d speed=%d,%d,%d\n", r->time,
                    r->shots, r->fold[0], r->fold[1], r->fold[2], base.sim.kick.speed[0], base.sim.kick.speed[1],
                    base.sim.kick.speed[2]);
        }
        ++base.cmd;
    }
}

static void ResetHistory(void)
{
    memset(records, 0xff, sizeof(records)); /* cmd -1: no slot matches a command number */
    memset(&base, 0, sizeof(base));
    memset(&correction, 0, sizeof(correction));
    memset(&corrections, 0, sizeof(corrections));
    base.cmd = cl.cmdNumber;
}

/* CL_FinishMove, once the angles are final: record the command's inputs and fold the simulated view kick. */
void CL_Recoil_FoldCommand(usercmd_t *cmd)
{
    const int number = cl.cmdNumber;
    recoil_record_t *r = &records[number & CMD_MASK];
    const playerState_t *ps = &cg.predictedPlayerState;
    const snapshot_t *snap = cg.nextSnap ? cg.nextSnap : cg.snap;
    recoil_sim_t sim;
    r->cmd = -1;
    if (mode == RECOIL_MODE_NATIVE || clientUIActives.state != CA_ACTIVE || clc.demoplaying || !snap ||
        !Recoil_OwnPlayer(snap->ps.otherFlags)) return;
    r->time = (uint32_t)cmd->serverTime;
    r->step = Recoil_SampleStep(ps->weapFlags, ps->offHandIndex, ps->weapon, ps->fWeaponPosFrac, ps->pm_type,
        ps->otherFlags);
    r->shots = 0;
    r->cmd = number;
    Commit();
    sim = Predict(number);
    Recoil_Offset(&sim, r->fold);
    Recoil_FoldAngles(cmd->angles, r->fold, 1);
}

/* CL_WritePacket: copies of the commands to send. Under server-authoritative recoil the wire carries aim only,
 * and the delta base differs from every command so the angles are sent explicitly: the server's base comes from
 * its folded player state, the client's from the snapshot. */
void CL_Recoil_WireCommands(usercmd_t *wire, int first, int count, usercmd_t *base_cmd)
{
    int i;
    for (i = 0; i < count; ++i) {
        const recoil_record_t *r = Record(first + i);
        wire[i] = cl.cmds[(first + i) & CMD_MASK];
        if (mode == RECOIL_MODE_AUTHORITATIVE && r) Recoil_FoldAngles(wire[i].angles, r->fold, -1);
    }
    if (mode == RECOIL_MODE_AUTHORITATIVE && count > 0)
        for (i = 0; i < RECOIL_AXES; ++i) base_cmd->angles[i] = wire[0].angles[i] ^ 1;
}

/* Prediction replays every unacknowledged command through this call, from the newest snapshot. Each pass
 * re-records the command's inputs as the server will see them: the player state before Pmove (recovery) and
 * the fire events after it. ebx (the command number) arrives through the stub. */
void CL_Recoil_PredictPmove(pmove_head_t *pm, int number)
{
    playerState_t *ps = pm->ps;
    const int before = ps->eventSequence;
    recoil_record_t *r = &records[number & CMD_MASK];
    const qboolean tracked = mode != RECOIL_MODE_NATIVE && r->cmd == number &&
        r->time == (uint32_t)pm->cmd.serverTime && Recoil_OwnPlayer(ps->otherFlags);
    int sequence;
    if (tracked)
        r->step = Recoil_SampleStep(ps->weapFlags, ps->offHandIndex, ps->weapon, ps->fWeaponPosFrac, ps->pm_type,
            ps->otherFlags);
    ((void (__cdecl *)(pmove_head_t *))PMOVE)(pm);
    if (!tracked || !Recoil_OwnPlayer(ps->otherFlags)) return;
    r->shots = 0;
    sequence = ps->eventSequence - before > MAX_EVENTS ? ps->eventSequence - MAX_EVENTS : before;
    for (; sequence != ps->eventSequence; ++sequence)
        if (Recoil_FireEvent(ps->events[sequence & (MAX_EVENTS - 1)]))
            r->shot[r->shots++] = Recoil_SampleShot(ps->weapFlags, ps->offHandIndex, ps->weapon, ps->fWeaponPosFrac,
                ps->weaponRestrictKickTime);
}

/* The model's gun-kick scale for one recorded shot (native strength when the shot was not recorded). */
static uint32_t ShotGunScale(uint32_t time, unsigned index)
{
    const int cmd = FindCommand(time);
    const recoil_record_t *r = Record(cmd);
    recoil_sim_t sim;
    if (!r || r->time != time || index >= r->shots) return RECOIL_SCALE;
    sim = Predict(cmd - 1);
    return Apply(&sim, r, index + 1);
}

/* Both recoil calls: native gun kick (the viewmodel; on a listen server the host's bullets) takes the model's
 * scale for this shot. Fire events arrive one call per shot, so the n-th call for a command time is its n-th
 * shot. The cgame also drops native view kick, which the simulation owns. Other players on a listen server stay
 * native: the stock server code publishes no contract. */
static void FireRecoil(const playerState_t *ps, float *kick, float *gunKick, qboolean listen)
{
    static shot_counter_t counters[2];
    shot_counter_t *counter = &counters[listen];
    const uint32_t time = (uint32_t)ps->commandTime;
    float before[3];
    VectorCopy(gunKick, before);
    CL_Recoil_NativeFireRecoil(ps, kick, gunKick);
    if (listen ? mode != RECOIL_MODE_LOCAL || ps->clientNum != cg.clientNum
               : mode == RECOIL_MODE_NATIVE || !Recoil_OwnPlayer(ps->otherFlags)) return;
    if (!listen) VectorClear(kick);
    counter->index = counter->time == time ? counter->index + 1 : 0;
    counter->time = time;
    Recoil_ScaleGunKick(gunKick, before, ShotGunScale(time, counter->index));
}

void CL_Recoil_FireRecoil(const playerState_t *ps, float *kick, float *gunKick) { FireRecoil(ps, kick, gunKick, qfalse); }
void CL_Recoil_ListenFireRecoil(const playerState_t *ps, float *kick, float *gunKick) { FireRecoil(ps, kick, gunKick, qtrue); }

/* svc_recoil: the server's state after the last command it executed. */
void CL_Recoil_ParseCorrection(msg_t *msg)
{
    uint32_t packed[RECOIL_PACKED];
    int i;
    for (i = 0; i < RECOIL_PACKED; ++i) packed[i] = (uint32_t)MSG_ReadLong(msg);
    if (mode != RECOIL_MODE_AUTHORITATIVE || clc.demoplaying) return;
    Recoil_Unpack(&correction.sim, packed);
    correction.pending = qtrue;
}

/* Restarts the simulation only when the contract or mode actually changes (systeminfo changes for many reasons). */
static void Activate(const recoil_contract_t *contract, recoil_mode_t contractMode)
{
    const recoil_model_t *next = Recoil_Model(contract->model);
    const recoil_mode_t nextMode = Recoil_Simulated(next) ? contractMode : RECOIL_MODE_NATIVE;
    if (nextMode == mode && !memcmp(contract, &active, sizeof(active))) return;
    active = *contract;
    model = next;
    mode = nextMode;
    memset(weapons, 0, sizeof(weapons));
    ResetHistory();
    Cvar_SetInt(activeModel, contract->model);
}

/* A new gamestate: weapon indices may change, so the caches and history always restart. */
void CL_Recoil_Clear(void)
{
    recoil_contract_t native;
    if (!activeModel) return;
    Recoil_Decode(RECOIL_NATIVE_WIRE, &native);
    mode = RECOIL_MODE_LOCAL; /* differs from native, so Activate resets */
    Activate(&native, RECOIL_MODE_NATIVE);
}

static void CL_Recoil_Status_f(void)
{
    static const char *modes[] = {"native", "local (listen server)", "server-authoritative"};
    Com_Printf(CON_CHANNEL_DONT_FILTER, "recoil: mode=%s model=%u hooked=%d base=%d t=%u kick=%d,%d,%d\n",
        modes[mode], active.model, hooked, base.cmd, base.sim.time,
        base.sim.kick.angle[0], base.sim.kick.angle[1], base.sim.kick.angle[2]);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "recoil: corrections received=%u compared=%u mismatched=%u last t=%u\n",
        corrections.received, corrections.compared, corrections.mismatched, correction.sim.time);
}

void CL_Recoil_Init(void)
{
    CL_RegisterCapability("cl_recoilProtocol", RECOIL_PROTOCOL, 0, RECOIL_PROTOCOL,
        "Recoil capability: 2 = applies the server-authoritative recoil simulation");
    CL_RegisterCapability("cl_recoilModels", SupportedModels(), 0, 0x7fffffff, "Bitmask of recoil models this client applies");
    activeModel = Cvar_RegisterInt("cl_recoilModel", 0, 0, 31, CVAR_ROM, "Effective server recoil model");
    Cmd_AddCommand("cl_recoilStatus", CL_Recoil_Status_f);
    CL_Recoil_Clear();
}

/* A listen server runs the stock server code, which publishes no contract. Its host reads the same
 * server setting (recoil_model) and profile file (recoil/model_<id>.profile) a dedicated server uses.
 * Like a dedicated server it keeps the profile it first read: once a map has loaded, the filesystem
 * no longer serves loose files, so later map loads and restarts reuse it. Restart the game to change it. */
static void ActivateListenServer(void)
{
    static char wire[RECOIL_WIRE_SIZE];
    static int wireModel;
    const int requested = Cvar_GetInt("recoil_model");
    recoil_contract_t contract;
    void *file;
    int length;
    if (requested <= 0 || !com_sv_running->boolean) return;
    if (requested != wireModel) {
        length = FS_ReadFile(va("recoil/model_%d.profile", requested), &file);
        if (length <= 0) { Com_PrintWarning(CON_CHANNEL_CLIENT, "recoil_model %d: no recoil/model_%d.profile\n", requested, requested); return; }
        if (length >= (int)sizeof(wire)) length = sizeof(wire) - 1;
        memcpy(wire, file, length);
        FS_FreeFile(file);
        while (length > 0 && (wire[length - 1] == '\n' || wire[length - 1] == '\r' || wire[length - 1] == ' ')) --length;
        wire[length] = 0;
        wireModel = requested;
    }
    if (!Recoil_Decode(wire, &contract) || !Recoil_Compatible(&contract, RECOIL_PROTOCOL, SupportedModels())) {
        Com_PrintWarning(CON_CHANNEL_CLIENT, "recoil_model %d: unsupported profile; native recoil\n", requested);
        return;
    }
    Activate(&contract, RECOIL_MODE_LOCAL);
}

void CL_Recoil_SystemInfoChanged(const char *systemInfo)
{
    const char *wire = Info_ValueForKey(systemInfo, "sv_recoilContract");
    recoil_contract_t contract;
    if (!activeModel) return;
    if (!wire[0]) { ActivateListenServer(); return; }
    if (!Recoil_Decode(wire, &contract) ||
        !Recoil_Compatible(&contract, RECOIL_PROTOCOL, SupportedModels())) {
        Com_Error(ERR_DROP, "Unsupported server recoil contract; install the matching PromodFlex client.");
        return;
    }
    Activate(&contract, RECOIL_MODE_AUTHORITATIVE);
}
