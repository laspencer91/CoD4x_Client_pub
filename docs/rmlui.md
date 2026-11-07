# Optional RmlUi host and CoD4 palette

The optional host provides a 32-bit RmlUi 6.3 and FreeType 2.13.3 build, a D3D9 renderer, an interactive diagnostic, a CoD4 component palette, and live source editing. It is opt-in at build time (`RMLUI=ON`), and draws after `rml_show`, `rml_palette` or an adopted economy menu opens. The optional economy bridge uses existing server dvars and native menu responses; GSC remains authoritative. Set `r_rmlEconomy 0` for the native menu.

## File ownership

| Path | Responsibility |
| --- | --- |
| `external/rmlui/`, `external/freetype/` | Pinned vendor bytes, upstream licenses, project patch record and provenance manifests. |
| `src/cl_rml.c`, `src/cl_rml.h` | CoD4 commands, filesystem/audio callbacks, modal input ownership and frame submission. |
| `src/rml/rml_host.h` | Engine-independent C boundary. No native game structures or RmlUi classes cross it. |
| `src/rml/native_keys.h` | Shared native CoD4 event values used by the engine adapter, runtime and tests. |
| `src/rml/runtime.cpp` | RmlUi lifetime, packaged filesystem, diagnostic bindings, semantic controls and bounded frame tickets. |
| `src/rml/source.*` | Explicit development source directory, bounded reads, recursive change detection and save coalescing. |
| `src/rml/effects.*` | Engine-independent soft-fade decorator adapted from ProlineUnreal. |
| `src/rml/frame.*` | Immutable CPU geometry/textures and render-command recording. |
| `src/rml/d3d9_renderer.*` | Render-thread uploads, GPU caches, state preservation, draw replay and device loss. |
| `ui/rml/` | Diagnostic and palette markup, shared RCSS tokens and tactical recipes, scanline texture and licensed font. |
| `tests/test_rml.cpp`, `tests/fixtures/` | Real-core layout/input/lifecycle checks and D3D9 pixel/state/reset checks. |
| `tools/rml/` | Build, asset staging, private runtime staging, launch and vendor verification. |

PromodFlex economy markup lives in the sibling gameplay mod at `ui/rml/economy.rml` and `economy.rcss`; `src/rml/economy.*` provides typed bindings and allowlisted actions. Reusable host effects belong under `src/rml`; gameplay transactions remain in GSC. Do not copy the Unreal RHI renderer or UObject bridge into this subsystem.

## Build and use

From the repository root, run `tools/rml/Build.ps1`. The script uses the workspace MinGW/NASM tools by default and accepts `-Toolchain` and `-BuildPath` overrides. It builds an isolated development client, the native test executable and the existing private game runner. Dependencies are vendored; configure does not fetch from the network. Verify their hashes with `python tools/rml/verify-vendor.py`.

Stage an owned game and optional existing mod build:

```powershell
tools/rml/Stage-Game.ps1 -GamePath 'C:\path\to\Call of Duty 4' -ModSource 'D:\path\to\built\mods\pmflex'
tools/rml/Run-Game.ps1
```

The staging tool writes under `build-rml/lab`, uses a separate profile/app-data folder, and reads the installed game as input. It places the original Miles sound library under **both `mss32.dll` and `miles32.dll` beside `iw3mp.exe`**. The legacy entry point passes the game EXE's HINSTANCE to `Sys_LoadModules`; that function loads `miles32.dll` from the EXE directory. Placing it only beside `cod4x_021.dll` does not satisfy this load. A missing module now reports the attempted full path and Windows error code.

Activate the game, then press F8 to open the palette. F9 reloads the current screen; Esc closes it. Console commands are `rml_palette`, `rml_show` (diagnostic), `rml_hide`, `rml_reload` and `rml_status`. `r_rmlUi 0` disables it. Console input takes priority, while modal RmlUi input owns camera/mouse/key events. Focus loss and disconnect release capture. The page's hover/select sounds use the existing game aliases and need audible in-game acceptance.

The runner selects windowed `1280x800` **before renderer initialization**. CoD4's `r_mode` takes a mode string; `-1` and `r_customwidth`/`r_customheight` do not select a custom mode in this client. Late changes to the latched fullscreen setting left the first test session with a lost device and no replayed frames. A windowed retry rendered through the native dispatcher.

## Editing while the game is open

`Run-Game.ps1` sets `COD4X_RML_SOURCE` to this checkout's `ui/rml` directory. The isolated client reads RML, RCSS, textures and fonts directly from that directory, retaining their `ui/rml/` resource names. Edit `palette.rml`, `palette.rcss`, or `shared/tactical.rcss` and save. There is no asset-copy or mod-relink step for those changes. `-SourcePath <directory>` selects a separate authoring tree. This environment override is connected only in `TEST_ISOLATION` clients.

While a screen is visible, its client-thread host checks Windows directory-change notifications at most ten times a second, then waits for a quiet interval of at least 100 ms before reloading. It watches writes, size changes and file/directory renames throughout the authoring tree, including same-size rapid saves. No filesystem scan or file hashing runs each frame. Stylesheet/template caches and texture handles are released on reload. Fonts are loaded once per host lifetime; replacing font files requires a restart. Hidden screens do no watcher polling; changes are picked up when opened again.

Reload releases held RML keys/buttons and replaces the DOM while keeping the bound model. Focus, scroll position and animation phase can reset. If the replacement document cannot load, the old document remains visible and the watcher can recover on the next save. RmlUi can accept partially malformed markup or ignore invalid RCSS declarations, so this is not a parser-error rollback guarantee. `rml_status` reports automatic reload successes/failures, activations and render statistics.

`Run-Game.ps1 -Packaged` clears the source override and uses the staged game filesystem instead. Run `Stage-Assets.ps1 -ModPath <runtime mod>` before that check. Use this mode to check packaging separately from live authoring. Both runners reject launching a second copy of the same private runtime; staging game binaries requires closing it. Live source edits do not require closing it.

## CoD4 palette and next features

Reference: `laspencer91/ProlineUnreal` revision `396a54108ac71a3960c51a7a7be4c8f33f930a2b`, inspected locally on 2026-11-06. The sources are `Plugins/ProlineRml/Source/ProlineRml/Private/ProlineRmlSoftFade.cpp`, `ProlineRmlSourceWatcher.cpp`, `ProlineRmlEffects.cpp`, `Shaders/Private/ProlineRml.usf`, and `docs/implementation/hud/RMLUI_PALETTE-2026-09-15.md`. No Unreal renderer, UObject or editor module is linked into CoD4.

| Palette cell | CoD4 implementation | Intended use |
| --- | --- | --- |
| Soft-fade surface | Proline decorator port; cached premultiplied vertex colors, no offscreen target. | Briefings and subdued equipment panels. |
| Frame and focus | Shared corner brackets, native button focus/disabled states and hover/select cues. | Equipment and loadout controls. |
| Signal sweep | Repeated scanline TGA plus a clipped transform sweep. | Phase and connection status. |
| Signal break | 240 ms displaced RGB text copies with stable outer layout. | Brief heading/phase transitions. This is a geometry variant, not Proline's row-band shader filter. |
| Arrival | 180 ms exponential-out translate/opacity settle. | Menu panels and updated selections. |
| Response states | Green amount punch, muted red denial text and disabled controls. | State vocabulary; sample values make no purchases. |

Replay restarts the sample timelines; all eighteen samples share the same source reload and local action route. Initial layout resolves before text/control geometry is recorded. Resizing receives a second layout update before recording at the new density.

The next renderer features are explicit: layer compositing and correct clipping first, then Proline's row-band/RGB glitch and noise dissolve. Grime needs a blurred silhouette; scene blur needs a separate scene capture. Decorative particles need a bounded emitter and batched geometry, rather than a DOM element per particle. None of those backend features is implied by adding their RCSS declarations. The development economy bridge is implemented; remaining native acceptance and packaging scope is recorded in the mod's `docs/rml-adoption.md`.

For a private runtime already prepared elsewhere, `Stage-Assets.ps1 -ModPath <private-mod-folder>` copies only the `ui/rml` namespace and writes an asset hash manifest. It rejects staging through a symlink/junction. Staged game runtimes have read-only-use junctions for stock `main`, `zone` and `miles`; never recursively remove them with a tool that follows junctions.

## Render and input design

The client thread alone touches RmlUi documents. It records a frame, which holds shared immutable geometry and texture data. A small ticket travels through native render-command ID 21, queued before console drawing. The existing dispatcher flushes native tessellation and calls the D3D9 backend. It consumes each ticket once. Discarded native queues retain at most three pending frames; device loss invalidates them.

The backend uses premultiplied blending, gamma-space UI colors, scissor rectangles and transforms. Geometry/glyph textures upload lazily and are reused. A state block plus explicit target, depth, transform, viewport and scissor restoration preserves native device state. Reset/shutdown notifications share one guarded release call with smoke. Smoke-only builds retain their existing absent-renderer behavior. GPU caches are cleared on loss and rebuilt from the retained CPU resources.

The filesystem copies a packaged file into its own memory before returning, releasing CoD4's temporary filesystem buffer immediately. RML assets resolve only under `ui/rml/`. TGA decoding currently supports uncompressed 24/32-bit images; fonts use FreeType. Native `<input type="button">` controls provide focus and disabled semantics. A short hover cooldown and stable control identity prevent repeated cues when moving inside one control. Held keys/buttons are released when a modal screen closes or loses focus.

The input boundary uses CoD4's actual event numbers from `win_wndproc.c::s_scantokey`, including navigation, keypad keys and modifiers. The older generic Quake `keycodes.h` values agree for Tab/Enter/mouse but differ for several special keys. F9 is handled explicitly while modal input is captured so that manual reload remains usable. The private `TEST_ISOLATION` client also routes F8 before the native menu can consume it. Console priority is checked before either shortcut. Host elapsed time is refreshed before opening a screen so a previously hidden UI starts its entrance on the current client clock.

While an RML screen owns input, the engine adapter draws the game's `ui_cursor` material after the RML frame and before the console. It uses physical mouse coordinates, uniform height-based scaling and the texture's centre hotspot. Opening a screen initializes its pointer position from the OS cursor. The Windows pointer is hidden while the game cursor is available; a missing material falls back to the system pointer. Losing application focus releases capture and restores the Windows pointer. This belongs to the game adapter, so standalone RML fixtures do not draw a game cursor. See [native cursor evidence](evidence/2026-11-06-rml-cursor.md).

The diagnostic proves the first supported visual subset: surfaces, solid/vertex-gradient fills, text, images, rectangular clipping, transforms and basic motion. Layer filters, stencil masks, named shader effects, particles and scene-background blur are subsequent work; they are not supplied by this initial backend.

## Verification status on 2026 11 06

- Full 32-bit client, RmlUi core and FreeType builds passed, including a build with both RmlUi and smoke enabled. A separate RmlUi-disabled/smoke-enabled client also built.
- Native D3D9 desktop checks passed for color/alpha pixels, TGA orientation, glyph rendering, scissor clipping, transforms, state restoration, cached uploads and a real device reset. The diagnostic was captured at `build-rml/rml-diagnostic.png`. Subsequent control/input refinements passed CPU checks; final in-game acceptance is left to the user.
- CPU checks use the actual diagnostic document, font and styles. They cover mouse and Enter activation, disabled controls, hover deduplication/reentry, capture loss with a held button, Escape, bounded tickets, retained documents on failed reload and model retention. The source test writes isolated real RML/RCSS files, observes layout changes after saves, removes the document and verifies recovery. They do not substitute for GPU or native CoD4 input checks.
- Existing tick-rate checks passed; smoke bridge tests passed 105 checks and 131,072 technique-hook cases.
- The command runner's execution context rejects D3D9 HAL creation with `0x8876086a`; CTest marks only the native GPU case skipped there (exit 77). Running from the desktop supplied a real hardware device. The test program, launched with no arguments, writes `build-rml/rml-desktop.log` and `rml-desktop-errors.log`.
- A fresh 2026-11-06 Windows checkout built the full client with smoke enabled and passed all four tests (`rml_native`, `rml_cpu`, `rml_source`, `rml_palette`) on an NVIDIA GeForce RTX 3050. The palette test checks complete button text on its first open and matching text after reload, switches back to the diagnostic, and captures 1280x800, 1024x768 and 3440x1440 targets.
- An isolated native CoD4 session with the released PromodFlex 0.1.16 mod rendered the palette over the native menu. A live RML text edit and RCSS color edit produced `source reloaded` and changed an engine screenshot without restarting that process. Renderer status reported equal recorded/rendered counts, no pending frames and `S_OK`. Captures and logs are under `build-rml/lab/game/mods/rmllab`. Audible cues, a human camera/fire suppression check and smoke rendering/reset coexistence still require interactive acceptance.

Game assets downloaded for this workspace live under `C:\development\games\cod4`; the private runtime uses read-only asset junctions. The PromodFlex client archive matched its published SHA256SUMS. No Steam installation, existing profile, server configuration or published release changed. See `docs/evidence/2026-11-06-rml-palette.md` for local provenance and acceptance scope.

## Economy and expanded palette follow-up

The palette contains eighteen samples: six original effects, six economy components, four briefing/motion effects and a reusable decrypt-text element. `TEST_ISOLATION` shortcuts are F7 for the economy fixture, F8 for the palette and F9 to reload the current screen. `rml_economy_preview` and `rml_palette` provide console equivalents. The preview does not spend live cash.

The runner watches the client `ui/rml` and sibling mod `ui/rml` roots together. Mod assets override shared paths. The current economy uses charcoal texture-gradient panels and stock HUD icons; weapon exports preserve source pixels inside padded 2:1 canvases. Shader-backed CSS gradients remain unsupported.

The full client and all five CTests passed on this workspace, including `rml_economy`. Its actual D3D9 document checks cover an on-screen Deploy button at 1280x800, native control events, authoritative cash retention, buy expiry and retained state on reload. The mod's `docs/evidence/2026-11-06-economy-visual.md` and aspect-ratio capture distinguish fixture evidence from native multiplayer acceptance.

### Briefing motion recipes

Four additional samples adapt Proline palette e7, g12, g13 and g17 using the existing geometry/scissor backend:

| Recipe | Behavior | Intended use |
| --- | --- | --- |
| `.typewriter-line` | 800ms left-to-right clipped line reveal with a caret. This is a continuous mask, not per-character typing. | Briefings and comms; keep prices and timers instant. |
| `.draw-frame` | Corners appear, the rule draws, then content fades in over 550ms. | Round/briefing headers. |
| `.stagger-row`, `.stagger-second`, `.stagger-third` | Three short row entrances settle within 500ms. | Initial squad/score list presentation. |
| `.power-on` | One 240ms opacity flicker settles to fully readable text. | Connection/status arrivals. |

Reusable rules live in `ui/rml/shared/tactical.rcss`; preview-only layout lives in `palette.rcss`. Add `.playing` on an ancestor to run the recipes. Resting CSS supplies the fully visible state; these effects do not loop. Size the typewriter's resting width and its keyframe end width together for different text. The frame recipe uses a 240dp stage and 222dp rule. Neither recipe hides transaction controls.

In F8, scroll to **Briefing / Motion** and press **Replay Effects**. It uses the existing replay route and preserves scroll position. Source reload uses the same shared stylesheet path. The actual D3D9 test exercises that button with mouse input, verifies intermediate reveal widths, full settled width and non-looping state, and records start/middle/settled PNGs. See [motion evidence](evidence/2026-11-06-rml-motion.md). These are palette components; they have not been wired to gameplay events. Full row-band glitch and noise dissolve remain separate renderer work.

## Decrypt text component

`src/rml/decrypt_text.*` registers `<decrypt-text>` with the host. Link `shared/decrypt-text.rcss` in the document, then use:

```xml
<decrypt-text text="SECURE THE SITE" tick-ms="18" cycles="3" glitch="glitch" />
```

Each character cycles through two or three distinct uppercase letters/digits, then locks to its original value. Already locked text stays correct while the next character scrambles. Spaces, punctuation and non-ASCII characters are preserved without scrambling. Random candidates exclude the correct character. This is cosmetic pseudorandom text, not encryption or a shader filter.

| Attribute | Default / behavior |
| --- | --- |
| `text` | Final literal text. Changes restart the component; `data-attr-text` can bind a model string. |
| `tick-ms` | 18ms per phase; clamped to 8?80ms. Each character uses `cycles + 1` phases, including its final lock. |
| `cycles` | 3; clamped to 2?3 scramble phases. |
| `glitch` | Optional presence attribute: add a 1dp horizontal twitch while scrambling. Omit for clean decoding. |
| `instant` | Optional presence attribute: immediately show final text, suitable for reduced-motion use. |
| `replay` | Change this value to restart. Internal helper `ReplayDecryptText(document)` increments it for each component. |

The component reserves the final text's width, paints the changing string above it and updates text geometry only when the string changes. Final characters are ordinary text nodes, so markup in `text` is not parsed as child elements. Animation is bounded to 256 UTF-8 code points and a 4096-byte input for parsing; longer input is shown immediately. Elapsed-time advancement skips missed phases on slow frames rather than building a backlog. It runs once and settles, without a timer thread or gameplay action. Source/document reload recreates the element and restarts from the authored text.

F8 sample **17 / Decrypt Text** provides separate replay and slow-preview controls. The global Replay Effects route also restarts decrypt elements. [D3D9 evidence](evidence/2026-11-06-rml-decrypt.md) records tests and captures.

## Strikepoint economy layout

The sibling mod's user-supplied Strikepoint handoff now governs the economy layout. The host uses a 1920x1080 authoring density only for economy/live preview screens; diagnostic and palette remain at their existing 1280x800 density. The centered stage scales uniformly with black letterboxing. Density changes trigger a second layout update even when the window size does not change, covering F7/F8 screen switching.

Economy fonts are loaded lazily from mod assets: Saira Condensed regular/semibold (mapped to the bold style) and Share Tech Mono. The mod includes their OFL licenses and source hashes. Missing fonts fail economy opening so the engine adapter can preserve the native menu. The palette does not acquire this dependency.

Existing teammate dvars now supply weapon and tactical thumbnails. The layout does not invent teammate armor data. The rebuy panel shows saved paid equipment and server-controlled reorder arrows. Keyboard Q/E toggles a picker, 1-9 selects its entry, Z/X requests an eligible armor/frag purchase or refund, C swaps tactical and Space deploys. Modal confirmation blocks those background actions. Attachment controls are siblings of the full-card picker hit area, so buying an attachment does not also open the picker.

The presentation keeps the original game cursor, grey gradient cards, amber selection and green cash/ownership. Context text briefly uses the decrypt component; Deploy uses the existing one-shot power-on recipe. Prices, cash and purchase controls do not wait for an entry sequence. Gradients/scrim/hologram fields are textures supported by the current renderer; no scene-blur/filter/particle backend is implied. The supplied Crash backdrop appears only in the fixture; live menus retain their game scene. The open center is reserved for future hero art rather than stretching stock HUD icons into a hero render.

The full client builds and all six CTests pass. The economy check verifies viewport containment and authored aspect ratio at 1920x1080, 1024x768 and 3440x1440, Deploy visibility at 1280x800, eligible armor dispatch, closed-buy rejection, keyboard/card behavior and density restoration. Informational font loads are excluded from test warning counts; parse/style/model diagnostics still fail the clean-document checks. See the mod's `docs/evidence/2026-11-06-strikepoint.md` for captures and scope.

Reusable controls, styling ownership and preview behavior: [component guide](rml-components.md).
