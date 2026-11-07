# Decrypt text component — 2026-11-06

Follow-up to client `69caf48`, branch `feat/rml-ui-foundation`.

The requested effect is implemented as a registered RML element, not a width-mask variant.
`src/rml/decrypt_text.*` owns character progression and literal text nodes. Shared styles are
in `ui/rml/shared/decrypt-text.rcss`; palette sample 17 provides rapid and slow previews and
a local replay button. Cosmetic random output uses uppercase A–Z and 0–9 only. Two/three
distinct random candidates precede each character's correct value. The optional glitch is
a one-pixel displacement, not the pending full horizontal-tear/RGB shader effect.

The full client and test harness built. All six CTests passed on the desktop NVIDIA
GeForce RTX 3050 (5.78 seconds total): native, CPU, source, palette, economy and decrypt.
The new `rml_decrypt` test uses the actual custom element, palette and D3D9 backend:

- Two-cycle and three-cycle progression, distinct alphanumeric candidates and exact lock times.
- Correct locked prefix while the following character scrambles, including a digit's final value.
- Stable final-text width throughout decoding.
- Dynamic text replacement, spaces, punctuation, Unicode and literal markup in the final output.
- Instant mode, empty text, mouse-driven slow/normal speed controls and replay after settlement.
- Exact final string, no submitted economy actions and safe component recreation after document reload.
- No stylesheet/model warnings beyond the informational font load.

The UTF-8 iterator initially failed tests and was corrected before this passing build.
No failed build was staged into the game.

Actual renderer captures were inspected: [active decoding](rml-decrypt-active.png) and
[settled text](rml-decrypt-settled.png). These are standalone D3D9 fixtures, not native game
screenshots. At the active checkpoint, the first line reads `SECE`: `SEC` is locked and
the fourth character is a random candidate. Final output reads `SECURE THE SITE`.

The successful client and 37 UI assets were staged into the private runtime, started as
process 17300 with both source roots enabled. Native hardware-input, reduced-frame-rate
perception and performance measurements are not established by these fixture checks.
