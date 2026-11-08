# Server-authoritative recoil: design plan

Status: revision 2 (2026-11-07), after an independent review and a code-placement review. Implemented
the same day and verified in game on a listen server and a local dedicated server (no prediction
mismatches). The current guide is [recoil-models.md](recoil-models.md).
Facts and evidence: [verified facts](evidence/2026-11-07-authoritative-recoil-facts.md). Section 11 answers
the review.
Repos: `CoD4x_Client_pub` (client; owns the shared core), `CoD4x_Server` (dedicated server),
`promod-econ-na` (profiles, tooling), `cod4-promod` (release scripts).

## 1. Goal

Recoil becomes server-computed and identical on client and server:

- **View kick (the aim climb).** It is a deterministic simulation. The client sends pure mouse aim, and
  the server adds the kick, so a client cannot remove it. The client predicts the same values for the
  screen, and the server corrects the client's simulation state if they ever differ.
- **Gun kick.** It already moves bullets on the server, using server-side randomness. It stays
  server-authoritative. Models can now scale it on the server, and the client mirrors the scale for the
  viewmodel.
- **Models are plug-ins.** A new model is one self-contained source file, one registry line and a mod
  profile. Engine glue does not change.

Stock clients keep working on model-0 (native) servers. Bullet spread is untouched.

## 2. Today's behaviour (summary of the facts file)

- **A shot** sets view-kick angular velocity (deterministic shape, global random numbers) and adds gun
  kick.
- **View kick** is integrated only on the client, once per rendered frame, in slices of at most 5 ms. It
  decays toward centre, damps return motion by 0.06, snaps to rest when it crosses zero and caps at ±10°.
  It reaches the server only because the client adds it into each command's angles.
- **Commands.** The client makes exactly one command per rendered frame, and the server executes the
  same commands one by one. "Per command" therefore is the client's current cadence: there is no server
  recoil tick today.
- **Server bullets** use the previous command's angles plus the server's own gun angles (sway and gun
  kick).
- **Followers, killcam and server demos** see server `ps.viewangles`, which follow the command angles.

## 3. Strategy: fold recoil into each command, then correct the state

```
client frame N                                    server, command N  (SV_ClientThink, C)
--------------                                    -----------------
sim = committed + replay(records < N)             Recoil_Advance(sim, gameTime_N, inputs(ps before N))
Recoil_Advance(sim, T_N, inputs(predicted ps))    lastUsercmd.angles = wire + offset(sim)
cl.cmds[N].angles = SHORT(aim) + offset(sim)      ClientThink -> Pmove -> G_PlayerEvent per fire event
wire = cl.cmds[N].angles - offset   (CL_WritePacket)     -> hook: Recoil_Shot(sim) + gun kick x model scale
prediction: Pmove wrapper records cmd N inputs    ...
  and fire events -> record[N]                    after a new shot: svc_recoil {time of last cmd, sim}
svc_recoil -> committed = server state; replay
```

- **Downstream stays stock.** Everything after the command is unchanged: bullets, melee, spectators,
  killcam and demos. There are no player-state or snapshot-field changes.
- **Client view.** The client keeps folded commands locally, so prediction and the view are unchanged.
- **Correction.** The server sends its simulation state for the last executed command in a small message
  (`svc_recoil`) that only capable clients receive: once after each new shot, and every 500 ms otherwise.
  Between shots both sides recover from the same inputs, so more would add nothing; the rate does not
  depend on the server's snapshot rate. The client adopts it and replays its later command records, as in
  ScrapCore's predicted `MovementState`. A misprediction (a rejected shot, a script weapon
  change, a lost command, a clamped time) appears once, at most as a snap of the gap, instead of
  lingering.
- **Listen server.** The host's client folds the offset into the wire ("local fold"), because the server
  half is the stock binary. Gun kick there is scaled by a hook on the listen server's own `G_PlayerEvent`
  call (bin `0x4A9CB2`), using the same server-side handler as the dedicated server.
- **Honest limit.** A script that pulls the mouse down perfectly is still possible, as in any game with
  client prediction. Removing or reducing recoil is not.

## 4. Determinism rules

- **Time.** The command `serverTime`. The server clamps it exactly as `ClientThink_real` does, to
  `[level.time - 1000, level.time + 200]`, so recoil follows game time: a client that sends times running
  faster than the game buys no extra recovery. A legitimate client is inside that window, where both sides
  use its raw time. `ps.commandTime` is never used. (Revision 3; revision 2 used the unclamped time.)
- **Integer fixed point.** Angle in micro-degrees and angular velocity in milli-degrees per second
  (micro-degrees per ms), so every step is exact integer arithmetic. The 0.06 return damping is
  `x * 6 / 100` with C99 truncation.
- **5 ms slices on an absolute-time grid.** Recovery runs one stock 5 ms slice per multiple of 5 ms that
  the command time crosses. Any split of time into commands therefore gives the same result, and the
  simulation matches the stock float integrator at 5 ms exactly (1.35 deg peak, 220 ms to rest). Stock
  integrates per rendered frame, so its kick varies by up to 12% with frame rate (facts file); the grid
  removes that. A simulation at rest jumps its time forward.
- **WeaponDef conversion.** `floor(x * 1000.0 + 0.5)` in double is exact for every float, so both sides
  get the same integers from the same fastfile.
- **Shot randomness.** `splitmix64` over (domain, clientNum, shot number, weapon). The shot number is the
  simulation's count of shots, which survives deaths and travels in the correction, so the n-th shot always
  gets the n-th roll. No input is the client's choice. (Revision 3: revision 2 hashed the command time,
  which the client picks; holding a shot back a few ms would have let it choose its roll.)
- **Fold.** `offset = round(angle * 65536 / 360e6)` angle-short units. The client sets
  `cmd = ANGLE2SHORT(aim) + offset`, so `wire = cmd - offset` is exact, and the server sets
  `lastUsercmd = wire + offset`.
- **Explicit angles on the wire.** In authoritative mode the client offsets its own copy of the delta
  base, so the first command's angles are always written explicitly. The two sides' bases differ once
  angles are folded (facts: usercmd delta encoding), and explicit values do not depend on the base.
- **Inputs.**
  - Step inputs: the weapon (same offhand rule as stock), `fWeaponPosFrac > 0.5`, and alive state.
    They come from the player state before the command: the server reads the live ps before
    `ClientThink`; the client reads its prediction (frame N-1), and the Pmove wrapper records the same
    pre-command values during replays.
  - Shot inputs come from the ps after the command's Pmove, on both sides: the client's prediction wrapper
    reads it after `Pmove`, and the server's `G_PlayerEvent` runs right after `Pmove`.
  - Wherever these differ, the correction repairs it.

## 5. Model architecture

Engine-free C in `src/recoil/`, mirrored byte for byte to the server by `tools/sync-recoil.py`:

```
recoil/model.h      contract: wire grammar, registry, capability check (exists; params generalised)
recoil/sim.h        state, native impulse, native step, advance, shot, fold (new)
recoil/models/*.h   one file per model (continuous.h first)
```

The types and the model interface live in `src/recoil/sim.h`; the guide to them is
[recoil-models.md](recoil-models.md).

- **Model 0 (native)** is never simulated and stays stock and trusted.
- **Model 1 (continuous)** scales both the view impulse and the gun scale by the `Recoil_Fire` opening
  ramp, converted from kick distance to impulse (`Recoil_DistanceScale`), and resets the ramp on weapon
  change, keyed on the shot's `weapon`.
- **Recovery** ignores `params` unless a model supplies `step`.
- **Future spray patterns** are compiled tables inside their model file, selected by a per-weapon
  parameter (pattern ID). Tables must match bit for bit on both sides anyway, and the wire carries IDs,
  not tables.
- **Gun-kick randomness** stays server-side. Making it deterministic too is a possible later model
  feature, not part of this work.

## 6. Engine glue

| Side | Where | Does |
|---|---|---|
| Client | `CL_FinishMove` (`cl_main.c:9570`), before the vehicle branch | Rebuild sim (committed + replay), advance to `cmd->serverTime` with predicted-ps inputs, fold `offset`, record it for command N |
| Client | `CL_WritePacket` (`cl_main.c:4383`) | Authoritative mode only: copies with `angles -= offset`, explicit-angle base |
| Client | SetCall `0x4476A3` (prediction `Pmove`) | Own player only: record pre-command step inputs and new fire events per command, keyed by command time |
| Client | hook `0x457D2E` (exists) | Sim active and own player: zero the native view kick, scale gun kick by the shot's `gun_scale` |
| Client | `CL_ParseServerMessage` | `svc_recoil` updates the committed state |
| Client | `cl_recoil.c` | Contract mode (native, authoritative, local fold). Resets only when the contract string changes, and on connect/disconnect. History lives in the module, not in `cl` |
| Server | `SV_ClientThink` (`sv_client.c:3048`) | Active non-bot client with a non-native contract: reset/advance, fold into `cl->lastUsercmd` only (`cmds[i]` and the plugin event keep the aim-only wire value; demos record the folded `ps.viewangles`), and set the shot context (game time) around `ClientThink` |
| Server | `_g_active_mp.asm` `G_PlayerEvent` | Retarget its `call BG_WeaponFireRecoil` to `SV_Recoil_FireRecoil` (same cdecl signature): native call, then, in shot context, `Recoil_Shot` and gun kick × `gun_scale` |
| Server | `SV_EndClientSnapshot` (`sv_snapshot.c:577`) | Before EOF, for capable clients: `svc_recoil` after each new shot, plus a 500 ms heartbeat |
| Server | `sv_recoil.c` (exists) | Contract, capability gate, per-client sims, reset on world entry (`SV_ClientEnterWorld`) |

Reset rules (server-authoritative; the client mirrors them and is corrected): the client is not a live
player, the command time goes backwards, a gap of more than 2000 ms while moving, world entry, or a contract
change.

## 7. Contract and compatibility

- The wire is unchanged: `1:0` stays the permanent native contract, and model 1 keeps
  `weapon,opening,full_shot,recovery`.
- Only the client capability changes. `cl_recoilProtocol=2` means "applies server-authoritative
  recoil". A dedicated server publishing a non-native model refuses clients below 2 or without the model
  bit. Native servers admit everyone.
- No release contains recoil code (facts file), so changing model 1's mechanism cannot strand a
  deployed client. Branch builds with protocol 1 are refused on model-1 servers instead of double-kicking.
- `svc_recoil` is the next opcode after `svc_acdata`. It is written after downloads and before EOF, so
  a client without support that plays a recorded message logs "Illegible server message" and drops only
  that trailing opcode.

## 8. Edge cases

- **Spectating or following** (`otherFlags & 2`, or not `& 4`): no records and no fold. The view comes
  from the followed player's server-folded angles.
- **Killcam and death:** the not-alive reset applies on both sides.
- **Demo playback:** commands are still created, but never sent. Folding requires an active connection that
  is not playing a demo, so the simulation stays idle.
- **Bots:** not simulated, as in stock.
- **Four-event ring:** both sides read the same ring (`ps.events & 3`). More than 4 events in one command
  needs a 200 ms command at under 5 fps, and the correction repairs it.
- **Server-skipped or dropped commands** (duplicates, more than 20 s ahead): the client's records for
  them are superseded by the correction.

## 9. Verification

- Unit tests:
  - golden hashes and the fold round trip;
  - split invariance;
  - integer step at 5 ms against a float port of `0x44FE20` (peak and settle);
  - model 1 ramp;
  - replay and correction equivalence (committed + records gives the same result as straight
    simulation).
- `developer 1` logs each shot (server `recoil: client %d t=%u shots=%u speed=...`, client
  `recoil: t=%u shots=%u fold=...`), and the client logs each mismatched correction with both states. A local dedicated server with one
  client must produce matching logs and no corrections in a clean session.

## 10. Stages

1. **Shared core and tests:** sim, model registry, continuous model.
2. **Client local fold (listen server):**
   - Pmove wrapper records and the fold;
   - view-kick suppression and gun scale in the hook;
   - the listen-server gun-kick hook at `0x4A9CB2`;
   - a feel check of a neutral model 1 against model 0, then the 35% opening.
3. **Authoritative mode:**
   - server `SV_ClientThink` fold, the `G_PlayerEvent` retarget and `svc_recoil`;
   - client wire subtraction, explicit base and correction;
   - the protocol 2 gate;
   - a local dedicated server log comparison.
4. **Tooling and docs:**
   - generalised parameters in `recoil_contract.py`;
   - the constants sync;
   - `docs/recoil*.md`.
5. `/simplify` over the whole diff.

## 11. Review answers (revision 1 questions)

| # | Answer | Verified at |
|---|---|---|
| 1 Gun kick moves bullets | Correct. The plan now gives models a server-applied gun scale; today's model 1 has this gap (client-only gun scaling) | `_g_active_mp.asm:1483-1562`, `_g_weapon.asm:586-589` |
| 2 No correction | Added `svc_recoil` committed state plus client replay | `sv_snapshot.c:577-585`, `cl_main.c:7506-7540` |
| 3 Different ps per step | Inputs are pre-command ps on both sides, and the Pmove wrapper re-records them every prediction pass. The correction covers server-only changes (script `takeWeapon`, death) | bin `0x452A4B`/`0x452A57`, `sv_client.c:3058` |
| 4 Shot attribution | Shots are recorded by the prediction Pmove wrapper per command, never from `cg.predictedPlayerState`. Replays overwrite, so a shot can't count twice | bin `0x4476A3` (cdecl `Pmove(pmove_t*)`) |
| 5 Clamped time | Revision 3: the server clamps the command time exactly as `ClientThink_real` does, before `ClientThink`. A legitimate client is inside the window, where both sides use its own time | `_g_active_mp.asm:1263-1276`, `sv_client.c:3053` |
| 6 Reset asymmetry | The server defines resets (section 6), and the correction carries them. The client resets only on a contract-string change. History moves out of `cl` | `cl_main.c:8623`, fast `map_restart` keeps `svs.time` |
| 7 Protocol bump | Dropped: the wire stays version 1, and the capability `cl_recoilProtocol` goes to 2 | no release tag contains `85ddf3e`/`e17505c` |
| 8 Event cap | The server hook is the `G_PlayerEvent` call. Both sides read the same 4-slot ring | `_bg_misc.asm:2215-2240` |
| 9 Who sees the fold | Only `cl->lastUsercmd`. `cmds[i]`, the plugin event and `SV_ClientEnterWorld` keep the aim-only value; server demos record the folded `ps.viewangles`. Bots and inactive clients are skipped | `sv_client.c:1110-1132`, `3053-3059` |
| 10 Delta base | Explicit angle encoding in authoritative mode | server `MSG_ReadDeltaKeyShort` |
| 11 Gating | Own player only (`otherFlags & 4`, not `& 2`); no commands in demo playback | bin `0x457CC7`, `0x4529FB` |
| 12 Vehicle branch | Fold before it. The branch overwrites angles in a vehicle, which stays native, and Promod has none | `cl_main.c:9612-9618` |
| 13 Patterns | Compiled tables in the model file, selected by a parameter ID | n/a |
| 14 Thin inputs | Step and shot inputs now carry weapon, def, quantised ADS and alive state. The shot result includes the gun scale. Weapon-switch policy is the model's | n/a |
| 15 Over-engineering | Steps at rest cost nothing (fast path). `params`/`validate` is a small generalisation of the existing per-weapon wire | n/a |
| 16-18 Fact fixes | Applied in the facts file: argument order, the `& 6` fire gate, owner viewangles, client state >= 8, `offsetAngles` 0 on every Promod weapon | facts file |
