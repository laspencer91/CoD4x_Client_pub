# Volumetric smoke bridge

An optional bridge that lets the CoD4x client draw smoke grenades as a real 3D volume instead
of camera-facing sprites. The renderer is not part of this repository: it is a separately
built, closed `vsmoke.dll` that the client loads only if it is present. This branch carries the
client side only: a small loader, a handful of engine hook points, two client cvars, one status
command and the binary interface the renderer implements.

**Status: custom build, not released.** Nothing built from this branch is meant for players
until the project owner decides so. It has been built and unit tested; it has not yet been run
in a game (see "Remaining work").

## What is in this branch

| File | Purpose |
|---|---|
| `src/vsmoke_api.h` | The renderer ABI (version 1): plain C structs and `__cdecl` function pointers |
| `src/cl_smoke_core.c/.h` | Engine-free helpers: policy lookup, grid path, renderer loading and checks |
| `src/cl_smoke.c/.h` | Engine glue: hook installation, map binding, server policy, cvars, status command |
| `src/cl_smoke_hooks.asm` | Register-preserving stubs for the hook sites |
| `smoke/tests/` | Unit tests for the core and a do-nothing stub renderer |
| `smoke/HANDOFF.md` | Maintainer handoff: design, hook points, ABI, server side, limits, licensing |

The rest of the client changes by four guarded lines (`#ifdef COD4X_VOLUMETRIC_SMOKE`):
`Patch_Other` (install hooks), `CL_InitOnceForAllClients` (cvars and renderer), `CL_Frame`
(per-frame update) and `R_ShutdownD3D` (release device resources).

## Build

The bridge is off by default. With `VOLUMETRIC_SMOKE=OFF` the build is the upstream client:
the touched objects compile to the same bytes as upstream.

```shell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOLUMETRIC_SMOKE=ON \
  -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++
cmake --build build --parallel
```

Options:

- `VOLUMETRIC_SMOKE` (OFF): compile the bridge.
- `VOLUMETRIC_SMOKE_RENDERER_SHA256` (empty): only load a renderer with this SHA-256.
- `VOLUMETRIC_SMOKE_TESTS` (OFF): build `test_cl_smoke_core.exe` and the stub renderers.

With a recent MSYS2 MinGW (GCC 16, mingw-w64 CRT 13) the upstream link fails on
`__mingw_SEH_error_handler` (libmsvcrt's `_beginthreadex` wrapper, pulled in after
libmingw32). This is unrelated to the bridge; add
`-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--undefined=___mingw_SEH_error_handler`.

## Test

```shell
cmake -S . -B build -DVOLUMETRIC_SMOKE=ON -DVOLUMETRIC_SMOKE_TESTS=ON
cmake --build build --target test_cl_smoke_core vsmoke_stub vsmoke_stub_noexport
build/bin/test_cl_smoke_core.exe build/bin
```

The tests cover info-string lookup, policy channel order, grid path validation, renderer
table validation, loading (pinned, unpinned, wrong hash, missing export, missing file) and the
ABI layout. No game is started.

## Install and use

Put the renderer next to the client DLL:

```
bin/cod4x_021/cod4x_021.dll
bin/cod4x_021/vsmoke/vsmoke.dll       renderer
bin/cod4x_021/vsmoke/...              its data files (compiled shaders, noise, notice)
```

Without that folder the client patches nothing and behaves exactly like upstream. With it:

- `r_volumetricSmoke 0|1` (archived, default 1): the player's own switch.
- `r_volumetricSmokeQuality full|half|quarter` (archived, default half): raymarch resolution.
- `/volumetricsmoke_status`: whether the renderer loaded (and why not), the server policy and
  the renderer's own one-line state.

Smoke is replaced only on servers that publish a policy; elsewhere the stock smoke stays:

```text
sets sv_volumetricSmoke "v1"
```

Each map needs an occupancy grid at `maps/mp/<map>.smokegrid`, read through the game
filesystem (loose file or any IWD, including usermaps). See `HANDOFF.md`.

## Remaining work

Runtime validation in a game (hook sites under CoD4x, renderer built against this ABI, device
reset, map change, pure servers, two clients agreeing), the renderer's API v1 build, a stock-map
grid pack, and the server-side cvar registration. See `HANDOFF.md`.
