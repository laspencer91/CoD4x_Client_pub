// Private loopback fixture. Mount as main_shared/promod/tickrate_smoke.gsx.
// Server completion proves a real detonation; compare both renderer reports separately.
run()
{
    level endon("restarting");
    level endon("tickrate_shared_smoke_done");
    if (!getDvarInt("tickrate_test") || getDvar("net_ip") != "127.0.0.1") return;
    level.sharedSmokePlayers = [];
    level.sharedSmokeBot = undefined;
    level thread watchdog();
    wait 3;
    logPrint("TICKRATE_SMOKE;WAITING_FOR_TWO_REAL_CLIENTS\n");
    started = getTime();
    while (level.sharedSmokePlayers.size < 2 && getTime() - started < 90000) {
        level.sharedSmokePlayers = [];
        players = getEntArray("player", "classname");
        for (i = 0; i < players.size; i++) {
            if (isDefined(players[i].isbot) && players[i].isbot) continue;
            if (!isDefined(players[i].pers["team"])) continue;
            level.sharedSmokePlayers[level.sharedSmokePlayers.size] = players[i];
            if (level.sharedSmokePlayers.size == 2) break;
        }
        wait 0.2;
    }
    if (level.sharedSmokePlayers.size < 2) { fail("two_clients_timeout"); return; }
    wait 3;
    level.rdyup = false;
    level.forcedEnd = false;
    game["PROMOD_KNIFEROUND"] = false;
    for (i = 0; i < 2; i++) level.sharedSmokePlayers[i] thread prepare("allies");
    bot = addTestClient();
    level.sharedSmokeBot = bot;
    if (!isDefined(bot)) { fail("bot_connect"); return; }
    started = getTime();
    while (!isDefined(bot.hasSpawned) && getTime() - started < 5000) wait 0.05;
    if (!isDefined(bot.hasSpawned)) { fail("bot_initialized"); return; }
    bot thread prepare("axis");
    wait 3;
    for (i = 0; i < 2; i++) {
        if (!isDefined(level.sharedSmokePlayers[i]) || level.sharedSmokePlayers[i].sessionstate != "playing") {
            fail("real_client_spawn");
            return;
        }
    }
    if (bot.sessionstate != "playing") { fail("bot_spawn"); return; }
    points = getEntArray("mp_sd_spawn_attacker", "classname");
    if (!points.size) { fail("no_spawn_anchor"); return; }
    base = floorAt(points[0].origin);
    yaw = points[0].angles[1];
    side = anglesToForward((0, yaw + 90, 0));
    for (i = 0; i < 2; i++) {
        player = level.sharedSmokePlayers[i];
        offset = -24;
        if (i) offset = 24;
        player setOrigin(floorAt(base + side * offset));
        player setPlayerAngles((0, yaw, 0));
        player closeMenu();
        player closeInGameMenu();
        player freezeControls(true);
    }
    bot botStop();
    bot.archivetime = 0;
    bot setOrigin(base);
    bot setPlayerAngles((80, yaw, 0));
    bot freezeControls(false);
    bot enableWeapons();
    bot botWeapon("m4_mp");
    bot setOffhandSecondaryClass("smoke");
    bot giveWeapon("smoke_grenade_mp");
    bot setWeaponAmmoClip("smoke_grenade_mp", 0);
    bot setWeaponAmmoStock("smoke_grenade_mp", 1);
    bot switchToOffhand("smoke_grenade_mp");
    wait 1;
    if (bot getWeaponAmmoStock("smoke_grenade_mp") < 1) { fail("no_smoke_ammo"); return; }
    level.sharedSmokeDetonated = false;
    bot thread captureSmoke();
    logPrint("TICKRATE_SMOKE;READY;" + getTime() + ";" + getDvarInt("sv_fps") + ";" +
        level.sharedSmokePlayers[0].name + ";" + level.sharedSmokePlayers[1].name + "\n");
    bot botAction("+smoke");
    wait 0.8;
    bot botAction("-smoke");
    started = getTime();
    while (!level.sharedSmokeDetonated && getTime() - started < 10000) wait 0.05;
    if (!level.sharedSmokeDetonated) { fail("no_native_detonation"); return; }
    for (i = 0; i < 2; i++) {
        player = level.sharedSmokePlayers[i];
        player setPlayerAngles(vectorToAngles(level.sharedSmokeOrigin + (0,0,48) - (player.origin + (0,0,60))));
    }
    for (i = 0; i < 6; i++) {
        wait 5;
        logPrint("TICKRATE_SMOKE;OBSERVE;" + getTime() + ";" + ((i + 1) * 5000) + "\n");
    }
    cleanup();
    logPrint("TICKRATE_SMOKE;NATIVE_COMPLETE;verify_two_client_renderer_reports\n");
    logPrint("TICKRATE_TEST;PASSED;" + getDvarInt("sv_fps") + "\n");
    level notify("tickrate_shared_smoke_done");
}

prepare(team)
{
    self endon("disconnect");
    self.pers["team"] = team;
    self.team = team;
    self.sessionteam = team;
    self maps\mp\gametypes\_promod::setClassChoice("assault");
    self.pers["assault"]["loadout_primary"] = "m4";
    self.pers["assault"]["loadout_primary_attachment"] = "none";
    self maps\mp\gametypes\_globallogic::spawnPlayer();
    self.maxhealth = 100000;
    self.health = 100000;
    self closeMenu();
    self closeInGameMenu();
    self freezeControls(false);
    self enableWeapons();
}

floorAt(origin)
{
    trace = bulletTrace(origin + (0,0,64), origin - (0,0,1024), false, undefined);
    if (trace["fraction"] < 1) return trace["position"] + (0,0,2);
    return origin;
}

captureSmoke()
{
    level endon("tickrate_shared_smoke_done");
    self endon("disconnect");
    self waittill("grenade_fire", grenade, weapon);
    if (!isDefined(grenade) || weapon != "smoke_grenade_mp") { fail("wrong_native_grenade"); return; }
    logPrint("TICKRATE_SMOKE;LAUNCH;" + getTime() + ";" + weapon + "\n");
    grenade waittill("explode", origin);
    level.sharedSmokeOrigin = origin;
    level.sharedSmokeDetonated = true;
    logPrint("TICKRATE_SMOKE;DETONATION;" + getTime() + ";" +
        origin[0] + ";" + origin[1] + ";" + origin[2] + "\n");
}

watchdog()
{
    level endon("tickrate_shared_smoke_done");
    wait 180;
    fail("watchdog_180_seconds");
}

cleanup()
{
    if (isDefined(level.sharedSmokeBot)) level.sharedSmokeBot botStop();
    for (i = 0; i < level.sharedSmokePlayers.size; i++)
        if (isDefined(level.sharedSmokePlayers[i])) level.sharedSmokePlayers[i] freezeControls(false);
}

fail(reason)
{
    logPrint("TICKRATE_TEST;FAILED;shared_smoke_" + reason + "\n");
    cleanup();
    level notify("tickrate_shared_smoke_done");
}
