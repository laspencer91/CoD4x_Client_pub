# Native cursor over RML — 2026-11-06

Follow-up to client `cce95ff` on `feat/rml-ui-foundation`.

Source inspection found that `CL_Rml_MouseEvent` explicitly called `CL_ShowSystemCursor(1)`.
The RML frame is submitted after the native menu, covering its software cursor. The adapter
now registers `ui_cursor` through the existing material API and submits its native stretch-pic
draw after the RML command and before console drawing. It hides the Windows pointer while
that material is available. Position comes from physical client mouse coordinates, initialized
from the OS pointer when opening. The draw has a square aspect and uses the centre hotspot;
stock material inspection maps `ui_cursor` to the 64x64 `3_cursor3` image.

The full RML-enabled client rebuilt successfully. All five CTests passed (5.10 seconds total).
Those tests cover the RML host and renderer; the game cursor is drawn by the native adapter.

The new DLL was loaded into private game process 27344. An engine screenshot captured
the RML palette with the original CoD4 arrow visibly drawn above it:
[native cursor capture](rml-native-cursor.png). Original file: private runtime
`mods/rmllab/screenshots/shot0017.tga`, 1280x800. Inspected visually after converting the TGA
to PNG. The first screenshot, shot0016, showed the native menu and is not used as cursor evidence.

Console status after shot0017:

```text
RmlUi: visible=1 recorded=1095 rendered=1095 draws=107438 uploads=8087/15 pending=0 result=0x00000000 reloads=0 failed=0 activations=9
```

This verifies native compositing of the game cursor on the palette. It does not establish
exhaustive focus/console, resolution or device-reset acceptance for this adapter change.
