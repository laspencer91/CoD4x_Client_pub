# Authoritative recoil: verified facts

Facts gathered while planning [server-authoritative recoil](../authoritative-recoil.md). Each line was
checked against source, the server's NASM (`CoD4x_Server/src/asmsource`), or the game binary
(`iw3mp.exe`, disassembled with mingw `objdump -M intel`). "bin" means a client binary address.
"Inferred" marks a conclusion that has not been run.

## Shot kick (`BG_WeaponFireRecoil`)

- **Signature and outputs.** Server `_bg_weapons.asm:2180`: `(ps, gunKick, viewKick)`.
  - Gun kick is **added** to `gunKick`.
  - View kick is **set**: `viewKick = [-pitch*s, yaw*s, yaw*s*-0.5]`.
  - The client binary copy is bin `0x41A7B0` and uses registers: eax = ps, edi = view kick, stack = gun kick.
- **Weapon and range selection.**
  - Weapon: `offHandIndex` (+0xE0) when `weapFlags` (ps+0x10) `& 2`, otherwise `ps.weapon` (+0xE8)
    (`q_shared.h:1530-1532`; bin 0x41A7BF / 0x41A7C7).
  - View ranges are ADS (`WeaponDef+0x700..0x70C`) only when `fWeaponPosFrac == 1.0`; otherwise hip (`+0x748..0x754`).
  - Gun ranges are ADS (`+0x6E0..0x6EC`) when `fWeaponPosFrac > 0`; otherwise hip (`+0x728..0x734`).
- **Scale `s`.** `s = 1`, unless `weaponRestrictKickTime > 0` (ps+0x50). Then
  `s = 0.01 * adsReducedKickPercent` (`+0x6DC`) at full ADS, otherwise `0.01 * hipReducedKickPercent` (`+0x724`).
- **Randomness.** Each range uses `randomf()`, the global `ms_rand` LCG (`sRandSeed`, server `q_math.c:915-930`).
  That LCG is shared by the whole process, so client and server cannot reproduce each other's values.
- **Promod weapon data.** The AK-47 view kick is pitch -30..60 and yaw 60..-60, with centre speed 1500 for
  hip and ADS (`weapons/mp/ak47_mp`). `adsViewErrorMin/Max` is 0 in every `weapons/mp` file, snipers included.

## View-kick decay (`CG_KickAngles`, bin `0x44FE20`)

- **Where it runs.** Client only, once per rendered frame, over `cg.frametime` (cg+0x46124), in slices of
  at most 5 ms (`dt = slice * 0.001`, double @0x70AF98).
- **Per axis, for pitch, yaw and roll.** State: `cg.kickAVel` @0x79E748 and `cg.kickAngles` @0x79E754.
  - If both velocity and angle are 0, the axis is skipped.
  - If angle != 0, velocity += dir * centre * dt, where dir = -1 for a positive angle (@0x70AD40), else +1.
  - `centre` is `WeaponDef+0x710` when `fWeaponPosFrac (cg+0x46230) > 0.5` (@0x6BE648). Otherwise it is
    `+0x758`. With no weapon it is 2400.0 (@0x70B670).
  - change = velocity * dt. It is scaled by 0.06 (@0x70AEF0) when moving toward centre (angle * change < 0).
  - If the angle crosses 0, angle = velocity = 0. If the new angle is exactly 0, velocity = 0.
  - If |angle| > 10.0 (@0x6BDF70), the angle clamps to +-10 and velocity = 0.
- **Weapon source.** The weapon comes from the predicted ps at cg+0x4613C, using the same offhand flag rule
  as the shot.
- **Gating.** It runs only when `nextSnap->ps.otherFlags & 4` (bin `0x4529F5`). Otherwise the kick is zeroed
  every frame (bin `0x452A10-0x452A30`). The kick is also zeroed at respawn (bin `0x445FA0`, per review).
- **Reference port.** ScrapCore's `step_axis` (ProlineUnreal `gameplay_recoil.cpp`) is the same algorithm
  with a configurable ceiling.

## Client command flow

- **One command per frame.** `CL_CreateNewCommands` (bin `0x463E00`) makes one command per rendered frame
  once client state >= 8. It calls `CL_CreateCmd` (bin `0x463D10`), which calls the source `CL_FinishMove`
  (`cl_main.c:9570`).
- **Angles.**
  - `CL_FinishMove` sets `angles = ANGLE2SHORT(viewangles + cgameKickAngles)`.
  - The vehicle branch then overwrites them (`cl_main.c:9612-9618`); it is unused in Promod.
  - `cgameKickAngles = kickAngles + offsetAngles` (`cg_main.c:1799-1803`). `offsetAngles` is the ADS view
    error (bin `0x447A0E-0x447AD4`).
- **Order inside a frame.**
  1. The command is created (bin `0x452A4B`).
  2. `CG_PredictPlayerState` runs (`0x452A57`).
  3. Predicted events are processed.

  So `CL_FinishMove` for command N reads the prediction made in frame N-1.
- **Prediction call.** Prediction replays every unacknowledged command from the snapshot ps by calling
  `Pmove(pmove_t*)` (cdecl, one stack argument) at bin `0x4476A3`. The `pmove_t` is at `0x8C9C90 + local
  offset`. Pmove is bin `0x414D10` and splits a command into steps of at most 66 ms.
- **Event processing.** The cgame `pmoveHandlers` entry has no playerEvent callback. Predicted events reach
  `CG_FireWeapon` (bin `0x457C60`), whose kick call is at bin `0x457D2E`. That call is guarded by
  `otherFlags & 6` and receives the global `cg.predictedPlayerState`, not the firing command's state.
- **Writing commands.** `CL_WritePacket` (`cl_main.c:4273-4392`) writes `cl.cmds[]` delta-chained from
  `MSG_SetDefaultUserCmd(&cl.snap.ps)`.

## Usercmd delta encoding

- **Server decode.** `MSG_ReadDeltaUsercmdKey` (server `msg.c`):
  - The first bit chooses between a whole-command copy from the base and a delta.
  - Each angle is `MSG_ReadDeltaKeyShort`: if its bit is set, value = `key ^ short`, which does not depend
    on the base; otherwise the base value is used.
- **Bases differ between the two sides.**
  - The server base is `MSG_SetDefaultUserCmd(live ps)`, with angles `ps.viewangles - delta_angles` (`msg.c:2506-2518`).
  - The client base comes from `cl.snap.ps`, whose viewangles reach the owner only on teleport, viewlock,
    pm_type 5, follow or archived snapshots (`msg.c:2686-2694`).
  - An angle the client encodes as "unchanged" is therefore rebuilt from the server's base.
  - Inferred: this is safe in stock because both bases track the same command angles. A folded offset
    breaks that equality, so authoritative mode must force explicit angle encoding.

## Server command flow

- **Decode before execute.** `SV_UserMove` (`sv_client.c:1055-1131`) decodes every command in a packet
  first, then runs `SV_ClientThink` per command. It skips commands at or before `lastUsercmd.serverTime`,
  and commands newer than the packet's last one.
- **`SV_ClientThink`** (`sv_client.c:3048`, C):
  - It drops commands more than 20 s ahead of `svs.time`.
  - It copies the raw command into `cl->lastUsercmd`, then calls `ClientThink`.
  - `ClientThink` (`g_active.cpp:48-66`) reads `lastUsercmd` through `SV_GetUsercmd` into `sess.cmd`.
  - The plugin event and `SV_WriteDemoArchive` run after it on `cmds[i]` (`sv_client.c:1129-1132`).
- **`ClientThink_real`** (asm, `_g_active_mp.asm:1244`):
  - It clamps `sess.cmd.serverTime` to [level.time-1000, level.time+200] and caps msec at 200.
  - It computes `fGunPitch/Yaw` (gclient+0x30A4/0x30A8) with `BG_CalculateWeaponAngles` from the previous
    `ps.viewangles` plus the gun-kick state at gclient+0x3158 (`:1483-1542`).
  - It calls `Pmove` once (`:1545`), then `G_UpdateEntityStateFromPlayerState` (`:1562`).
  - Finally it runs the event loop, which calls `FireWeapon` (`:1659-1669`).
- **Gun kick is applied inside the command.** `G_UpdateEntityStateFromPlayerState` (`g_active.cpp:86`) calls
  `BG_PlayerStateToEntityState` (`_bg_misc.asm:2055`). That walks the new `ps.events` ring slots (`& 3`) and
  calls `pmoveHandlers[1].playerEvent = G_PlayerEvent` (`_bg_pmove.asm:8619`).
  - For events 26, 27 and 38, `G_PlayerEvent` (`_g_active_mp.asm:326-357`) calls
    `BG_WeaponFireRecoil(ps, gclient+0x3158, stack)`.
  - That adds gun kick and throws the view kick away.
  - It happens per command, inside `SV_ClientThink`.
- **Bullets.** `FireWeapon` (`_g_weapon.asm:553-713`) builds bullet, shotgun, projectile and grenade
  direction from `ps.viewangles` with pitch and yaw replaced by `fGunPitch/Yaw` (`:586-589`).
  - **Server gun kick (random, server-only) therefore moves bullets.** It is server-authoritative today.
  - Melee uses `BG_GetPlayerViewDirection(ps.viewangles)` instead (`:833`).
- **Antilag.** `FireWeapon` gets `client->lastServerTime` (the command time) when `g_antilag` is on.
  `G_AntiLagRewindClientPos` (`g_weapon.c:18-87`) rewinds only target origins, by at most 400 ms.
- **Player state size.** `playerState_t` is 0x2F64 bytes and the asm hard-codes gclient offsets after it,
  so it cannot grow. `playerStateFields` has 141 entries; the count is written in 8 bits (`msg.c:2358-2500`, `:2781`).

## Contract and release status

- **Release status.** No release tag contains recoil code. Commits `85ddf3e` and `e17505c` are only on
  `feat/rml-ui-foundation`, and the latest client tag `promodflex-v0.1.15` predates them. Deployed clients
  ignore `sv_recoilContract`.
- **Current wire.** `RECOIL_NATIVE_WIRE "1:0"` is documented as permanent. `Recoil_Decode` accepts wire
  version 1 only. `CL_Recoil_SystemInfoChanged` raises `Com_Error` on an undecodable contract.
- **Current model 1 has a gap.** It scales gun kick on the client only (`cl_recoil.c:78-81`). The server's
  gun kick, which moves bullets, stays at full native strength.

## Stock kick strength depends on frame rate (measured)

A float port of `CG_KickAngles` (test `tests/test_recoil.c` `StockKick`) driven with one frame per call. An
AK-47 pitch kick of 60 deg/s with centre speed 1500:

| Frame rate | Peak | Back at rest |
|---|---|---|
| 60 fps | 1.359 deg | 224 ms |
| 125 fps | 1.373 deg | 224 ms |
| 250 fps | 1.320 deg | 216 ms |
| 333 fps | 1.292 deg | 213 ms |
| 1000 fps | 1.230 deg | 206 ms |
| shared 5 ms grid (`sim.h`) | 1.350 deg | 220 ms |

The decay integrates per rendered frame, so recoil is up to 12% stronger at 125 fps than at 1000 fps. The
continuous solution is 1.20 deg. The shared simulation uses stock's own 5 ms slice on an absolute-time grid:
identical to the float port at 5 ms frames, and the same at every frame rate.

## Placement review (verified hook sites)

- **Prediction `Pmove` call.** Bin `0x4476A3` is `e8 68 d6 fc ff` (to `0x414D10`, cdecl `Pmove(pmove_t *)`).
  - `pm->ps` is `cg.predictedPlayerState` (0x794474), and `pm->cmd.serverTime` is at +4.
  - EBX holds the command number. The loop runs from `cmdNumber - 127` to `cmdNumber` (0x44746D-0x4476BF),
    starting from the snapshot ps every frame.
  - It is skipped for a demo, a followed player (`otherFlags & 2`), `cg_nopredict` and `cg_synchronousClients`.
  - It does not check `otherFlags & 4`, so free-roaming spectators predict too.
- **`CL_CreateNewCommands`.** It increments `cl.cmdNumber` (0x463E1D) before `CL_CreateCmd`. `CL_FinishMove`
  writes into a stack temporary, which is copied into `cl.cmds[n & 127]` afterwards (0x463E3F).
  - Commands are also created while CA_PRIMED, and during demo playback (state 9, never sent).
- **Command readers.** Only local readers use `cl.cmds`, through `CL_GetUserCmd` (0x45AB40), and they want
  folded angles.
- **Command writer.** `MSG_WriteDeltaUsercmdKey` (bin 0x505C60) uses a short form that takes `angles[2]`
  from the base when it is unchanged. The client's `ANGLE2SHORT` rounds and the server's truncates, so the
  two delta bases already differ in stock. Pmove converts the full dword, so angles must be masked to 16 bits.
- **Kick state writers.** Only `CG_KickAngles`, the zeroing when not the local player (0x452A10), the
  respawn reset (0x446018) and the call at 0x457D2E write `cg.kickAVel`/`kickAngles`.
  - Damage and shellshock kick do not touch them.
  - The only other client-trusted part of the command angles is `offsetAngles` (the ADS view error).
- **Listen server.** The listen server's `G_PlayerEvent` recoil call at bin `0x4A9CB2` is `e8 f9 0a f7 ff`,
  with the same register convention. Its `SV_ClientThink` is binary (0x52CD10).
- **Server internals.**
  - `SV_ClientThink` also runs for bots (`sv_main.c:4099`; `remoteAddress.type == NA_BOT`).
  - With `g_synchronousClients`, `ClientThink_real` runs from `G_RunClient` instead, outside the command.
  - Fire events are consumed inside the command at `_g_active_mp.asm:1562`. Other callers of
    `BG_PlayerStateToEntityState` (`ClientEndFrame`, `ClientSpawn`, `TeleportPlayer`, turret, `setOrigin`)
    find no new events.
- **Snapshot end.** `SV_EndClientSnapshot` runs for every client message (`sv_snapshot.c:550`, `:1158`).
  Server demos record that buffer (`sv_snapshot.c:470`), and client demos record the raw message.
  - A client without `svc_recoil` support logs "Illegible server message" (`cl_main.c:7508`).
  - The svc enums match on both sides, so the next free opcode is 13.
- **Windows server build.** `mingw32-make` with the bundled MinGW and NASM links `bin/cod4x18_dedrun.exe`.
  Only the plugin `.def` export step fails, because `pexports` is missing.

## Runtime verification (2026-11-07)

- **Listen server.** mp_crash, AK-74u, `recoil_model 1`, local mode. Each shot was recorded exactly once,
  78-88 ms apart. The command fold climbs during a spray (pitch about -313 units, or 1.7 deg), and a
  second burst opens at 35% again.
- **Local dedicated server.** `bin/cod4x18_dedrun.exe` with `recoil_model 1`, cheats on and `sv_pure 0`
  for the unsigned test client. The test client (session-only Key Code) connected with protocol 2 and
  models 3. The server simulated about 31 shots across two sprays. `cl_recoilStatus` reported 125
  corrections received, 124 compared with the client's own prediction, and 0 mismatched.
- **Correction rate.** After the send-condition fix (only the kick and model state count as a change),
  corrections fell from one per snapshot to 125 over the session.
- **Server frame rate.** This test ran at the default `sv_fps 20`. The PromodFlex release config runs
  `sv_fps 100`, `sv_minSnaps 100` and rate 100000 (`cod4-promod/release/promodflex/pmflex-server.cfg`), so each
  client gets up to 100 snapshots a second. A correction can ride on every one while the kick moves: 61 bytes x
  100 = about 6.1 KB/s. The simulation itself does not depend on the server frame rate, because it steps on
  command time. Revision 3 sends one correction per shot instead (see below).
- **100 Hz, revision 3 (shot count, per-shot corrections, game-time clamp).** `sv_fps 100`, `sv_minSnaps 100`,
  rate 100000, AK-74u sprays.
  - No added lag: 84 shots on both sides; 134 corrections compared, 0 mismatched.
  - Through a UDP relay with 40 ms each way, 0-10 ms jitter and 2% loss both ways (about 2% of packets
    dropped each way, measured): two runs, 45 and 60 shots with equal counts on both sides; 114 and 117
    corrections compared, 2 mismatched in each. Every mismatch sat on a server shot command with the same
    shot count: the client's speeds equal the server's plus about two 5 ms recovery slices, so the client
    fired the same roll about one command earlier than the server. The correction snapped the view by about
    0.08 deg. Cause: lost commands (below).
- **Environment notes.**
  - This machine has no CoD4 Key Code in the registry. Test builds (`COD4X_TEST_ISOLATION`) generate
    a session-only key in memory.
  - Promod's `servercheck.gsc` forces `sv_pure 1` unless `sv_cheats` is on.
  - `sv_pure` registers after config execution, so set it with rcon after startup.
- **Recoil bug found in play: 2.6 s client freezes while running (fixed).** `CL_Recoil_PredictPmove` read
  the fire events a Pmove added with `for (seq = before; seq != after; ++seq)`. The event counter is 8 bits:
  `BG_AddPredictableEventToPlayerstate` stores `(eventSequence + 1) & 0xff` (server `_bg_misc.asm:2847`). At
  the wrap (255 to 0) the loop walked about 4.3 billion values, freezing the main thread for about 2.6 s, once
  for each prediction pass that replayed that command. Footsteps reach the wrap every few yards of running.
  - **How it was found.** The server logged a flood of "Delta request from out of date packet": the client's
    acknowledgement was stuck for about 290 snapshots. A relay that logs silences showed the client sending
    nothing for 2.63-2.70 s while the server kept sending. A gdb sample of the main thread showed it in the
    hook's event loop (DLL `0x64d1bc39`) with the loop counter at about 32 million.
  - **Fix.** `Recoil_NewEvents` (`engine.h`) counts new events modulo 256 and caps them at the 4-slot ring;
    tests cover the wrap.
- **The opening scaled speed, not distance (fixed).** The native kick is an impulse; recovery pulls back at a
  constant rate (`CG_KickAngles`; gun kick in `BG_CalculateWeaponPosition_GunRecoil_SingleAngle`, plus drag), so
  the distance grows with the square of the impulse. Model 1 scaled the impulse by the opening, so 35% moved the
  aim about 12% as far and the kick was back to zero within about 80 ms, before the AK-47's next shot (fire time
  85 ms, `weapons/mp/ak47_mp`): the second bullet landed on the first. Aim offset when shot 2 fires, averaged
  over the AK-47's roll range: 0.00 deg at 35% of the speed, 0.12 deg (largest 0.50) at 35% of the distance, 0.57
  deg (largest 1.79) at stock. `Recoil_DistanceScale` (integer square root) turns the ramp's distance share into
  the impulse share; a 35% opening now peaks at 38% of stock's 1.35 deg (the first 5 ms slice adds a little).
- **A stale server object made every shot disagree (build fixed).** After the distance change, 14 of 82
  corrections mismatched: the client's kick was exactly 1.414x the server's on a burst's first shot (sqrt 0.5 vs
  0.5) and 1.225x on the second. The change touched only the shared headers, and the server makefile had no
  header dependencies, so `obj/sv_recoil.o` kept the old model. The makefile now compiles with `-MMD -MP` and
  reads `obj/*.d`; touching `src/recoil/sim.h` rebuilds `sv_recoil.o`. Rebuilt: 1 of 113 mismatched.
- **Remaining mismatches are lost commands.** With 150 ms ping, jitter and 2% loss, and the client's
  `cl_packetdup 0` (each command sent once), about one shot in 30 disagreed. Each had the same shot count and
  roll, with the client's kick exactly one or two 5 ms recovery slices ahead: the server never received the
  firing command and ran the shot in the next one. The next correction repairs it. Inferred from the values, not
  yet confirmed by repeating with `cl_packetdup 1`.
- **Every round repeated the same rolls (fixed).** `SV_ClientEnterWorld` runs at every round's restart and
  started the shot count at 0, so the n-th shot of every round had the same roll (the server log showed the same
  first-shot kick 14 times). The count now starts at a random value (`Com_RandomBytes`); the client learns it
  from the first correction, sent at once after entry.
- **Engine bug found while testing at 100 Hz (fixed in `CoD4x_Server/src/sv_main.c`).** Under `+devmap`, a
  Promod script calls `map_restart(false)` at level time 110 ms, during the three settle frames
  `SV_SpawnServer` runs inside the load frame (10 ms each at 100 Hz). `GScr_MapRestart` sets the level's
  restart flag (`level+0x2ddc` = 1) and queues `fast_restart`, which runs in the same engine frame.
  `SV_MapRestart` then returned silently (`com_frameTime == sv.start_frameTime`), so the flag stayed set and
  the round end's `map_restart(true)` at 119.5 s failed with "map_restart already called". That error is
  fatal with `developer 1`; with `developer 0` it would end the script thread and the round would never
  restart. Logged with temporary instrumentation. The fix defers such a restart to the next frame
  (`sv_deferredFastRestart`).

## Fixed: the client chose one hash input and the recovery clock (found 2026-11-07, inferred, not exploited)

- **The roll depends on the command time.** `Recoil_Hash` mixes the shooter, the raw usercmd `serverTime`,
  the shot index and the weapon (`src/recoil/sim.h:77-84`). The server takes `shot.time` from that raw time
  (`sv_recoil.c`, `SV_Recoil_FireRecoil`).
- **The client picks that time.** The server accepts any increasing command time: `SV_UserMove` drops only
  stale or out-of-order commands (`sv_client.c:1111-1120`), and `ClientThink_real` clamps to
  `[level.time-1000, level.time+200]`.
- **Inferred exploit.** A modified client could hold back the command that fires for a few ms and send it
  at whichever time gives the smallest pitch roll. The hash is public and cheap. With AK-47 pitch -30..60,
  the best of 5 rolls averages about -15 against a fair mean of +15, so the kick would point down. The cost
  in fire rate has not been measured.
- **Fix (revision 3).** `Recoil_Hash` mixes the shooter, the shot number and the weapon. The shot number
  is `recoil_sim_t.shots`: `Recoil_Shot` assigns it, `Recoil_Reset` keeps it, and `svc_recoil` carries it
  (16 words). `SV_ClientEnterWorld` starts each client at a random shot count (below), which the first
  correction carries to the client.
  Knowing upcoming rolls does not help, because skipping a shot does not change them. The only choice left
  is to spend a bad roll on a wasted shot, which costs ammo and noise.
- **Second time input: the recovery clock.** Recovery and the model-1 ramp read the command time. The game
  clamps it to `[level.time - 1000, level.time + 200]` (`_g_active_mp.asm`, `ClientThink_real_10`:
  `level+0x1ec` is `level.time`, `g_shared.h:126`), but the simulation used the raw value, and
  `SV_ClientThink` accepts commands up to 20 s ahead (`sv_client.c:3052`). A client could therefore run its
  command clock fast during a spray and buy extra recovery. The server now applies the same clamp
  (`SV_RecoilGameTime`), so recoil follows game time; legitimate clients are inside the window.
- **Correction policy.** Only shots add information the client cannot predict, so the server sends
  `svc_recoil` once after each new shot, plus the 500 ms heartbeat, instead of every snapshot while the
  kick moves. At 100 Hz that is about one per shot instead of 100 a second.
