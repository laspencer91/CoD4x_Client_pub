# Volumetric smoke for CoD4x: maintainer handoff

This branch adds an optional, off-by-default bridge to the CoD4x client so that a separately
distributed renderer can draw smoke grenades as a 3D volume. It is offered for review and
possible upstream integration. It is a custom build today; nothing ships to players from it
without the project owner's decision.

- Base: `CoD4x_Client_pub` master at `17182c7` (tag 21.8, "Bump version: fix keyboard layout
  switching", 2026-09-23).
- Branch: `feature/volumetric-smoke`, one logical commit per step.
- Renderer: closed, prebuilt `vsmoke.dll` plus compiled shaders and data. Its source is not
  part of this handoff; the contract between the two is `src/vsmoke_api.h`.

## 1. What it does and why it is fairer

Stock CoD4 smoke is a cloud of camera-facing sprites. Every client draws them from its own
camera, so the visible edge, the thin spots and what a sprite hides differ between the
thrower, the defender and a spectator, and depend on graphics settings.

The renderer instead simulates the cloud on an occupancy grid of the map (which 50 cm cells
can hold smoke) and raymarches it as a volume against the scene depth. Every client derives
the same cloud from the same detonation point, time, grid and server policy, so density along
any line of sight is the same for everyone. The cloud fills rooms and doorways instead of
clipping through walls. Bullets and grenade blasts carve temporary holes, and those holes are
the same for everyone who saw the shot.

The server decides whether volumetric smoke is used and how it looks and behaves (size,
growth, lifetime, carve sizes) through one replicated policy string. A client cannot change
the policy; it can only turn the feature off for itself (which shows the stock sprites) or
lower the raymarch resolution.

## 2. Shape of the change

```
cod4x_021.dll (this branch)                        vsmoke\vsmoke.dll (closed, prebuilt)
------------------------------------------         -------------------------------------
cl_smoke.c        engine glue, hooks, cvars  --->   simulation, carving, raymarch, compositing
cl_smoke_core.c   loader, policy, paths      <---   host services: fs_read, cvar_string
cl_smoke_hooks.asm register-preserving stubs
vsmoke_api.h      the ABI both sides compile against
```

Upstream code changes by four guarded calls:

| File | Function | Call |
|---|---|---|
| `src/sys_patch.c` | `Patch_Other` | `CL_Smoke_Patch()` after `Gfx_PatchRB_ExecuteRendererCommandsLoop()` |
| `src/cl_main.c` | `CL_InitOnceForAllClients` | `CL_Smoke_Init()` after the command registrations |
| `src/cl_main.c` | `CL_Frame` | `CL_Smoke_Frame()` after `CL_DiscordFrame()` |
| `src/r_main.c` | `R_ShutdownD3D` | `CL_Smoke_DeviceLost()` before the engine releases its resources |

All four are inside `#ifdef COD4X_VOLUMETRIC_SMOKE`, set by the CMake option
`VOLUMETRIC_SMOKE` (OFF). With the option off, `cl_main.c`, `r_main.c` and `sys_patch.c`
compile to byte-identical objects compared to upstream (checked with the MinGW toolchain below).

Runtime switches, from outermost:

1. Build option off: no bridge at all.
2. No `vsmoke\vsmoke.dll` next to the client DLL: `CL_Smoke_Patch` returns before writing
   anything; the engine is untouched.
3. A hook site does not hold the expected stock bytes (for example another patch got there
   first): no hook is installed, the status command says which site.
4. Renderer fails to load, has the wrong SHA-256 (if pinned), wrong ABI version or fails
   `init`: hooks stay installed but call nothing; stock smoke.
5. The server publishes no policy, or the map has no grid: the renderer keeps stock smoke.
6. `r_volumetricSmoke 0`: the player keeps stock smoke.

## 3. Hook points

All sites are in the stock 1.7 `iw3mp.exe` (the image CoD4x patches). `CL_Smoke_Patch`
compares every site with the expected original bytes (or call target) before writing.
None of these addresses is patched by the public client (checked against every address in
`src/`); the closed `ApplyAntiHackHooks` of the official build was not available to check.

| Site | Original | Hook | Purpose |
|---|---|---|---|
| `0x64AD70` | `call 0x64ACD0` (3D scene draw) | `VSmoke_Stub_Composite` | after the scene: `composite(frame, view)`; view = `ESI` (`GfxViewInfo*`) |
| `0x64B2F7` | `call 0x64AD80` (distortion pass) | `VSmoke_Stub_Prepare` | after it: `prepare(frame, view)`; view = `EDI` |
| `0x64B372` | `call 0x64AEF0` (emissive/particle pass) | `VSmoke_Stub_Particles` | brackets it: `particles_begin` / `particles_end` |
| `0x4A1150` | function entry, 8 bytes | `VSmoke_Stub_FxSpawn` (jmp) | every effect spawn: `fx_spawn(name, time, origin)`; nonzero suppresses the stock effect (returns NULL) |
| `0x5F9C1E` | `mov [ebx+13A8h], dl` (`GfxViewInfo.needsFloatZ`) | `VSmoke_Stub_SceneDepth` (jmp + nop) | forces float-Z scene depth while smoke is active |
| `0x4587B0` | `call 0x458720` (tracer decision, world hit) | `VSmoke_Stub_WorldBullet` | other players' resolved shots |
| `0x458D6F` | `call 0x458720` (tracer decision, flesh hit) | `VSmoke_Stub_FleshBullet` | same, flesh path |
| `0x455ADD`, `0x455CFD`, `0x455E2B` | `call 0x4558E0` (bullet trace) | `VSmoke_Stub_SimulatedTrace` | the local player's traces, including misses and penetration |
| `0x5F5278` | `call 0x5F3FD0` (release before `IDirect3DDevice9::Reset` in the lost-device path) | `CL_Smoke_ReleaseForReset` | `device_lost()` before the reset |

Every stub saves the integer registers, flags and the x87/SSE state (`fxsave`) around the C
callback, so the interrupted engine code sees what it would have seen without it. The
argument layouts at the bullet and effect sites come from the stock-1.7 prototype that drew
volumetric smoke on these sites before this port, where they were exercised at runtime.

Engine data read by the bridge (stock addresses, most already named in the client headers):
`r_dx.device`, `gfxCmdBufSourceState.input.codeImages[18]` (float-Z), the scene render target
(`0xD573EB0`, index `R_RENDERTARGET_SCENE`), the distortion-pending flag (`0xCC9F3BC`),
`rgp.world->lightGrid`, `cgs.mapname`, `cg.time`, `cg.clientNum`, `cgEntities[n].nextState.weapon`
and `BG_GetWeaponDef()` (CoD4x's relocated weapon table, not the stock one).

**Particle draws.** If the renderer sets `VSMOKE_WANTS_PARTICLE_DRAWS` and returns a table
from `particles_begin`, the bridge swaps the device object's method-table pointer to a private
copy with entries 81..84 (`DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`,
`DrawIndexedPrimitiveUP`) replaced, and restores the original pointer in `particles_end`. Only
that device object changes and only for the emissive pass; `d3d9.dll`'s code and its shared
table are never written. A maintainer who prefers an engine-level draw hook can replace this
without changing the ABI.

## 4. Threads and call order

- Main thread (`CL_Frame`): `set_settings` when the policy, quality or switch changes;
  `set_map` when `cgs.mapname` changes while `CA_ACTIVE` (NULL on disconnect);
  `pop_console` (up to 8 lines a frame, printed with `^3Smoke:`); `wants_scene_depth` once per
  frame.
- Main thread (client game): `fx_spawn`, `bullet`.
- Render backend thread: `prepare`, `particles_begin`, `particles_end`, `composite`.
- `device_lost`: from `R_ShutdownD3D` (main) and from the lost-device reset path.

The renderer must lock its own shared state between the main and render threads.

## 5. Renderer ABI (version 1)

Defined in `src/vsmoke_api.h`. 32-bit x86, every function pointer `__cdecl`, natural alignment,
only 32-bit scalars, floats and pointers. Every struct starts with its size; fields are only
appended; `VSMOKE_API_VERSION` changes only for incompatible changes. The DLL exports one
undecorated symbol:

```c
const vsmoke_renderer_t *__cdecl VSmoke_GetRenderer(uint32_t api_version);  /* NULL if unsupported */
```

Sizes and offsets every renderer build must match (asserted by the tests under GCC and checked
with MSVC x86):

| Struct | Size | Notes |
|---|---|---|
| `vsmoke_view_t` | 356 | `origin` at 260, `time` at 320, viewport `x` at 340 |
| `vsmoke_frame_t` | 36 | device, float-Z texture, scene colour/depth, size, distortion flag, native draws |
| `vsmoke_draw_table_t` | 20 | four native draw entry points |
| `vsmoke_map_t` | 44 | BSP name, grid path, `GfxLightGrid*` (56 bytes, IW3 layout), world bounds |
| `vsmoke_host_t` | 28 | `fs_read`, `fs_free`, `cvar_string`, data directory, host name |
| `vsmoke_renderer_t` | 76 | `init` at 20, `device_lost` at 72 |

Required entries: `init`, `set_map`. Everything else may be NULL. The loader checks the
version, the table size, the required entries and that a renderer asking for particle draws
provides `particles_begin`/`particles_end`.

The renderer must not patch the executable, read engine addresses, hook Direct3D or run
console commands. It receives the device, view, depth texture, light grid and file contents
through the ABI and reports through `pop_console`/`status`.

## 6. Server policy

One string, `sv_volumetricSmoke`, replicated to every client. The client reads the raw
configstrings, not a cvar the player could set: systeminfo (`CS_SYSTEMINFO`) first, then
serverinfo (`CS_SERVERINFO`). A malformed or duplicated systeminfo entry does not fall back to
serverinfo. An empty or missing policy keeps stock smoke.

Today, on any CoD4x server and without a server change, an admin publishes it into serverinfo:

```text
sets sv_volumetricSmoke "v1 width=1.3 bloom_time=3 carve_scale=0.8"
```

**Server-side change proposed for CoD4x_Server** (small, separate PR): register the cvar as a
systeminfo string so `set` replicates it and GSC can change it with `setDvar`, for example in
`SV_Init`:

```c
Cvar_RegisterString("sv_volumetricSmoke", "", CVAR_SYSTEMINFO, "Volumetric smoke policy for clients that support it (empty: stock smoke)");
```

Optionally validate the `v1` grammar server-side so an admin sees a typo immediately. Keep
the value short: serverinfo is limited to 1024 characters shared with other keys.

Grammar: `v1` followed by space-separated `field=number` pairs; unknown fields, duplicates or
out-of-range values make the whole policy invalid (stock smoke, the client log says why).
The renderer owns the field list. The CoD2 build of the same renderer uses these fields, and
the CoD4 build is expected to keep them, with weapon classes renamed for CoD4:

| Field | Default | Range | Effect |
|---|---|---|---|
| `width` | 1.2 | 0.5 to 1.5 | mature cloud radius multiplier |
| `height` | 1.0 | 0.5 to 2.0 | vertical radius on top of width |
| `bloom_rate` | 512 | 64 to 32768 | new cells per second while growing |
| `bloom_time` | 0 | 0 to 30 | seconds to full size; overrides `bloom_rate` when above 0 |
| `thicken` | 0.5 | 0.05 to 3 | seconds for a reached cell to fill |
| `duration` | 48 | 10 to 90 | cloud lifetime from detonation |
| `fade_time` | 12 | 1 to 30 | final fade; below `duration` |
| `carve_scale` | 1.0 | 0.5 to 2.0 | multiplies every carve radius |
| `carve_<class>` | per class | 1 to 20 | hole radius per weapon class (CoD4 classes to be fixed by the renderer: pistol, smg, assault, lmg, sniper, shotgun) |

plus look fields (`density`, `brightness`, `variation`, `shadow_strength`, `lightgrid_strength`,
`edge_softness`, `height_gradient`, `billow_*`, `wisp_*`, `rise_speed`, `tint_*`). The final
CoD4 list ships with the renderer build.

## 7. Map occupancy grids

Path: `maps/mp/<map>.smokegrid`, read through the game filesystem with `FS_ReadFile`, so it
works as a loose file or from any IWD, including a usermap's own IWD, and honours `sv_pure`.
`VSmoke_GridPath` derives it from `cgs.mapname` (`maps/mp/<map>.d3dbsp`) and accepts only
`[A-Za-z0-9_-]` map names. A map without a grid keeps stock smoke.

- Usermaps: the map tool writes the grid into the usermap IWD, stored, next to the BSP.
- Stock maps: a grid pack IWD (all stock MP maps). On `sv_pure 1` servers the pack must also
  be on the server (as any IWD), or the client will not see it.

File format (little endian, version 2), 96-byte header followed by the payload:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | magic `0x474D5350` ("PSMG") |
| 4 | u32 | version, 2 |
| 8 | u32 | header size, 96 |
| 12 | u32 x3 | `nx, ny, nz` cell counts, each 1..65535, product at most 16,000,000 |
| 24 | f32 x3 | origin (world units, minimum corner) |
| 36 | f32 | cell size (19.685039, 50 cm) |
| 40 | f32 x2 | core and surface fractions used by the baker |
| 48 | u32 | contents mask baked (solid, glass, bullet clip) |
| 52 | u32 | encoding, 1 = runs |
| 56 | u32 | bounds source (0 collision, 1 hints) |
| 60 | u32 | reserved, 0 |
| 64 | u64 | collision fingerprint (FNV-1a of the baked collision snapshot) |
| 72 | u64 | content hash (FNV-1a of the decoded cell bits) |
| 80 | u64 | solid cell count |
| 88 | u64 | payload size; file size = 96 + payload size, at most 16 MiB |

Payload: alternating runs of empty and solid cells in x-fastest order (x, then y, then z),
starting with empty, each run length an unsigned LEB128 varint. The loader rejects any file
whose decoded cells do not hash to the header's content hash.

The header is already versioned and self-validating; no change is needed for this bridge.
A possible version 3 could store the BSP checksum in the reserved field so a stale grid for a
rebuilt map of the same name is refused.

## 8. Joining servers with a self-built client

`CoD4x_Client_pub` builds a client without the closed `client-auth` module (`OFFICIAL` off).
Per the upstream README such a client joins only servers without client authentication.
Server source: from commit `3211d86` (2022-10-05, after the 21.2 release) a server started
with `sv_noauth 1` skips the security module. A 21.2 server has no `sv_noauth`; it accepts
the client only when its security module (`steam_api.dll` next to the server) is not loaded,
because `SV_ConnectSApi` then returns success. Either way, such a server runs without the
CoD4x anticheat and GUID authentication for every player. This is the main reason the feature
belongs in the official build if it is to reach public servers.

The client side of this branch builds and its exports (`WinMain@16`, `GetCoD4xVersion`) match
what the CoD4x launcher calls. When the launcher is started without
`+set protocolversion 021`, its updater replaces a client DLL whose `GetCoD4xVersion` is older
than the latest `CoD4x_Client_pub` release.

## 9. Building and testing

See `smoke/README.md`. In short:

```shell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOLUMETRIC_SMOKE=ON -DVOLUMETRIC_SMOKE_TESTS=ON
cmake --build build
build/bin/test_cl_smoke_core.exe build/bin
```

To exercise the client with the prebuilt renderer: put `vsmoke\` (renderer, compiled shaders,
noise, notice) next to `cod4x_021.dll`, start a local server with
`sets sv_volumetricSmoke "v1"` on a map that has a grid, connect, and run
`/volumetricsmoke_status`. The stub renderer from `smoke/tests` can stand in for the real one
(rename `vsmoke_stub.dll` to `vsmoke\vsmoke.dll`): it draws nothing, never suppresses stock
smoke, and its status line counts effect spawns, bullets and composites, which shows that each
hook fires.

Verified so far (2026-10-30, MSYS2 MinGW GCC 16.1, NASM 2.16.03, CMake 4.4, Ninja 1.13):

- upstream `17182c7` and this branch build; the branch with `VOLUMETRIC_SMOKE=ON` links;
- `-Wall -Wextra` is clean for `cl_smoke.c` and `cl_smoke_core.c`;
- 66 unit checks pass (no game started);
- MSVC x86 (`cl /std:c11`) compiles the same ABI size/offset assertions;
- with the option OFF the touched objects are byte-identical to upstream;
- every hook site holds the expected bytes in the stock 1.7 executable
  (SHA-256 `f50f8a52…a391a5`, the large-address-aware build the CoD4x installer provides).

Not verified: anything at runtime. No game client or server was started for this work.

## 10. Known limits

- Runtime behaviour under CoD4x is unverified: that every site fires, that suppression of the
  stock effect is safe for every caller of the effect spawn, device reset, `vid_restart`, map
  change, killcam and demo playback, `r_smp_backend` on and off.
- Clouds are simulated per client from the observed detonation. Late joiners do not see clouds
  thrown before they joined; there is no server-owned detonation event yet.
- Remote players' misses do not carve (only shots that resolve to an impact do); the local
  player's misses do.
- The distortion (heat haze) pass is seen only as a "pending" flag; the prototype's extra
  seams for distortion technique tagging and copy timing are not part of ABI version 1.
- Float-Z scene depth is requested for every 3D view while smoke is active; a mod that sets
  `r_zFeather 0` may still leave depth unavailable (the renderer then keeps stock smoke).
- The device is created with `Direct3DCreate9` (not Ex) by the public client; a renderer
  using `D3DPOOL_MANAGED` would need changes for a 9Ex device.
- One renderer DLL per client build; the SHA-256 pin is optional.

## 11. Open questions for the maintainer

1. Naming: `vsmoke`, `r_volumetricSmoke*`, `sv_volumetricSmoke`, `/volumetricsmoke_status`.
2. Where the renderer lives in an official release (next to the versioned client DLL here).
3. Whether the official build's anti-hack hooks touch any site in section 3.
4. Whether to keep the method-table swap for particle draws or replace it with an engine hook.
5. The server-side `sv_volumetricSmoke` registration (section 6).
6. Whether official builds should pin the renderer SHA-256 and sign it.
7. Whether stock-map grids ship with the client release or as a server-side IWD.

## 12. Licensing and provenance

- **CoD4x_Client_pub** (`LICENSE.md`): "The following conditions apply:
  https://support.activision.com/license" plus an MIT-style warranty disclaimer. That page is
  an index of Activision's per-game end-user licence agreements. It sets no open-source or
  copyleft obligation and states no explicit redistribution grant. The bridge in this branch
  is offered under the same terms as the repository it is proposed to.
- **CoD4x_Server** is AGPL-3.0 (its README); the proposed one-line server change falls under it.
- **The renderer** (`vsmoke.dll`, compiled shaders, noise texture, grid packs) is not in this
  repository and is distributed as binaries only, under terms set by its copyright holder.
  The binaries must ship with this attribution, which the renderer's licence requires in every
  copy:

  > Volumetric Smoke System by Logan - contact at logan@loganspencer.dev

  and must not be presented as the work of anyone else. The names "ScrapSmoke" and "ProMod X"
  are not licensed with it.
- The bridge only loads the renderer at run time through a documented C interface; no
  renderer code is linked into or distributed with the client.

## 13. Not included

- Renderer source and HLSL, the occupancy baker and the grid packs.
- Any launcher, installer or packaging.
- Server-side changes (proposed in section 6 only).
- Changes to the official build's closed modules.
