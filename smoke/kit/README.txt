VOLUMETRIC SMOKE TEST KIT (private test build, not for distribution)

What it is
  A self-contained test of the volumetric smoke in a CoD4x client. It uses your Call of Duty 4
  (version 1.7) install only to READ the game files. Everything it writes stays inside this kit
  folder (runs\ and reports\). It installs nothing, needs no CoD4x launcher or updater, runs its
  own small test server on 127.0.0.1 (ports 28970-28976), and stops everything it started when it
  ends. Your CoD4x/PromodFlex setup, profiles and settings are not touched.

  The kit contains: a self-built CoD4x 21.8 client with the smoke bridge (bin\cod4x_021.dll), the
  smoke renderer (bin\vsmoke\), the kit's loader (bin\cod4x_run.exe), a CoD4x 21.2 test server
  (server\), CoD4x's own zone files, and occupancy grids for stock maps (mp_crash).

Steps
  1. Copy the whole VSmokeTestKit folder anywhere on the test PC (for example the Desktop).
  2. Close Call of Duty 4 if it is running.
  3. Double-click Run-SmokeTest.cmd.
     - It finds your CoD4 folder (registry, Steam libraries) or asks you to choose it.
     - The game opens in a window and joins the test server on mp_crash.
     - Pick a class if the game asks (any class; press Esc to close menus).
     - The kit measures 10 seconds without smoke, puts you in front of a fixed spot, spawns a
       smoke cloud there and measures again. Then it is your turn.
  4. Look at the smoke and play with it (see below). In the black kit window type s + Enter for
     another cloud at the test spot, or just press Enter when you are done.
  5. The kit closes the game and the server and writes reports\smoke-report-<PC>-<time>.txt.
     Send that file back.

What to look for
  - Smoke vs walls, floors, doorways: the cloud should fill the space and stop at walls, not poke
    through them or show hard cut lines.
  - Edges: soft, no blocky steps or dark/bright halos around players, weapons or the gun in hand.
  - Flicker: no flashing, popping or tearing while you move, turn or stand inside the cloud.
  - Depth: objects in front of the smoke stay in front; objects behind it fade into it.
  - Bullets: shooting through the cloud leaves short-lived holes along the shot.
  - Inside the cloud: you should see almost nothing; walking out should clear smoothly.
  - Frame rate: note if the game stutters when the cloud is on screen.
  Write anything you notice (with where you stood) in a note next to the report.

If something goes wrong
  The report still gets written, with the reason (for example "no readable depth buffer",
  "renderer failed to load", a crash). To try again just run Run-SmokeTest.cmd again.
  Options (from a command prompt in this folder):
    Run-SmokeTest.cmd -GamePath "D:\Games\Call of Duty 4"      use this game folder
    Run-SmokeTest.cmd -Fullscreen                              fullscreen instead of a window

Notes
  - The PromodFlex mod is not needed; the test uses stock rules. (PromodFlex forces r_zFeather 0,
    which the smoke needs on; that is being changed separately.)
  - The client is a self-built CoD4x without CoD4x's own authentication, so it only joins test
    servers like the one in this kit.
  - Volumetric Smoke System by Logan - contact at logan@loganspencer.dev
