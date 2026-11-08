# Recoil models

Recoil that moves the aim (view kick) is a deterministic simulation shared by the client and the
dedicated server. The server is authoritative: the client sends aim without kick, the server adds its own
simulated kick to each usercmd, and the client predicts the same values for the screen. Design, evidence
and review answers: [authoritative-recoil.md](authoritative-recoil.md);
[verified facts](evidence/2026-11-07-authoritative-recoil-facts.md).

Model IDs are permanent:

- **0/native** is stock CoD4. It is never simulated, and stock clients may play.
- **1/continuous** is the opening ramp: each burst starts at `opening` of the native kick distance and reaches
  full strength on shot `full_shot`.

## Code layout

| File | Role |
|---|---|
| `src/recoil/sim.h` | The simulation and the model interface. Integer state (micro-degrees, milli-degrees/s). The native shot (`BG_WeaponFireRecoil`'s view kick with hashed randomness). Native recovery (`CG_KickAngles` in 5 ms slices on an absolute-time grid). Advance, shot, the usercmd offset, and the correction message format. |
| `src/recoil/engine.h` | Engine rules both sides must read identically: fire events, own and live player, weapon slot, input sampling thresholds, the usercmd fold, gun-kick scaling. |
| `src/recoil/models/*.h` | One file per model. |
| `src/recoil/model.h` | The contract: model registry, wire decoder, capability check, per-weapon profile lookup. |
| `src/cl_recoil.c` (+ `cl_recoil_hooks.asm`) | Client glue. Folds the offset into each new command (`CL_FinishMove`). Records fire events per command (prediction `Pmove` call `0x4476A3`). Drops native view kick and scales gun kick (`0x457D2E`). Strips the offset from the wire (`CL_WritePacket`). Applies `svc_recoil` corrections. Runs the listen-server host's gun kick (`0x4A9CB2`). `cl_recoilStatus` prints the mode and correction counters. |
| `CoD4x_Server/src/sv_recoil.c` | Server glue. Contract and admission. Folds into `lastUsercmd` in `SV_ClientThink`. Handles shots from `G_PlayerEvent` (one retargeted call in `_g_active_mp.asm`), including the gun-kick scale. `svc_recoil` before `svc_EOF`. |

The headers are engine-free and mirrored byte for byte to the server: run
`python tools/sync-recoil.py`, or `--check` in CI (CTest `recoil_sync` with `RECOIL_SERVER_SOURCE`).
The script also generates the Python tooling's `recoil_constants.py`.

## Adding a model

1. Write `src/recoil/models/<name>.h`:
   - **A parameter table** `recoil_param_t <name>_params[] = {{"name", min, max}, ...}`. The wire decoder,
     and the generated Python validator, enforce those bounds.
   - **Optional hooks.** Each runs only for weapons with a profile entry; the others stay native.
     - `validate(param)`: rules that span several parameters.
     - `shot(sim, param, shot, result)`: adjust the native result for one shot. It may scale, replace or
       add to `result->view` (the speeds the shot sets) and `result->gun_scale`, and keep private state
       in `sim->model`.
     - `step(sim, param, step)`: replace one 5 ms recovery slice (the default is `Recoil_NativeStep`).
2. Give it the next permanent ID in `model.h` and a registry line
   `{ID, "name", <name>_params, count, validate, shot, step}`.
3. Add its mod profile `recoil/model_<id>.profile` (`1:<id>:weapon,p1,...,pN/...`) and catalog entry. If the
   model has a `validate` rule, mirror it in the mod's `tools/recoil_contract.py` `CROSS_FIELD_RULES`.
4. Add tests to `tests/test_recoil.c`, run `tools/sync-recoil.py`, and build both engines.

The engine glue does not change. Everything a model reads is in `recoil_shot_t` and `recoil_step_t`:
- the usercmd time (the server clamps it to game time);
- the shooter;
- the shot number: the simulation's count of shots, which picks the native roll;
- the weapon and its fixed-point WeaponDef kick data;
- full and half ADS;
- the native reduced-kick window;
- whether the player is alive.

Any arithmetic must be integer, so both builds agree. The tests assert golden values under both compiler
settings.

Randomness must not depend on anything the client chooses. The command time is the client's choice: a
modified client can hold a shot back a few ms to pick its roll. Hash the shot number instead (`Recoil_Hash`).

## Contract

The server's `recoil_model` (default 0) is locked at the first map; `recoil_status` lists the models.
It publishes one ROM systeminfo value, `sv_recoilContract`: `1:0`, or `1:1:ak47,3500,4,7500/...`.

The wire grammar (version 1) is permanent. Each model's parameter count and bounds come from the
registry. At most 16 weapons and 767 bytes. A weapon entry matches its family (`ak74u` covers
`ak74u_mp` and `ak74u_reflex_mp`).

Clients advertise ROM userinfo values:
- `cl_recoilProtocol`: 2 means it applies the server-authoritative simulation;
- `cl_recoilModels`: a bitmask, non-native only while the hooks are installed.

A server with a non-native model admits only clients with protocol 2 and the model bit. It checks on
connect, on world entry and on every userinfo change. Native servers admit everyone.

**Listen servers** publish nothing (stock server code). The host reads `recoil_model` and its profile
itself and folds the kick into the wire ("local mode"). Other players on a listen server stay native.

## Verification (2026-11-07)

- **Tests:** client CTest `recoil_core` and the server's `make test-recoil`. They also pass built with
  the server's flags (SSE, -O2, LTO) and with x87.
- **In game, listen server:** mp_crash, AK-74u. One record per shot, about 88 ms apart; the fold climbs
  during a spray, and a second burst opens at 35% again.
- **In game, dedicated server:** a local `cod4x18_dedrun.exe` with model 1 and a test client. About 31
  server-simulated shots. The client received 125 corrections, compared 124 with its own prediction,
  and all matched exactly (`cl_recoilStatus`).
- **At 100 Hz (revision 3):** 84 shots, 134 corrections compared, 0 mismatched without lag. With 150 ms
  ping, jitter and 2% loss: 1 mismatch in 113 corrections, a firing command the server never received,
  repaired by the next correction (facts file).
- **Not verified:** feel in a match, multiple players.
