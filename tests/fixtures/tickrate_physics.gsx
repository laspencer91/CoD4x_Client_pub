// Private loopback fixture. Mount as main_shared/promod/tickrate_smoke.gsx.
// Uses real bot throws: CoD4x has no ownerless magicGrenade script builtin.
run()
{
    level endon("restarting");
    level endon("tickrate_physics_done");
    if (!getDvarInt("tickrate_test") || getDvar("net_ip") != "127.0.0.1") return;
    level.physicsBots = [];
    level thread watchdog();
    wait 3;

    if (getDvar("mapname") != "mp_crash") {
        fail("requires_mp_crash");
        return;
    }
    logPrint("TICKRATE_PHYSICS;BEGIN;" + getDvarInt("sv_fps") + ";" + getDvar("g_gravity") + "\n");
    logPrint("TICKRATE_PHYSICS;LIMIT;bot_owner;native_fuse;native_velocity;compare_launch_pose_and_first_origin\n");
    bot = actor("allies");
    if (!isDefined(bot)) return;
    opponent = actor("axis");
    if (!isDefined(opponent)) return;
    opponent freezeControls(true);
    opponent disableWeapons();

    // Let spawn/loadout threads finish before leaving the ready-up restrictions.
    wait 3;
    level.rdyup = false;
    level.forcedEnd = false;
    game["PROMOD_KNIFEROUND"] = false;
    bot freezeControls(false);
    bot enableWeapons();
    wait 1;

    points = getEntArray("mp_sd_spawn_attacker", "classname");
    if (!points.size) {
        fail("no_spawn_anchor");
        return;
    }
    base = floorAt(points[0].origin);
    yaw = points[0].angles[1];
    flat = (0, yaw, 0);
    if (!throwCase(bot, "flat", base, flat)) return;
    if (!throwCase(bot, "drop", base, (80, yaw, 0))) return;

    wall = nearbyWall(base);
    if (isDefined(wall)) {
        if (!throwCase(bot, "near_wall", wall.origin, wall.angles)) return;
    } else {
        logPrint("TICKRATE_PHYSICS;SKIP;near_wall;no_horizontal_wall_within_192\n");
        if (!throwCase(bot, "flat_repeat", base, flat)) return;
    }
    cleanup();
    logPrint("TICKRATE_TEST;PASSED;" + getDvarInt("sv_fps") + "\n");
    level notify("tickrate_physics_done");
}

actor(team)
{
    bot = addTestClient();
    if (!isDefined(bot)) {
        fail("bot_connect");
        return undefined;
    }
    level.physicsBots[level.physicsBots.size] = bot;
    deadline = getTime() + 5000;
    while (!isDefined(bot.hasSpawned) && getTime() < deadline) wait 0.05;
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
    deadline = getTime() + 5000;
    while (bot.sessionstate != "playing" && getTime() < deadline) wait 0.05;
    if (bot.sessionstate != "playing") {
        fail("bot_playing");
        return undefined;
    }
    bot botStop();
    bot.maxhealth = 100000;
    bot.health = 100000;
    return bot;
}

floorAt(origin)
{
    trace = bulletTrace(origin + (0,0,64), origin - (0,0,1024), false, undefined);
    if (trace["fraction"] < 1) return trace["position"] + (0,0,2);
    return origin;
}

nearbyWall(origin)
{
    start = origin + (0,0,55);
    for (i = 0; i < 8; i++) {
        direction = anglesToForward((0, i * 45, 0));
        trace = bulletTrace(start, start + direction * 192, false, undefined);
        if (trace["fraction"] >= 1 || abs(trace["normal"][2]) > 0.25) continue;
        if (distance(start, trace["position"]) < 48) continue;
        wall = spawnStruct();
        wall.origin = floorAt(trace["position"] + trace["normal"] * 48 - (0,0,55));
        wall.angles = vectorToAngles(trace["normal"] * -1);
        wall.angles = (0, wall.angles[1], 0);
        return wall;
    }
    return undefined;
}

throwCase(bot, name, origin, angles)
{
    bot botStop();
    bot closeMenu();
    bot closeInGameMenu();
    bot freezeControls(false);
    bot enableWeapons();
    bot.archivetime = 0;
    bot.health = 100000;
    bot setOrigin(origin);
    bot setVelocity((0,0,0));
    bot setPlayerAngles(angles);
    bot giveWeapon("m4_mp");
    bot switchToWeapon("m4_mp");
    bot botWeapon("m4_mp");
    bot giveWeapon("frag_grenade_mp");
    bot setWeaponAmmoClip("frag_grenade_mp", 0);
    bot setWeaponAmmoStock("frag_grenade_mp", 1);
    bot switchToOffhand("frag_grenade_mp");
    wait 1;

    level.physicsLaunchTime = undefined;
    level.physicsExplodeTime = undefined;
    level.physicsComplete = false;
    bot thread captureGrenade(name);
    // Both intervals are exact at 20 and 100 Hz. Weapon animation scheduling
    // can still change the native launch frame; the launch record exposes it.
    throwState(bot, name, "before_press");
    if (bot getWeaponAmmoStock("frag_grenade_mp") < 1) {
        fail(name + "_no_frag_ammo");
        return false;
    }
    bot botAction("+frag");
    wait 0.2;
    throwState(bot, name, "held_200ms");
    wait 0.6;
    bot botAction("-frag");
    throwState(bot, name, "released");
    deadline = getTime() + 2500;
    while (!isDefined(level.physicsLaunchTime) && getTime() < deadline) wait 0.05;
    if (!isDefined(level.physicsLaunchTime)) {
        fail(name + "_no_grenade_fire");
        return false;
    }
    deadline = getTime() + 6500;
    while (!level.physicsComplete && getTime() < deadline) wait 0.05;
    if (!level.physicsComplete) {
        fail(name + "_sample_timeout");
        return false;
    }
    wait 0.2;
    return true;
}

throwState(bot, name, phase)
{
    logPrint("TICKRATE_PHYSICS;INPUT;" + name + ";" + phase + ";" + getTime() + ";" +
        bot.sessionstate + ";" + bot getCurrentWeapon() + ";" + bot getCurrentOffhand() + ";" +
        bot fragButtonPressed() + ";" + bot getWeaponAmmoClip("frag_grenade_mp") + ";" +
        bot getWeaponAmmoStock("frag_grenade_mp") + ";" + bot.archivetime + "\n");
}

captureGrenade(name)
{
    level endon("tickrate_physics_done");
    self endon("disconnect");
    // The actual native notify is grenade_fire, not grenade_fired.
    self waittill("grenade_fire", grenade, weapon);
    if (!isDefined(grenade) || weapon != "frag_grenade_mp") {
        fail(name + "_wrong_grenade");
        return;
    }
    level.physicsLaunchTime = getTime();
    position = self getOrigin();
    angles = self getPlayerAngles();
    origin = grenade getOrigin();
    logPrint("TICKRATE_PHYSICS;LAUNCH;" + name + ";" + getDvarInt("sv_fps") + ";" +
        getTime() + ";" + (getTime() % 50) + ";" + weapon + ";" +
        position[0] + ";" + position[1] + ";" + position[2] + ";" +
        angles[0] + ";" + angles[1] + ";" + angles[2] + ";" +
        origin[0] + ";" + origin[1] + ";" + origin[2] + "\n");
    // Sampling begins in the notify thread, so both rates record t=0,50,100...
    // independently of when the controlling thread sees the launch.
    started = level.physicsLaunchTime;
    grenade thread recordExplosion(name, started);
    samples = 0;
    while (isDefined(grenade) && getTime() - started <= 6000) {
        origin = grenade getOrigin();
        logPrint("TICKRATE_PHYSICS;SAMPLE;" + name + ";" + getDvarInt("sv_fps") + ";" +
            (getTime() - started) + ";" + origin[0] + ";" + origin[1] + ";" + origin[2] + "\n");
        samples++;
        wait 0.05;
    }
    logPrint("TICKRATE_PHYSICS;END;" + name + ";" + getDvarInt("sv_fps") + ";" +
        (getTime() - started) + ";" + samples + ";" + isDefined(grenade) + "\n");
    if (isDefined(grenade) || samples < 2 || !isDefined(level.physicsExplodeTime)) {
        fail(name + "_grenade_lifetime");
        return;
    }
    level.physicsComplete = true;
}

recordExplosion(name, started)
{
    level endon("tickrate_physics_done");
    self waittill("explode", origin);
    level.physicsExplodeTime = getTime();
    logPrint("TICKRATE_PHYSICS;EXPLODE;" + name + ";" + getDvarInt("sv_fps") + ";" +
        (getTime() - started) + ";" + origin[0] + ";" + origin[1] + ";" + origin[2] + "\n");
}

watchdog()
{
    level endon("tickrate_physics_done");
    wait 45;
    fail("watchdog_45_seconds");
}

cleanup()
{
    for (i = 0; i < level.physicsBots.size; i++) {
        bot = level.physicsBots[i];
        if (isDefined(bot)) bot botStop();
    }
}

fail(reason)
{
    logPrint("TICKRATE_TEST;FAILED;physics_" + reason + "\n");
    cleanup();
    level notify("tickrate_physics_done");
}
