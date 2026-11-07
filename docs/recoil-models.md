# Server-owned recoil models

2026-11-07 foundation. Only model **0/native** is integrated into gameplay. Model 1 has
a shared calculation and profile assets; native accepted-shot updates, prediction state,
snapshot correction and event integration are still required. Selecting 1 fails with an
explicit server error. It never silently substitutes another model.

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
The normal client build includes `cl_recoil.c`; the server makefile discovers
`sv_recoil.c` through its existing source wildcard. No native recoil patch or
player-state layout change is installed by this foundation.

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
legacy clients without those fields; servers without this contract clear client state
and retain native recoil. The client/server functions are wired, but model-1 admission
ordering has not been exercised in a native multiplayer session.

The 64-bit fingerprint is a compact consistency check, not authentication or a proof
of client code integrity. Runtime model selection remains disabled for a model until
firing, prediction and replication support are implemented and verified together.

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

The AK74u fixture generates scales 2500, 6250, 10000 at accepted-shot times 0, 78,
156 ms. Each shot adds buildup; elapsed simulation time continuously removes it.
Internal heat has 1/1000-basis-point precision and one cadence interval of reserve,
so frame splitting loses no fractional recovery and full-speed firing reaches the
authored endpoint. Only the output multiplier is capped at 10000. These are calculated
impulse multipliers, not guaranteed bullet positions or played balance results.

State belongs to a player/weapon and the prediction checkpoint. Restore it before
replay. Pass accepted-shot sequence IDs, never trigger presses or render-frame counts.
Duplicate IDs return the prior scale without consuming another shot; stale time/IDs
are rejected. The foundation intentionally does not cache this state globally.

## Adding another model

1. Add its permanent descriptor and model-specific validation/dispatch in the shared
   core. Extend/version the payload if its parameter shape differs; retain old decoders.
2. Add its mod profile and manifest entry; update/check profile hashes with
   `tools/update-recoil-manifest.py` in the mod repository.
3. Implement native accepted-shot state, snapshot encoding/restoration, prediction and
   per-shot event scale consumption on both roles. Keep weapon switching/attachments,
   reload, respawn, ADS transitions and simulation time wrap rules explicit.
4. Verify native-tail impulses and random-number handling separately from multiplier
   agreement. Shared multipliers do not synchronize the engine's random stream.
5. Run core/policy tests, native replay and latency/loss tests, then set its registry
   `integrated` flag, synchronize the mirrors and build matching engines.

Tests: configure `RECOIL_TESTS=ON`, build `test_recoil`, run CTest `recoil_core`.
The server's `make test-recoil` exercises actual policy glue with mock host services
and the real engine info-string helpers, including multi-weapon transport. C and Python
validators run against the same fixture corpus. The release repository's `scripts/test-recoil-package.py` checks
synthetic archives; those fixtures are not runnable game binaries or a public release.
