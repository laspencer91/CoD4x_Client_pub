# Server-authoritative recoil: design plan

Status: plan for review (2026-11-07). Nothing here is implemented yet.
Repos: `CoD4x_Client_pub` (client, owns the shared core), `CoD4x_Server` (dedicated server),
`promod-econ-na` (profiles, tooling), `cod4-promod` (release scripts).

## 1. Goal

Recoil that moves the aim becomes server-computed and identical on client and server:

- The client sends pure mouse aim. The server adds recoil itself, so a client cannot remove or reduce it.
- The client predicts the exact same recoil for the screen, with no network data per shot.
- Recoil models are plug-ins: adding one is a new self-contained source file, a registry line, and a mod
  profile, with no engine glue changes.

Non-goals: viewmodel ("gun") kick stays native and cosmetic on the client. Bullet spread is untouched.
Stock clients keep working on servers that run model 0 (native).

## 2. How recoil works today (verified)

| Fact | Evidence |
|---|---|
| A shot calls `BG_WeaponFireRecoil`, which **sets** the view-kick angular velocity: pitch = -lerp(PitchMin, PitchMax, r), yaw = lerp(YawMin, YawMax, r), roll = -0.5 x yaw, times the native reduced-kick scale. It uses ADS ranges only when `fWeaponPosFrac == 1.0` and hip ranges otherwise. It **adds** gun kick (viewmodel). `r` comes from the global `randomf`. | server `asmsource/_bg_weapons.asm:2180-2330`; WeaponDef ADS 0x700-0x710, hip 0x748-0x758 (`xassets/weapondef.h:478-500`) |
| `CG_KickAngles` integrates the kick **only on the client**, once per rendered frame over `cg.frametime`, in slices of at most 5 ms. Per axis (pitch, yaw, roll): if angle != 0, velocity -= sign(angle) x centerSpeed x dt, where centerSpeed is the ADS value when `fWeaponPosFrac > 0.5`, the hip value otherwise, and 2400 with no weapon. Then change = velocity x dt, x0.06 when moving toward centre. Crossing zero sets angle = velocity = 0. \|angle\| > 10 deg clamps to +-10 and sets velocity = 0. | client binary `0x44FE20` (constants 0.001 @0x70AF98, -1 @0x70AD40, 0.5 @0x6BE648, 2400 @0x70B670, 0.06 @0x70AEF0, 10 @0x6BDF70) |
| It runs only for the local player (`nextSnap->ps.otherFlags & 4`). Otherwise kick is zeroed each frame. | client binary `0x4529F5`, `0x452A10-0x452A30` |
| The client creates **exactly one usercmd per rendered frame**. Kick reaches the screen only through the command: `cmd->angles = ANGLE2SHORT(viewangles + kickAngles + offsetAngles)`. `offsetAngles` is the ADS view error, which is 0 on every Promod weapon. | `CL_CreateNewCommands` `0x463E00`; `src/cl_main.c:9570-9590`; `src/cg_main.c:1799-1803` |
| The server runs `ClientThink` once per received command. `PM_UpdateViewAngles` sets `ps.viewangles` from the command angles. `FireWeapon` aims from the previous command's angles plus sway (`fGunPitch/Yaw`). Melee uses `ps.viewangles`. The server discards its own view kick. | server `sv_client.c:1127`, `3048-3075`; `_g_active_mp.asm:1244-1729`; `_g_weapon.asm:553-713` |
| Followers, killcam and server demos see `ps.viewangles`, which are recoil-included because the command was. Owners never receive their own viewangles. | server `msg.c:2686-2694`; client `msg.c:2943-2952` |
| `SV_ClientThink` is C. All of a packet's commands are decoded before any is executed. `CL_WritePacket` is C and writes each command from `cl.cmds[]`. The delta base on both sides is `MSG_SetDefaultUserCmd(ps)`. | server `sv_client.c:1055-1131`; client `cl_main.c:4273-4392`; server `msg.c:2506` |

**"Per tick" clarified.** There is no recoil code on a server tick today. The decay runs per rendered
frame on the client only. Because the client makes one command per frame, "per command" and "per frame"
are the same cadence. The server already executes those same commands one by one, so running the decay
per command on both sides keeps today's feel.

## 3. Strategy: fold recoil into the command on each side

Recoil is a deterministic per-player simulation driven only by data both sides see identically: each
command's `serverTime`, the shots fired in it, and the player state at that point.

```
client frame N                                server, command N
--------------                                -----------------
Recoil_Advance(sim, T_N)                      Recoil_Advance(sim, T_N)       (SV_ClientThink, C)
off = Recoil_CommandOffset(sim)               off = Recoil_CommandOffset(sim)
cl.cmds[N].angles = SHORT(aim) + off          cmd.angles = wire + off   -> identical command
wire = cl.cmds[N].angles - off   (CL_WritePacket)
prediction runs cmd N -> fire events          ClientThink -> Pmove -> fire events
hook 0x457D2E -> Recoil_Shot(sim, ...)        scan ps.events -> Recoil_Shot(sim, ...)
```

Consequences:

- Everything downstream of the command is stock, including the bullet trace, melee, spectators, killcam
  and demos. There are no new snapshot fields. The playerState keeps its size, and no server assembly
  changes.
- The client's local commands still carry the recoil, so prediction and the view are unchanged.
- No correction channel is needed. A misprediction, such as a shot the server rejects, only differs until
  recoil returns to rest, and rest is an exact zero on both sides. A debug log on both sides proves
  agreement (section 8).
- Honest limit: a script that pulls the mouse down perfectly ("recoil control") is still possible, as in
  every game with client prediction. Removing recoil itself is no longer possible.

**Listen server (local play):** the server half is the stock binary, so the host's client folds the
offset into the wire too ("local fold"), exactly like today's command. It is the same simulation with a
different fold point.

## 4. Determinism

- **Integer fixed point.** Angle in micro-degrees, angular velocity in milli-degrees per second. Each
  1 ms step adds velocity (in micro-degrees per ms) to the angle, so the step is integer-exact. MSVC
  x87 and GCC produce bit-identical results.
- **1 ms steps on absolute command time.** Results do not depend on how time is split into commands, so
  a lost command cannot desynchronise the decay. The stock game uses 5 ms slices. The difference is
  measured by a test against a float port of `0x44FE20` (section 9).
- **WeaponDef conversion.** WeaponDef floats are converted with `floor(x * 1000.0 + 0.5)` in double,
  which is exact for every float, so both sides get the same integers from the same fastfile.
- **Shot randomness.** Uses `splitmix64` over (domain, shooter clientNum, shot `commandTime`, index of
  the shot in that command, weapon index), as in ProlineUnreal's ScrapCore `counter_hash.h`. It does not
  depend on history, so one mispredicted shot cannot shift later shots. No match seed is needed; a seed
  would not stop a cheat, because the client must know it to predict.
- **Command offset.** `off = round(angle_udeg x 65536 / 360e6)` in integer angle-short units, added mod
  65536. The client sets `cmd = ANGLE2SHORT(aim) + off`, so `wire = cmd - off` is exact.

## 5. Model architecture

Two layers, both engine-free C in `src/recoil/` and mirrored byte-for-byte to the server by
`tools/sync-recoil.py`:

```
recoil/model.h   contract: wire decode/encode, model registry, capability check   (exists, generalised)
recoil/sim.h     simulation: state, native impulse, native recovery, advance/shot (new)
recoil/models/*.h  one file per model, included by the registry                   (new)
```

### Simulation state and inputs

```c
typedef struct { int32_t angle[3], speed[3]; } recoil_kick_t;      /* udeg, mdeg/s; pitch, yaw, roll */
typedef struct {
    uint32_t time;                 /* last advanced command serverTime; 0 = never */
    recoil_kick_t kick;
    uint32_t model_state[4];       /* private to the active model, zeroed on reset */
} recoil_sim_t;

typedef struct {                   /* what the engine glue extracts from WeaponDef (fixed point) */
    int32_t pitch[2][2], yaw[2][2];/* [hip|ads][min|max], mdeg/s */
    int32_t center[2];             /* [hip|ads] deg/s^2 */
    int32_t reduced_percent[2];    /* native reduced-kick percent [hip|ads] */
    uint32_t fire_msec;
} recoil_weapon_kick_t;

typedef struct {                   /* one accepted shot */
    uint32_t time, shooter, index, weapon;
    int ads_full;                  /* fWeaponPosFrac == 1.0 (native range selection) */
    int reduced;                   /* weaponRestrictKickTime > 0 */
    const recoil_weapon_kick_t *def;
} recoil_shot_t;

typedef struct {                   /* recovery inputs for one advance */
    int ads_half;                  /* fWeaponPosFrac > 0.5 */
    int32_t center;                /* chosen center speed, or 2400 without a weapon */
} recoil_step_t;
```

### Model interface

```c
typedef struct recoil_model {
    uint8_t id; const char *name;
    uint8_t params;                                        /* per-weapon integers on the wire */
    int  (*validate)(const int32_t *param);                /* model bounds; wire stays generic */
    /* Adjust the native impulse for this shot (scale, replace, add). Required. */
    void (*shot)(recoil_sim_t *, const int32_t *param, const recoil_shot_t *, recoil_kick_t *impulse);
    /* Optional: replace one 1 ms recovery step; NULL = Recoil_NativeStep. */
    void (*step)(recoil_sim_t *, const int32_t *param, const recoil_step_t *);
} recoil_model_t;
```

The core provides building blocks that models compose: `Recoil_NativeImpulse` (the stock velocity set),
`Recoil_NativeStep` (the stock decay), `Recoil_Hash`, and the opening-ramp state helpers that exist
today.

- Model 0 (native) has no `shot` and is never simulated: it remains stock and trusted.
- Model 1 (continuous) is `shot = native impulse x Recoil_Fire(...)` with its ramp state in
  `model_state`.

A new model (say, a fixed pattern, or a stronger first-shot climb) is one header with `validate` and
`shot` (plus `step` if it changes recovery), one registry entry, and a mod profile. The wire format
`P:M:weapon,p1,...,pn/...` and the decoder stay generic: `params` and `validate` come from the
registry.

### Engine glue (the only engine-specific code)

| Side | Where | Does |
|---|---|---|
| Client | `CL_FinishMove` (`cl_main.c:9570`) | Advances to `cmd->serverTime` using the predicted ps (weapon, ADS fraction), adds `off` to the command, and records `off` per command number |
| Client | `CL_WritePacket` (`cl_main.c:4383`) | Authoritative mode only: writes copies with `angles -= off`, delta-chained on the copies |
| Client | hook `0x457D2E` (exists) | Sim active and own player (`otherFlags & 4`, not following): zeroes the native view-kick output, keeps gun kick, and calls `Recoil_Shot` |
| Client | `CL_Recoil_SystemInfoChanged` (exists) | Picks the mode: native, authoritative (dedicated contract) or local fold (listen server) |
| Server | `SV_ClientThink` (`sv_client.c:3048`) | Non-bot, active, non-native contract. Before the `lastUsercmd` copy: advances to `cmd->serverTime`, adds `off`, and records `eventSequence`. After `ClientThink`: each new event 26/27/38 (at most 4) calls `Recoil_Shot` |
| Server | `sv_recoil.c` (exists) | Contract publication and capability gate, plus per-client `recoil_sim_t` reset on connect |

Both sides fill `recoil_weapon_kick_t` with the same shared inline from the six WeaponDef fields, so
conversion cannot drift.

## 6. Contract and compatibility

- `RECOIL_PROTOCOL` goes from 1 to 2, meaning server-authoritative simulation. Native wire becomes `2:0`.
  Version 1 client-applied model 1 was never released, so it is removed rather than kept.
- The client advertises `cl_recoilProtocol=2` and `cl_recoilModels` (a bitmask, only while the hook is
  installed). A dedicated server with a non-native model refuses clients without the protocol or model.
  Stock clients join model-0 servers as before.
- Model IDs and per-model parameter lists remain permanent contracts. Model 1 keeps
  `weapon,opening,full_shot,recovery`.

## 7. Lifecycle and edge cases

- **Reset:** `serverTime` going backwards (map change or restart), connect and disconnect. Otherwise the
  simulation settles to exact zero by itself.
- **Long gaps:** while at rest, `time` jumps forward. While moving, the advance is capped at 2000 steps,
  then the state is zeroed. Both sides do the same.
- **Spectating:** the client simulation ignores followed players' events. Followers see the followed
  player's server-folded `ps.viewangles`.
- **Bots:** not simulated, as in stock (bots never had view kick).
- **Server-skipped commands** (duplicates and before-restart) are never folded on the server. The client
  only folded commands the server did execute, except for lost ones, which is bounded misprediction.
- **Mid-command weapon or ADS changes:** the step inputs come from the player state at command start on
  both sides (the client's last prediction, which equals the server's ps before `ClientThink`), and shot
  inputs come from the ps after the command.
- **Four-event cap:** both sides see at most 4 new events per command, the stock limit.

## 8. Verification hooks

`developer 1` on both sides prints `recoil: t=%u shot=%u off=%d,%d hash=%08x` per shot. With a local
dedicated server and one client, the two logs must match line for line. A small diff script makes
that a repeatable test.

## 9. Stages

1. **Core** (shared headers + tests):
   - fixed-point native impulse and recovery;
   - generalised registry and wire;
   - model 1 as the first plug-in.

   Tests:
   - golden hash values;
   - split invariance (advancing in different command splits gives the same state);
   - exact fold round trip;
   - a float port of `0x44FE20` at 5 ms against the integer simulation (peak and settle within tolerance);
   - model 1 opening ramp.

   Run on client `ctest` and server `make test-recoil`.
2. **Client, local fold** on a listen server. Replaces today's kick-rescale path. Playtest: model 1
   with a neutral profile against model 0 should feel the same; then the opening ramp.
3. **Server and authoritative mode:**
   - `SV_ClientThink` glue;
   - client wire subtraction;
   - protocol 2 gate;
   - local dedicated server against client log diff.
4. **Tooling and docs:**
   - mod `recoil_contract.py` generalised parameters and constants;
   - release scripts sync;
   - `docs/recoil*.md`.
5. `/simplify` across the diff.

## 10. Open questions for review

1. Is 1 ms integer stepping close enough to stock 5 ms float slicing to feel identical, or should the
   simulation keep 5 ms slices aligned to absolute time?
2. Shot time: the client uses predicted `ps->commandTime` at the hook. Is it always the command that
   fired the shot (one command per frame)?
3. Do the server's skipped or clamped command paths (`serverTime` clamp in `ClientThink_real`, 200 ms
   msec cap, the 66 ms Pmove split) change anything when the simulation uses raw `cmd.serverTime`?
4. Should the ADS view error (`offsetAngles`) become part of the simulation for non-Promod weapons?
