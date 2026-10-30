# Runtime test results, 2026-10-30

First runs of the bridge inside a game. All processes ran from a private copy of the game files,
bound to 127.0.0.1 / ::1 on ports 28970-28978, and were stopped by their own process ids. Clients
were `TEST_ISOLATION` builds (app data from `COD4X_APPDATA_DIR`, no Steam, Discord or registry
associations, commands through `COD4X_TEST_COMMANDS`). The machine was used over Remote Desktop:
DirectSound fails there ("Error initializing direct sound instance!"), the game continues silently.
GPU: AMD Radeon RX 9060 XT. Server: a copy of CoD4x 21.2 (build 1154) without its security module.

| Run | What | Result | Evidence |
|---|---|---|---|
| R0 | renderer through ABI v1 without the game (hidden D3D9 window) | pass | 28/28 checks; cloud covers 2299 of 3600 sampled centre pixels; bullet hole visible |
| R1 | client built with the bridge off joins the 21.2 server, quits | pass | server "Going from CS_FREE to CS_CONNECTED for VSmokeA"; clean quit; 17 s |
| R2 | bridge on, no renderer | pass | `Volumetric smoke: off (no vsmoke\vsmoke.dll next to the client)`; 15 s |
| R3 | stub renderer in play, vid_restart, quit | pass | `fx=261 bullets=3 composites=1047`, after vid_restart `fx=746 composites=1504`; 63 s |
| R4a | policy from serverinfo (`sets sv_volumetricSmoke "v1"` in a server cfg) | pass | `Volumetric smoke: server policy v1`; 15 s |
| R5 | real renderer, stock rules, mp_crash grid from an IWD | pass | `map=mp_crash grid=loaded policy=valid active=1 ready=1 clouds=1`; light grid 26009 probes; 86-98 s |
| R5u | real renderer, Map Kit usermap mp_mkhot, grid from the usermap's own IWD | pass | `occupancy grid loaded for mp_mkhot (56x56x22)`, light grid 3091 probes; 16 s |
| R6 | two clients, one detonation | not completed | both clients joined, both `ready=1` with the same policy and grid; the scripted team/class/throw did not spawn a player, so no shared grenade was thrown |
| R7 | wrong pinned hash, missing export, no policy, map without grid | pass | each reports its reason and keeps stock smoke; 60 s |

## Found and fixed during the runs

- The particle-draw routing copied only the 119 public `IDirect3DDevice9` entries. Direct3D calls
  further internal virtual functions through the same table, which crashed in `d3d9.dll`
  (second-chance access violation at an address read from beyond the copy). The bridge now copies
  the device's whole method table (every entry that points into the same module).
- `+sets` on the server command line did not reach serverinfo; a `sets` line in a server config does.
- Test builds only: the named "MainThreadSuspendingOnBeginFrame" event made a second client on the
  same PC stall; the stock "wait" command does not delay start-up config commands; the app data
  folder had no override. All three are handled under `TEST_ISOLATION`.

## Environment findings (not bridge defects)

- A `+devmap` listen server in this public 21.8 build fails after map load with
  `Hunk_FreeTempMemory: bad magic`, and `+devmap` on the 21.2 server copy stalls after loading
  `common_mp`. Runs use `+map` with `sv_cheats 1`.
- The 21.2 server exits right after queueing the map when `fs_game` names a folder without a
  `mod.ff`.
- CoD4x loads IWDs from `main` only if they are named `iw_*`, `localized_*` or `jcod4x_*`; a stock
  grid pack in `main` must use the `jcod4x_` prefix (or live in a mod folder or the app data
  `main` folder).
- A CoD4x server refuses a second connection from the same address while an earlier one is still
  loading ("An uncompleted connection ... has been detected").
- D3D9 cannot create a device on a separate (non-input) desktop, so a test client cannot be fully
  hidden; the harness moves its windows off-screen without activating them.
- The client also contacts `filter.cod4x.ovh` and resolves `cod4master.activision.com` when it
  connects.
- A Promod-based mod forces `r_zFeather 0` and shows "SET R_ZFEATHER TO 0" when the bridge turns
  it on for scene depth.

## Not covered yet

Two clients agreeing on one thrown grenade (R6), real grenade detonations through `fx_spawn` (only
renderer-side spawns and ambient effects were exercised), killcam and demo playback, device reset
by alt-tab in fullscreen, `sv_pure 1`, other GPU vendors.
