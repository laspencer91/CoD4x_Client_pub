// Private loopback fixture. Mount as main_shared/promod/tickrate_smoke.gsx.
run()
{
    level endon("restarting");
    level endon("tickrate_retention_done");
    if (!getDvarInt("tickrate_test") || getDvar("net_ip") != "127.0.0.1") return;
    level.retentionBots = [];
    level thread watchdog();
    wait 3;
    if (getDvar("mapname") != "mp_crash") {
        fail("requires_mp_crash");
        return;
    }
    for (i = 0; i < 10; i++) {
        team = "allies";
        if (i >= 5) team = "axis";
        if (!isDefined(actor(team))) return;
    }
    wait 3;
    level.rdyup = false;
    level.forcedEnd = false;
    game["PROMOD_KNIFEROUND"] = false;
    for (i = 0; i < level.retentionBots.size; i++) {
        bot = level.retentionBots[i];
        bot closeMenu();
        bot closeInGameMenu();
        bot.archivetime = 0;
        bot freezeControls(false);
        bot enableWeapons();
        bot giveWeapon("m4_mp");
        bot switchToWeapon("m4_mp");
        bot botWeapon("m4_mp");
        bot setWeaponAmmoClip("m4_mp", 30);
        bot setWeaponAmmoStock("m4_mp", 300);
        bot.retentionMoved = false;
        bot.retentionShots = 0;
        bot thread watchShots();
    }
    wait 1;
    for (i = 0; i < level.retentionBots.size; i++) {
        bot = level.retentionBots[i];
        bot.retentionOrigin = bot getOrigin();
        bot.retentionDestination = walkLane(bot.retentionOrigin);
        if (!isDefined(bot.retentionDestination)) {
            fail("no_walk_lane_" + i);
            return;
        }
        bot botAction("+fire");
    }
    started = getTime();
    nextLog = 10000;
    setArchive(true);
    logPrint("TICKRATE_RETENTION;READY;" + started + ";10\n");
    while (getTime() - started < 80000) {
        elapsed = getTime() - started;
        phase = int(elapsed / 1000) % 2;
        for (i = 0; i < level.retentionBots.size; i++) {
            bot = level.retentionBots[i];
            if (!isDefined(bot) || !isAlive(bot)) {
                fail("actor_lost_" + i);
                return;
            }
            if (distance(bot getOrigin(), bot.retentionOrigin) > 8)
                bot.retentionMoved = true;
            destination = bot.retentionOrigin;
            if (!phase) destination = bot.retentionDestination;
            bot botMoveTo(destination);
            bot botLookAtPlayer(level.retentionBots[(i + 5) % 10], "j_spineupper");
            bot.health = 100000;
            if (bot getWeaponAmmoClip("m4_mp") < 10)
                bot setWeaponAmmoClip("m4_mp", 30);
        }
        if (elapsed >= nextLog) {
            progress(elapsed);
            nextLog += 10000;
        }
        wait 0.2;
    }
    progress(getTime() - started);
    for (i = 0; i < level.retentionBots.size; i++) {
        bot = level.retentionBots[i];
        if (!bot.retentionMoved || !bot.retentionShots) {
            fail("actor_not_active_" + i);
            return;
        }
    }
    cleanup();
    logPrint("TICKRATE_TEST;PASSED;" + getDvarInt("sv_fps") + "\n");
    level notify("tickrate_retention_done");
}

actor(team)
{
    bot = addTestClient();
    if (!isDefined(bot)) {
        fail("bot_connect");
        return undefined;
    }
    level.retentionBots[level.retentionBots.size] = bot;
    started = getTime();
    while (!isDefined(bot.hasSpawned) && getTime() - started < 5000) wait 0.05;
    if (!isDefined(bot.hasSpawned)) {
        fail("bot_initialized");
        return undefined;
    }
    bot.pers["team"] = team;
    bot.team = team;
    bot.sessionteam = team;
    bot maps\mp\gametypes\_promod::setClassChoice("assault");
    bot.pers["assault"]["loadout_primary"] = "m4";
    bot.pers["assault"]["loadout_primary_attachment"] = "none";
    bot maps\mp\gametypes\_promod::menuAcceptClass("go");
    started = getTime();
    while (bot.sessionstate != "playing" && getTime() - started < 5000) wait 0.05;
    if (bot.sessionstate != "playing") {
        fail("bot_playing");
        return undefined;
    }
    bot botStop();
    bot.maxhealth = 100000;
    bot.health = 100000;
    return bot;
}

walkLane(origin)
{
    for (span = 96; span >= 48; span -= 48) {
        for (i = 0; i < 8; i++) {
            destination = origin + anglesToForward((0, i * 45, 0)) * span;
            trace = bulletTrace(origin + (0,0,48), destination + (0,0,48), false, undefined);
            if (trace["fraction"] < 1) continue;
            ground = bulletTrace(destination + (0,0,48), destination - (0,0,64), false, undefined);
            if (ground["fraction"] >= 1 || ground["normal"][2] < 0.7) continue;
            if (abs(ground["position"][2] - origin[2]) > 16) continue;
            return ground["position"] + (0,0,2);
        }
    }
    return undefined;
}

watchShots()
{
    level endon("tickrate_retention_done");
    self endon("disconnect");
    for (;;) {
        self waittill("weapon_fired");
        self.retentionShots++;
    }
}

progress(elapsed)
{
    moved = 0;
    shots = 0;
    for (i = 0; i < level.retentionBots.size; i++) {
        moved += int(level.retentionBots[i].retentionMoved);
        shots += level.retentionBots[i].retentionShots;
    }
    logPrint("TICKRATE_RETENTION;PROGRESS;" + getTime() + ";" + elapsed + ";" + moved + ";" + shots + "\n");
}

watchdog()
{
    level endon("tickrate_retention_done");
    wait 100;
    fail("watchdog_100_seconds");
}

cleanup()
{
    for (i = 0; i < level.retentionBots.size; i++)
        if (isDefined(level.retentionBots[i])) level.retentionBots[i] botStop();
}

fail(reason)
{
    logPrint("TICKRATE_TEST;FAILED;retention_" + reason + "\n");
    cleanup();
    level notify("tickrate_retention_done");
}
