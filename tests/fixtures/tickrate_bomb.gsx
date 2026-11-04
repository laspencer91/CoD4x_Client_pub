// Private loopback S&D interaction fixture. Mount as main_shared/promod/tickrate_smoke.gsx.
run()
{
    level endon("restarting");
    level endon("tickrate_bomb_done");
    if (!getDvarInt("tickrate_test") || getDvar("net_ip") != "127.0.0.1") return;
    level.bombTestBots = [];
    level thread watchdog();
    wait 3;
    if (level.gametype != "sd" || getDvar("mapname") != "mp_crash") { fail("requires_crash_sd"); return; }
    if (!isDefined(level.sdBomb) || !level.bombZones.size || game["promod_do_readyup"]) {
        fail("default_bomb_objects_missing"); return;
    }
    if (level.plantTime != 5 || level.defuseTime != 7 || level.bombTimer != 45) {
        fail("requires_default_5_7_45"); return;
    }
    attacker = actor(game["attackers"]);
    defender = actor(game["defenders"]);
    if (!isDefined(attacker) || !isDefined(defender)) return;
    wait 3;
    level.rdyup = false;
    level.forcedEnd = false;
    game["PROMOD_KNIFEROUND"] = false;
    started = getTime();
    while (level.inPrematchPeriod && getTime() - started < 20000) wait 0.05;
    if (level.inPrematchPeriod || level.timerStopped) { fail("round_not_running"); return; }
    home = attacker.origin;
    before = maps\mp\gametypes\_globallogic::getTimePassed();
    wait 1;
    checkTime(maps\mp\gametypes\_globallogic::getTimePassed() - before, 1000, "round_clock");
    logPrint("TICKRATE_BOMB;ROUND_HUD;" + getTime() + ";" +
        int(getTime() + maps\mp\gametypes\_globallogic::getTimeRemaining()) + "\n");
    wait 0.5;
    if (!isDefined(attacker.carryObject)) {
        attacker setOrigin(floorAt(level.sdBomb.trigger.origin));
        attacker botAction("+activate");
        started = getTime();
        while (!isDefined(attacker.carryObject) && getTime() - started < 3000) wait 0.05;
        attacker botAction("-activate");
    }
    if (!isDefined(attacker.carryObject) || attacker.carryObject != level.sdBomb) {
        fail("native_bomb_pickup"); return;
    }
    logPrint("TICKRATE_BOMB;PICKUP;" + getTime() + "\n");
    zone = level.bombZones[0];
    armUse(attacker, zone.trigger);
    started = getTime();
    while (!attacker.isPlanting && !level.bombPlanted && getTime() - started < 3000) wait 0.01;
    if (!attacker.isPlanting) { inputState(attacker, zone.trigger, "plant"); fail("plant_not_started"); return; }
    plantStarted = getTime();
    logPrint("TICKRATE_BOMB;PLANT_BEGIN;" + plantStarted + ";" + zone.useTime + "\n");
    while (!level.bombPlanted && getTime() - plantStarted < 7000) wait 0.01;
    attacker botAction("-activate");
    if (!level.bombPlanted) { fail("plant_not_completed"); return; }
    planted = getTime();
    checkTime(planted - plantStarted, 5000, "actual_use_plant");
    logPrint("TICKRATE_BOMB;PLANTED;" + planted + ";" + (planted + 45000) + ";" + getDvarInt("ui_bomb_timer") + "\n");
    paused = maps\mp\gametypes\_globallogic::getTimePassed();
    wait 1;
    checkTime(maps\mp\gametypes\_globallogic::getTimePassed() - paused, 0, "planted_round_paused");
    if (!level.timerStopped) { fail("planted_round_not_paused"); return; }
    if (getDvar("tickrate_bomb_case") == "defuse") {
        attacker setOrigin(home);
        armUse(defender, zone.bombDefuseTrig);
        started = getTime();
        while (!defender.isDefusing && getTime() - started < 3000) wait 0.01;
        if (!defender.isDefusing) { inputState(defender, zone.bombDefuseTrig, "defuse"); fail("defuse_not_started"); return; }
        defuseStarted = getTime();
        logPrint("TICKRATE_BOMB;DEFUSE_BEGIN;" + defuseStarted + "\n");
        while (!level.bombDefused && getTime() - defuseStarted < 9000) wait 0.01;
        defender botAction("-activate");
        if (!level.bombDefused) { fail("defuse_not_completed"); return; }
        checkTime(getTime() - defuseStarted, 7000, "actual_use_defuse");
        logPrint("TICKRATE_BOMB;DEFUSED;" + getTime() + ";" + getDvarInt("ui_bomb_timer") + "\n");
    } else {
        while (!level.bombExploded && getTime() - planted < 47000) wait 0.01;
        if (!level.bombExploded || level.bombDefused) { fail("bomb_not_expired"); return; }
        checkTime(getTime() - planted, 45000, "actual_bomb_expiry");
        logPrint("TICKRATE_BOMB;EXPLODED;" + getTime() + ";" + getDvarInt("ui_bomb_timer") + "\n");
    }
    wait 0.1;
    cleanup();
    if (!isDefined(level.bombTestFailed)) logPrint("TICKRATE_TEST;PASSED;" + getDvarInt("sv_fps") + "\n");
    level notify("tickrate_bomb_done");
}

actor(team)
{
    bot = addTestClient();
    if (!isDefined(bot)) { fail("bot_connect"); return undefined; }
    level.bombTestBots[level.bombTestBots.size] = bot;
    started = getTime();
    while (!isDefined(bot.hasSpawned) && getTime() - started < 5000) wait 0.05;
    if (!isDefined(bot.hasSpawned)) { fail("bot_initialized"); return undefined; }
    bot.pers["team"] = team;
    bot.team = team;
    bot.sessionteam = team;
    bot maps\mp\gametypes\_promod::setClassChoice("assault");
    bot.pers["assault"]["loadout_primary"] = "m4";
    bot.pers["assault"]["loadout_primary_attachment"] = "none";
    bot maps\mp\gametypes\_promod::menuAcceptClass("go");
    started = getTime();
    while (bot.sessionstate != "playing" && getTime() - started < 5000) wait 0.05;
    if (bot.sessionstate != "playing") { fail("bot_playing"); return undefined; }
    bot botStop();
    bot botWeapon("m4_mp");
    bot.maxhealth = 100000;
    bot.health = 100000;
    bot.archivetime = 0;
    bot freezeControls(false);
    bot enableWeapons();
    return bot;
}

armUse(bot, trigger)
{
    bot botStop();
    bot botWeapon("m4_mp");
    bot.archivetime = 0;
    bot freezeControls(false);
    bot enableWeapons();
    for (spot = 0; spot < 8; spot++) {
        position = trigger.origin + anglesToForward((0,spot * 45,0)) * 48;
        bot setOrigin(floorAt(position));
        bot setVelocity((0,0,0));
        bot botLookAt(trigger.origin + (0,0,8));
        wait 0.3;
        inputState(bot, trigger, "settle_" + spot);
        if (bot isOnGround() && bot isTouching(trigger)) break;
    }
    bot botStop();
    bot botWeapon("m4_mp");
    inputState(bot, trigger, "before_activate");
    bot botAction("+activate");
}

floorAt(origin)
{
    trace = bulletTrace(origin + (0,0,64), origin - (0,0,1024), false, undefined);
    if (trace["fraction"] < 1) return trace["position"] + (0,0,2);
    return origin;
}

inputState(bot, trigger, phase)
{
    logPrint("TICKRATE_BOMB;INPUT;" + phase + ";" + getTime() + ";" +
        bot useButtonPressed() + ";" + bot isTouching(trigger) + ";" + bot isOnGround() + ";" + bot getCurrentWeapon() + ";" +
        bot.origin + ";" + trigger.origin + ";" + bot getVelocity() + "\n");
}

checkTime(actual, expected, name)
{
    logPrint("TICKRATE_BOMB;TIME;" + name + ";" + actual + ";" + expected + "\n");
    if (abs(actual - expected) > 50 + int(1000 / getDvarInt("sv_fps"))) fail(name);
}

watchdog()
{
    level endon("tickrate_bomb_done");
    wait 110;
    fail("watchdog_110_seconds");
}

cleanup()
{
    for (i = 0; i < level.bombTestBots.size; i++)
        if (isDefined(level.bombTestBots[i])) level.bombTestBots[i] botStop();
}

fail(reason)
{
    level.bombTestFailed = true;
    logPrint("TICKRATE_TEST;FAILED;bomb_" + reason + "\n");
    cleanup();
    level notify("tickrate_bomb_done");
}
