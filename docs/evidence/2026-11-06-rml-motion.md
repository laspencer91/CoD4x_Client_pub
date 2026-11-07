# Briefing motion palette — 2026-11-06

Follow-up to client `37383db` on `feat/rml-ui-foundation`. The reference is ProlineUnreal
`396a54108ac71a3960c51a7a7be4c8f33f930a2b`, `Content/Scrap/UI/Rml/Palette/palette.rcss`:
e7 typewriter reveal, g12 frame drawing, g13 staggered rows and g17 power-on flicker.
The CoD4 recipes are in `ui/rml/shared/tactical.rcss`; samples 13–16 are in `palette.rml`.

These adapt the reference using clipping, width, opacity and transforms. No shader filter,
offscreen layer or gameplay event integration is added. Typewriter is a continuous clipped
line reveal, not character-by-character typing. Power-on is one short opacity effect,
not the full Proline horizontal-tear/RGB glitch. Resting rules keep text visible once motion ends.

The test executable rebuilt successfully. All five CTests passed on the NVIDIA GeForce RTX 3050:
`rml_native`, `rml_cpu`, `rml_source`, `rml_palette`, `rml_economy` (5.01 seconds total).
The extended palette test uses real mouse input on Replay Effects after scrolling the section
into view. It verifies the reveal starts near zero width, progresses to an intermediate width,
finishes at full width and remains settled. Frame content, the last staggered row and the
power-on sample begin at near-zero opacity and finish fully visible. Parsing adds no warnings
beyond the permitted informational font load. The existing first-open text/reload and size checks pass.

Actual D3D9 fixture captures were inspected:

- [Start](rml-motion-start.png): hidden entrance content and collapsed reveal.
- [Middle](rml-motion-middle.png): partially revealed text and extending frame rule.
- [Settled](rml-motion-settled.png): readable text, completed frame and all squad rows.

The harness advances motion in 50ms steps because vendored RmlUi caps animation deltas at 100ms.
Large simulated-time jumps would not represent a running animation. These captures are standalone
renderer evidence, not an in-game hardware-input or performance claim.

Updated assets were staged into the private runtime. F8 opens the palette; scroll to
Briefing / Motion and use Replay Effects. Both direct source roots and the existing reload
route remain in place. The economy document and transaction behavior are unchanged.
