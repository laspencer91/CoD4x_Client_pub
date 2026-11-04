# CoD4x 100 Hz and volumetric smoke

Maintainer context, updated November 4, 2026. The paired client, server and
PromodFlex changes are ready for a controlled private playtest. They have not
been deployed to public servers. An intermittent reconnect failure under severe
network impairment remains open.

## Components and provenance

| Component | Local repository | Responsibility |
|---|---|---|
| Client | `cod4x-smoke` | Snapshot history, packet pacing, optional smoke bridge |
| Server | `../cod4x-100hz-server` | Configured simulation rate, history, timers and capability checks |
| Mod | `../cod4-promod-projects/pmflex` | Promod timing, network settings and HUD permissions |
| Launcher | `../cod4-promod-projects/cod4-promod-main` | Matched runtime installation and packaging |

The client derives from legacy `callofduty4x/CoD4x_Client_pub` at `17182c7`
(21.8), with our existing smoke bridge. The server base is
`callofduty4x/CoD4x_Server` at `ddd65fce1a1093017bad2dbb6840d75d11c0d56f`.
The gameplay base is pmflex `8647c4b`; the product base is `0b8fbb7`.
These are pinned bases, not claims about the latest upstream releases.

This is separate from CODTV's 125 Hz/capsule implementation. The inspected public
CODTV repository supplied documentation, not buildable native engine source.
No CODTV engine changes or new capsule hitboxes were imported here. CoD2 and MWR
informed the timing/history review; their native offsets and prediction behavior
must not be assumed to apply to CoD4.

The renderer is a separate, private project. Only its C ABI and optional loader
belong in the client repository; do not commit its source or HLSL to these forks.

## Configuration and compatibility

- `sv_fps` supports **20 or 100**. Set it before the first map; changing it
  requires a server process restart. Other rates fail explicitly.
- The engine publishes read-only `sv_tickrate_protocol=1` and
  `sv_tickrate_active_ms=50|10` in replicated system/server information.
- At 100 Hz, real clients must advertise protocol 1 and 256 history slots.
  Checks cover initial connection and later userinfo updates. Native bots are
  exempt. Unpatched clients are not allowed on the 100 Hz server.
- `sv_minSnaps` and `sv_minRate` control server floors; `sv_maxRate` remains the
  bandwidth ceiling. The private candidate uses minimum snapshots 100 and
  minimum/maximum rate 100000. Ordinary 20 Hz Promod defaults are preserved.
- The client accepts snapshot settings through 125 and enforces at least the
  active server packet rate on high-rate servers, including LAN connections.
  A configured `cl_maxpackets 30` therefore still sends about 100 packets/sec
  at 100 Hz. Actual client frame availability still matters.
- The launcher installs the client and renderer as a matched set, validates
  manifest hashes and known runtime filenames before writes, and backs up replaced
  files. Smoke runtime files are all-or-none. Candidate launcher minimum: 0.2.17.
  Product app data uses `COD4PROMOD_MW`; launch with `+set protocolversion 021`
  to select the custom DLL instead of allowing the upstream bootstrap updater
  to replace it.

## Timing rules that must survive maintenance

| Area | Rule |
|---|---|
| World simulation | 50 ms at 20 Hz; 10 ms at 100 Hz |
| Snapshot retention | Server 32 slots at 20 Hz, 256 at 100 Hz; client 256 full snapshots outside the fixed native layout |
| Archive | 6200 slots and 128 MiB; byte cursor rebases safely; report oldest decodable time rather than allocated capacity |
| Script waits | Convert seconds using the active step and retain fractional scheduling error; reset carry on notify/frame-end paths |
| Free grenade collisions | Independent 50 ms schedule, preserving baseline integration and trajectories |
| Linked missiles | Follow their link every world tick; throwing-owner references are not links |
| Vehicles | Independent 50 ms schedule, including integration and cooldown timestep reads |
| Items | Trajectory lookahead, duration and reciprocal all use the same world step |
| Ping | Timestamp-bounded recent window, about 1.6 seconds; do not average the entire enlarged history |
| Smoke simulation | Independent renderer clock, 64 Hz; lifetime is elapsed time, not server tick count |
| Objective and mod timers | Elapsed game milliseconds; intentional 50 ms polling remains 50 ms |

Do not replace every literal 50 with the server timestep. Independent subsystem
schedules, polling intervals, animation padding and stance cooldowns have different
purposes. Item collision reconstruction is the opposite case: restoring a 50 ms
duration alone at 100 Hz would project beyond its 10 ms collision endpoint.

The snapshot sidecar preserves native client/server ABI layouts. Stage the delta
base into the native decoder's scratch arrays before calling the unchanged native
decoder; retain complete entity/client states, not only the smaller public snapshot.
Fast restart must reject zeroed server frames as delta bases while retaining valid
in-flight client history. Reset capability caches correctly on lifecycle changes.

The VM fractional-wait hooks have negative-control coverage: removing either
notify/frame-end carry reset breaks scheduling. They are not duplicate cleanup code.
Windows requests and balances a 1 ms timer period; Linux uses 64-bit file offsets
so 32-bit filesystem metadata works on the tested Windows-mounted assets.

## Prediction and interpolation

Own-player input prediction is unchanged: native prediction replays pending user
commands through `Pmove`. Remote players interpolate accepted snapshots.

There was no active fixed 50 ms remote-player extrapolation to preserve in this
fork. The upstream wrapper's attempted trajectory fields are overwritten by the
native conversion. Reordering it would enable new behavior.

The existing client clock targets the latest snapshot minus the actual snapshot
gap minus 5 ms, excluding network transit. That means roughly 55 ms of lookback
with 50 ms snapshots and 15 ms with 10 ms snapshots. This is interpolation delay,
not forward prediction. Do not apply the earlier CoD2 lookahead assumption here.

## Parked: mouse rotation investigation (November 4)

Private tests: client base `ecd7954`, local Promod/Crash, 250 FPS cap,
165 Hz desktop. User reports longstanding choppy mouse turning across mice;
walking and keyboard turning are smooth. Raw-input toggle did not help.

- Fixed deltas through the native mouse accumulator once per command: smooth.
  Captures showed no extra camera hold when input yaw changed.
- Windows `SendInput` produced accepted raw-input reports. At matched target
  speed, 125 updates/sec was choppy; requested 1000 (roughly 950 actual sends/sec)
  was smooth. Injection bypasses physical hardware/connection and some drivers.
- Cursor-maintenance batching did not help; `m_filter 1` helped little, if at all.

Conclusion: input cadence is implicated; **Windows versus device/driver/client
handling remains unresolved**. Observed physical mouse: MX Master 3S/Bluetooth.
Background-listener readings around 7.5 ms cannot establish hardware polling
rate because Windows can throttle/coalesce background delivery.

Next:
1. Timestamp physical reports inside foreground CoD4, including device ID,
   buffered reads, handler cost and command/camera consumption on one QPC clock.
   Entry points: `IN_RawEvent`/`IN_RawInputDrainQueue`, `Sys_EventLoop`,
   `CL_MouseEvent`/`CL_FinishMove`.
2. Compare a known 1000 Hz physical mouse with injected input at matched settings;
   separate arrival gaps from client consumption delays.
3. Investigate CODTV's raw-input/high-polling work and test compatibility/DPI
   settings individually. Its screenshots suggested disabling fullscreen
   optimizations and setting high-DPI override to Application; neither was
   applied or verified here.

All experimental code, test processes, builds and captures were removed.
Evidence is session observations; no trace files were retained.

## Smoke bridge contract

`src/cl_smoke.c` owns engine hooks and lifecycle; `cl_smoke_core.c` owns engine-free
policy/loading helpers. `src/vsmoke_api.h` is the authoritative **API version 2**
contract. Update client and renderer together; version 1 pairings are rejected.
The renderer receives engine data through the ABI and does not patch the executable.

The ABI is 32-bit x86, natural alignment, `__cdecl` function pointers and caller-owned
UTF-8 strings valid during calls. Structure sizes are view 356, frame 36, draw table
20, map 44, host 28 and renderer **80** bytes. `VSmoke_GetRenderer` is the exported
entry point. Required callbacks include `init` and `set_map`; capability flags
require their corresponding draw/technique callbacks.

Hook sites validate expected executable bytes before writing. The composite,
prepare, particle, effect and bullet stubs preserve registers, flags and x87/SSE
state. Material-technique seams at `0x648E9E` and `0x64902C` pass only technique
flags for smoke-aware heat haze. The particle pass temporarily routes calls through
a private copy of the device's complete method table: copying only its 119 public
entries previously crashed on Direct3D's internal entries.

Main-thread calls handle settings, map binding and game events; render-thread calls
handle preparation, particle routing, techniques and composition. The renderer
synchronizes shared state. Release default-pool resources before device reset or
destruction. A same-map restart changes the map generation and clears clouds and
render state even though the map name is unchanged.

Place `vsmoke/vsmoke.dll` and its data beside `cod4x_021.dll`. The bridge is built
with `VOLUMETRIC_SMOKE=ON`; a pinned renderer hash is supported. Missing/incompatible
renderer, invalid policy or missing grid keeps stock smoke. Players can use
`r_volumetricSmoke` and `r_volumetricSmokeQuality full|half|quarter`.

Publish `sets sv_volumetricSmoke "v1"` before loading the map. Policy comes from
raw systeminfo first, then serverinfo; a malformed/duplicate systeminfo policy
must not fall back to serverinfo or a client-writable cvar. The renderer validates
optional `field=value` parameters. Keep the policy within the shared info-string
budget. The policy grammar version `v1` is separate from renderer ABI version 2.

The bridge manages `r_zFeather` when scene depth is required and mirrors its trusted
permission in read-only `cl_volumetricSmokeDepth`. Promod HUD checks use that ROM
value; old writable `promod_smokeDepth`/`promod_networkRate` helpers are not authority.
Depth permission resets when the renderer is inactive or disconnected.

Map grids live at `maps/mp/<map>.smokegrid` and load through the game filesystem.
Pure servers need the matching grid IWD too. Stock packs in `main` need an accepted
prefix such as `jcod4x_`; mod IWDs may also carry grids. Grid format is little-endian
version 2, magic `0x474D5350`, 96-byte header, alternating empty/solid unsigned LEB128
runs in x-fastest order, and a validated decoded-cell hash. Cell size is 50 cm.

Late joiners do not reconstruct smoke detonated before joining. Remote misses
without resolved impacts do not carve; local misses do. Final visual acceptance,
killcam/demo coverage, fullscreen device-reset coverage and other GPU vendors
remain separate from passing event/lifecycle tests.

## Validation and remaining issue

Native/core regression tests cover 256-slot decoding at boundary ages, removals,
dense entities/clients, ABI preservation, packet pacing, fractional waits, missile
and vehicle integration, ping windows and reset/delta fallback.

Real bomb tests passed on Windows and Linux at both rates, for defuse and explosion:
native bots actually held `+activate`. The 45-second fuse took 45000 game ms in all
eight cases. Plant/defuse retained existing polling overhead; round-clock and
replicated HUD deadlines did not run five times faster. The HUD's independent
tenths phase is an inherited cosmetic quirk.

November 4 paired-client tests passed on a pure server: both received 100 snapshots
per second and sent about 100 packets per second. With 300 ms RTT and 2% packet
loss, delivery was 97.5-98.1 snapshots/sec, with matching smoke events and zero
invalid deltas. Smoke cleared on fast restart and HUD permission spoofing was
rejected. A combined loss/restart measurement dipped to 94-95 snapshots/sec and
missed its 95% threshold; the separate steady-state tests passed.

The blackout/reconnect rerun also passed with 300 ms RTT, jitter and loss. An earlier
intermittent stale-session sequence failure remains unresolved; controlled stale
datagrams demonstrated the underlying hazard at 20 Hz too. One passing rerun does
not establish a fix. Treat session isolation as a separate network change rather
than claiming enlarged history fixes it.

Ten-bot shooting/movement and archive wrap passed; retained archive time exceeded
60 seconds. Sixty-four-slot startup/allocation passed on both platforms, but that
is not a 64-player load test. No public release or live-server restart followed.

## Build and repeatable checks

- Client: CMake options `TICKRATE_TESTS=ON`, `VOLUMETRIC_SMOKE=ON` and
  `VOLUMETRIC_SMOKE_TESTS=ON`; run `ctest --test-dir build-tickrate`, the smoke core
  executable and the technique ABI executable. New MinGW CRTs may need linker flag
  `-Wl,--undefined=___mingw_SEH_error_handler`.
- `tests/replay_client_tickrate.py`: compiled sidecar helpers plus the pinned native
  snapshot decoder under Unicorn; requires `pefile` and `unicorn`.
- Server: `make test-tickrate`; `tools/tests/native_tickrate_test.py` exercises the
  compiled Windows/Linux binaries using JSON from `tickrate_layout.c`. Keep the
  native-test binaries unstripped for symbol discovery.
- `tests/run_bomb_timers.py --fps 20|100 --case defuse|expiry`: real headless bomb
  tests, including WSL/Linux. Fixtures in `tests/fixtures` cover bomb, physics,
  retention and paired smoke scenarios.
- Product `scripts/test-cod4-tickrate.py` and `cod4_loopback_proxy.py`: private native
  clients, configurable loss/jitter/blackouts and map/reconnect/smoke restart tests.
  `scripts/test_prepare_cod4_release.py` checks packaging; launcher Rust tests cover
  verified runtime installation.

`TEST_ISOLATION=ON` is for private automated clients only; production builds leave
it OFF. It isolates app data/commands and avoids shared integrations/configuration
downloads. Native client tests need an active Windows desktop for Direct3D. Mute
with **`snd_volume 0`**, close only owned test processes in `finally`, and never
restart or kill unrelated servers. Generated run receipts and diagnostics are not
source files and do not belong in commits.

## Distribution and attribution

Keep each repository's existing license and notices. The client bridge loads the
renderer dynamically; renderer source is not linked into the client. Renderer
binaries and data must retain their supplied license and this attribution:

> Volumetric Smoke System by Logan - contact at logan@loganspencer.dev

The names "ScrapSmoke" and "ProMod X" are not licensed with the renderer. Public
self-built clients omit the closed client-auth module; the private no-auth test
setup does not validate authenticated public-server compatibility.
