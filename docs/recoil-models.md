# Server-owned recoil models

Models **0/native** and **1/continuous** are integrated.

View kick only moves the shooter's own aim: the cgame's local-player `CG_FireWeapon` path calls
`BG_WeaponFireRecoil` (`0x41A7B0`, call site `0x457D2E`) and adds the result to `cg.kickAVel` /
`cg.vGunSpeed`; the server's `G_PlayerEvent` computes the same values and discards the view kick
(source inspection of `iw3mp.exe` 1.7 and the server's `_g_active_mp.asm`). So a model needs no
player-state, prediction or snapshot change. `cl_recoil.c` replaces that call
(`cl_recoil_hooks.asm`), runs the native function, then scales the shot's kick by the model's
multiplier. Direction and randomness stay native. The hook checks the call bytes before patching,
and the client advertises non-native models only while it is installed.

The engine's per-burst counter `weaponShotCount` only counts burst-fire weapons, so model 1 keeps
its own per-shot state on the client, keyed to the current weapon and reset when the weapon changes
or time goes backwards.

## Ownership and selection

The dedicated server registers `recoil_model`, default 0. Set it before the first map;
the first successful selection locks for the process lifetime. Restart the process to
change it. `recoil_status` lists the requested/effective selection, protocol, supported
model mask and contract fingerprint. Model IDs are permanent: 0/native, 1/continuous;
a later model receives a new ID rather than changing model 1's meaning.

`src/recoil/model.h` is the canonical engine-free C core and contract decoder.
`tools/sync-recoil.py` synchronizes the tracked server core, shared C/Python validation
corpus and the mod-owned Python validator mirrored into release tooling. Run `--check`
when testing/building a matched set, or select repos with `--server`, `--mod`, `--release`.
Set `RECOIL_SERVER_SOURCE` in CMake to enable the `recoil_sync` CTest. Do not edit mirrors.
The normal client build includes `cl_recoil.c` and its hook; the server makefile discovers
`sv_recoil.c` through its existing source wildcard. No player-state layout change is needed.

## Server/client agreement

The server publishes one protected `sv_recoilContract` systeminfo value, containing
both model and all profile values. A client's local configuration is not an input.
The client reads the raw configstring, validates the complete value, rejects unsupported
models and acknowledges its fingerprint in ROM/userinfo `cl_recoilAck`.

ROM/userinfo `cl_recoilProtocol` identifies the contract reader; `cl_recoilModels` is
the bitmask of **fully integrated** gameplay models. Both currently advertise only
native gameplay (mask 1). Protocol support alone is insufficient. Connection checks
use capabilities; world entry and active userinfo changes also require the effective
contract acknowledgement when a non-native model is active. A pending or old acknowledgement
holds a capable client in the primed state; active userinfo mismatches reject it. Native servers permit
legacy clients without those fields. Without a contract (any other server) the client keeps native
recoil, except as a listen-server host: the stock listen server publishes nothing, so the host reads
`recoil_model` and `recoil/model_<id>.profile` itself, once per game, like a dedicated server does.
Model-1 admission on a dedicated CoD4x server has not been exercised in a multiplayer session.

The 64-bit fingerprint is a compact consistency check, not authentication or a proof
of client code integrity.

## Profiles and continuous model

Mod source owns `recoil/models.json` and `recoil/model_<id>.profile` for non-native models. These are **server-only loose files**
at `mods/pmflex/recoil/`; client IWDs do not contain tuning data. Server-only
`recoil-native.cfg` and `recoil-continuous.cfg` ship beside the IWD. Native has no profile file. The server reads the selected profile;
the client consumes the published payload, never its own profile copy. Keep authoring
files and runtime support separate: packaging a profile does not enable its model.

The v1 payload `1:0` is the permanent native contract, including on newer engines.
A newer reader retains v1 support; capability checks compare its supported version
to the contract version, rather than requiring equal version ceilings. Model 1 uses
`1:1:ak74u,2500,3,7500[/weapon,opening,full_shot,recovery]`. Multipliers use basis
points (10000 = full strength), and recovery is basis points per second. Reject
unknown model/version, duplicate weapons, noncanonical/overflowing numbers and invalid
bounds. Maximum payload is 767 bytes, with at most 16 weapon entries.

The PromodFlex profile (`opening 2500, full_shot 4`) generates 2500, 5000, 7500, 10000 at the
AK74u cadence; the original fixture (`full_shot 3`) generates 2500, 6250, 10000. Each shot adds buildup; elapsed simulation time continuously removes it.
Internal heat has 1/1000-basis-point precision and one cadence interval of reserve,
so frame splitting loses no fractional recovery and full-speed firing reaches the
authored endpoint. Only the output multiplier is capped at 10000. These are calculated
impulse multipliers, not guaranteed bullet positions or played balance results.

The client calls `Recoil_Fire` once per fired shot with the predicted `commandTime` and a shot
sequence; duplicate IDs return the prior scale and stale time is rejected (the client then starts a
new burst). Runtime check 2026-11-07 (listen server, mp_crash, AK74u, about 91 fps): the hook ran
once per shot at 78-88 ms intervals; replaying those times gives 25, 49, 73, 98, 99... percent, and
a burst after a 2.5 s pause opens at 25% again. Frame quantization (88 ms between shots against a
78 ms cadence) costs 1-2% at full strength. Feel in a match has not been evaluated.

## Adding another model

1. Add its permanent descriptor and model-specific validation/dispatch in the shared
   core. Extend/version the payload if its parameter shape differs; retain old decoders.
2. Add its mod profile and manifest entry; update/check profile hashes with
   `tools/update-recoil-manifest.py` in the mod repository.
3. Apply it in `CL_Recoil_FireRecoil` (one call per fired shot). Keep weapon switching,
   respawn and time-wrap rules explicit.
4. Run the core/policy tests and an in-game check, then set its registry `integrated` flag,
   synchronize the mirrors and build matching engines.

Tests: configure `RECOIL_TESTS=ON`, build `test_recoil`, run CTest `recoil_core`.
The server's `make test-recoil` exercises actual policy glue with mock host services
and the real engine info-string helpers, including multi-weapon transport. C and Python
validators run against the same fixture corpus. The release repository's `scripts/test-recoil-package.py` checks
synthetic archives; those fixtures are not runnable game binaries or a public release.
