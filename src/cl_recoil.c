#include "client.h"
#include "cl_recoil.h"
#include "cl_tickrate.h"
#include "sys_patch.h"
#include "xassets/weapondef.h"
#include "xzone.h"
#include "recoil/model.h"

/* cgame's local-player `call BG_WeaponFireRecoil`. View kick only ever moves the client's own aim
 * (cg.kickAngles); the server computes and discards it, so recoil models are applied here. */
#define CG_FIRE_RECOIL_CALL 0x457d2e
#define BG_WEAPON_FIRE_RECOIL 0x41a7b0

extern WeaponDef *bg_weaponDefs[];
void CL_Recoil_Stub_FireRecoil(void);
void CL_Recoil_NativeFireRecoil(const playerState_t *ps, float *kick, float *gunKick);

static qboolean hooked;
static cvar_t *activeModel;
static recoil_contract_t active;
static recoil_state_t shot;      /* the local player's current weapon */
static unsigned shotWeapon;
static uint32_t shotSequence;

const recoil_contract_t *CL_RecoilContract(void) { return &active; }

/* Without the hook only native recoil is supported, whatever the shared registry lists. */
static unsigned SupportedModels(void)
{
    return hooked ? Recoil_RuntimeModels() : Recoil_RuntimeModels() & (1u << RECOIL_NATIVE);
}

/* Runs inside Patch_MainModule's writable window, before Com_Init. */
void CL_Recoil_Patch(void)
{
    const byte *call = (const byte *)CG_FIRE_RECOIL_CALL;
    if (call[0] != 0xe8 || CG_FIRE_RECOIL_CALL + 5 + *(const int *)(call + 1) != BG_WEAPON_FIRE_RECOIL) return;
    SetCall(CG_FIRE_RECOIL_CALL, CL_Recoil_Stub_FireRecoil);
    hooked = qtrue;
}

/* Profile entries name the weapon family: "ak74u" covers ak74u_mp, ak74u_reflex_mp and ak74u_silencer_mp. */
static const recoil_weapon_t *Profile(const WeaponDef *def)
{
    unsigned i;
    for (i = 0; def && def->szInternalName && i < active.count; ++i) {
        const size_t length = strlen(active.weapons[i].weapon);
        if (!strncmp(def->szInternalName, active.weapons[i].weapon, length) && def->szInternalName[length] == '_')
            return &active.weapons[i];
    }
    return NULL;
}

/* Model 1 scales the native kick of each shot by its opening multiplier; direction and randomness stay native. */
void CL_Recoil_FireRecoil(const playerState_t *ps, float *kick, float *gunKick)
{
    const unsigned index = (ps->weapFlags & 2) ? (unsigned)ps->offHandIndex : ps->weapon;
    const WeaponDef *def = index < ASSET_TYPE_WEAPON_COUNT ? bg_weaponDefs[index] : NULL;
    const recoil_weapon_t *profile;
    float kickBefore[3], gunBefore[3], native, factor;
    unsigned scale;
    int i;
    VectorCopy(kick, kickBefore);
    VectorCopy(gunKick, gunBefore);
    CL_Recoil_NativeFireRecoil(ps, kick, gunKick);
    if (active.model != RECOIL_CONTINUOUS || !(profile = Profile(def))) return;
    if (index != shotWeapon) { memset(&shot, 0, sizeof(shot)); shotWeapon = index; }
    scale = Recoil_Fire(&shot, profile, (unsigned)def->iFireTime, (uint32_t)ps->commandTime, ++shotSequence);
    if (!scale) {  /* time went backwards: respawn, map restart or a new server */
        memset(&shot, 0, sizeof(shot));
        scale = Recoil_Fire(&shot, profile, (unsigned)def->iFireTime, (uint32_t)ps->commandTime, shotSequence);
    }
    /* The native result already includes any weapon-file reduced-kick window; replace it, not stack on it. */
    native = ps->weaponRestrictKickTime > 0 ?
        (ps->fWeaponPosFrac == 1.0f ? def->adsGunKickReducedKickPercent : def->hipGunKickReducedKickPercent) * 0.01f : 1.0f;
    if (!scale || native <= 0.0f) return;
    if (Cvar_GetInt("developer")) Com_Printf(CON_CHANNEL_DONT_FILTER, "recoil: %s shot at %d ms: %u%%\n", def->szInternalName, ps->commandTime, scale / 100);
    factor = (float)scale / RECOIL_SCALE / native;
    for (i = 0; i < 3; ++i) {
        kick[i] = kickBefore[i] + (kick[i] - kickBefore[i]) * factor;
        gunKick[i] = gunBefore[i] + (gunKick[i] - gunBefore[i]) * factor;
    }
}

static void Activate(const recoil_contract_t *contract)
{
    active = *contract;
    Cvar_SetInt(activeModel, contract->model);
}

void CL_Recoil_Clear(void)
{
    if (!activeModel) return;
    Recoil_Decode(RECOIL_NATIVE_WIRE, &active);
    memset(&shot, 0, sizeof(shot));
    Cvar_SetInt(activeModel, 0);
}

void CL_Recoil_Init(void)
{
    CL_RegisterCapability("cl_recoilProtocol", RECOIL_PROTOCOL, 0, RECOIL_PROTOCOL,
        "Recoil contract protocol (not gameplay model support)");
    CL_RegisterCapability("cl_recoilModels", SupportedModels(), 0, 0x7fffffff, "Bitmask of recoil models this client applies");
    activeModel = Cvar_RegisterInt("cl_recoilModel", 0, 0, 31, CVAR_ROM, "Effective server recoil model");
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
    const int model = Cvar_GetInt("recoil_model");
    recoil_contract_t contract;
    void *file;
    int length;
    if (model <= 0 || !com_sv_running->boolean) return;
    if (model != wireModel) {
        length = FS_ReadFile(va("recoil/model_%d.profile", model), &file);
        if (length <= 0) { Com_PrintWarning(CON_CHANNEL_CLIENT, "recoil_model %d: no recoil/model_%d.profile\n", model, model); return; }
        if (length >= (int)sizeof(wire)) length = sizeof(wire) - 1;
        memcpy(wire, file, length);
        FS_FreeFile(file);
        while (length > 0 && (wire[length - 1] == '\n' || wire[length - 1] == '\r' || wire[length - 1] == ' ')) --length;
        wire[length] = 0;
        wireModel = model;
    }
    if (!Recoil_Decode(wire, &contract) || !Recoil_Compatible(&contract, RECOIL_PROTOCOL, SupportedModels())) {
        Com_PrintWarning(CON_CHANNEL_CLIENT, "recoil_model %d: unsupported profile; native recoil\n", model);
        return;
    }
    Activate(&contract);
}

void CL_Recoil_SystemInfoChanged(const char *systemInfo)
{
    const char *wire = Info_ValueForKey(systemInfo, "sv_recoilContract");
    recoil_contract_t contract;
    if (!activeModel) return;
    CL_Recoil_Clear();
    if (!wire[0]) { ActivateListenServer(); return; }
    if (!Recoil_Decode(wire, &contract) ||
        !Recoil_Compatible(&contract, RECOIL_PROTOCOL, SupportedModels())) {
        Com_Error(ERR_DROP, "Unsupported server recoil contract; install the matching PromodFlex client.");
        return;
    }
    Activate(&contract);
}
