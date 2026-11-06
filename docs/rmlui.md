# Optional RmlUi host

The first adoption slice provides a 32-bit RmlUi 6.3 and FreeType 2.13.3 build, a D3D9 renderer, an interactive diagnostic document, and an isolated development workflow. It is opt-in at build time (`RMLUI=ON`), and draws only after `rml_show`. The economy menu still uses its existing native implementation.

## File ownership

| Path | Responsibility |
| --- | --- |
| `external/rmlui/`, `external/freetype/` | Pinned vendor bytes, upstream licenses, project patch record and provenance manifests. |
| `src/cl_rml.c`, `src/cl_rml.h` | CoD4 commands, filesystem/audio callbacks, modal input ownership and frame submission. |
| `src/rml/rml_host.h` | Engine-independent C boundary. No native game structures or RmlUi classes cross it. |
| `src/rml/runtime.cpp` | RmlUi lifetime, packaged filesystem, diagnostic bindings, semantic controls and bounded frame tickets. |
| `src/rml/frame.*` | Immutable CPU geometry/textures and render-command recording. |
| `src/rml/d3d9_renderer.*` | Render-thread uploads, GPU caches, state preservation, draw replay and device loss. |
| `ui/rml/` | Diagnostic markup, shared RCSS tokens, motion sample and licensed font. |
| `tests/test_rml.cpp`, `tests/fixtures/` | Real-core layout/input/lifecycle checks and D3D9 pixel/state/reset checks. |
| `tools/rml/` | Build, asset staging, private runtime staging, launch and vendor verification. |

Future PromodFlex economy markup belongs in the gameplay mod, with this host providing rendering and typed actions. Reusable host effects belong under `src/rml`; gameplay transactions remain in GSC. Do not copy the Unreal RHI renderer or UObject bridge into this subsystem.

## Build and use

From the repository root, run `tools/rml/Build.ps1`. The script uses the workspace MinGW/NASM tools by default and accepts `-Toolchain` and `-BuildPath` overrides. It builds an isolated development client, the native test executable and the existing private game runner. Dependencies are vendored; configure does not fetch from the network. Verify their hashes with `python tools/rml/verify-vendor.py`.

Stage an owned game and optional existing mod build:

```powershell
tools/rml/Stage-Game.ps1 -GamePath 'C:\path\to\Call of Duty 4' -ModSource 'D:\path\to\built\mods\pmflex'
tools/rml/Run-Game.ps1
```

The staging tool writes under `build-rml/lab`, uses a separate profile/app-data folder, and reads the installed game as input. It places the original Miles sound library under **both `mss32.dll` and `miles32.dll` beside `iw3mp.exe`**. The legacy entry point passes the game EXE's HINSTANCE to `Sys_LoadModules`; that function loads `miles32.dll` from the EXE directory. Placing it only beside `cod4x_021.dll` does not satisfy this load. A missing module now reports the attempted full path and Windows error code.

Activate the game, then press F8 to open the diagnostic. F9 reloads its loose RML/RCSS. Esc closes it. Console commands are `rml_show`, `rml_hide`, `rml_reload` and `rml_status`. `r_rmlUi 0` disables it. Console input takes priority, while modal RmlUi input owns camera/mouse/key events. Focus loss and disconnect release capture. The page's hover/select sounds use the existing game aliases and need audible in-game acceptance.

For a private runtime already prepared elsewhere, `Stage-Assets.ps1 -ModPath <private-mod-folder>` copies only the `ui/rml` namespace and writes an asset hash manifest. It rejects staging through a symlink/junction. Staged game runtimes have read-only-use junctions for stock `main`, `zone` and `miles`; never recursively remove them with a tool that follows junctions.

## Render and input design

The client thread alone touches RmlUi documents. It records a frame, which holds shared immutable geometry and texture data. A small ticket travels through native render-command ID 21, queued before console drawing. The existing dispatcher flushes native tessellation and calls the D3D9 backend. It consumes each ticket once. Discarded native queues retain at most three pending frames; device loss invalidates them.

The backend uses premultiplied blending, gamma-space UI colors, scissor rectangles and transforms. Geometry/glyph textures upload lazily and are reused. A state block plus explicit target, depth, transform, viewport and scissor restoration preserves native device state. Reset/shutdown notifications share one guarded release call with smoke. Smoke-only builds retain their existing absent-renderer behavior. GPU caches are cleared on loss and rebuilt from the retained CPU resources.

The filesystem copies a packaged file into its own memory before returning, releasing CoD4's temporary filesystem buffer immediately. RML assets resolve only under `ui/rml/`. TGA decoding currently supports uncompressed 24/32-bit images; fonts use FreeType. Native `<input type="button">` controls provide focus and disabled semantics. A short hover cooldown and stable control identity prevent repeated cues when moving inside one control. Held keys/buttons are released when a modal screen closes or loses focus.

The diagnostic proves the first supported visual subset: surfaces, solid/vertex-gradient fills, text, images, rectangular clipping, transforms and basic motion. Layer filters, stencil masks, named shader effects, particles and scene-background blur are subsequent work; they are not supplied by this initial backend.

## Verification status on 2026 11 06

- Full 32-bit client, RmlUi core and FreeType builds passed, including a build with both RmlUi and smoke enabled. A separate RmlUi-disabled/smoke-enabled client also built.
- Native D3D9 desktop checks passed for color/alpha pixels, TGA orientation, glyph rendering, scissor clipping, transforms, state restoration, cached uploads and a real device reset. The diagnostic was captured at `build-rml/rml-diagnostic.png`. Subsequent control/input refinements passed CPU checks; final in-game acceptance is left to the user.
- CPU checks use the actual diagnostic document, font and styles. They cover mouse and Enter activation, disabled controls, hover deduplication/reentry, capture loss with a held button, Escape, bounded tickets, failed reload and model retention. They do not substitute for GPU or native CoD4 input checks.
- Existing tick-rate checks passed; smoke bridge tests passed 105 checks and 131,072 technique-hook cases.
- The command runner's execution context rejects D3D9 HAL creation with `0x8876086a`; CTest marks only the native GPU case skipped there (exit 77). Running from the desktop supplied a real hardware device. The test program, launched with no arguments, writes `build-rml/rml-desktop.log` and `rml-desktop-errors.log`.
- Initial isolated CoD4 launches failed before logging because test staging omitted `miles32.dll` next to the game EXE. After the user authorized a normal-launcher retry without Computer Use, corrected staging passed Miles loading and reached Client Initialization Complete and renderer initialization. Actual CoD4 draw dispatch, audible cues, reset coexistence and modal input remain pending user validation.

No installed game binaries, profiles, server configuration or published releases were changed. The private launch workflow must pass in-game acceptance before the economy menu is replaced.
