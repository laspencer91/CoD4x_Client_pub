[CmdletBinding()]
param(
    [string]$GamePath = '',          # the PC's Call of Duty 4 folder (read only); detected when empty
    [string]$Map = 'mp_crash',       # a map with an occupancy grid in the kit
    [switch]$Fullscreen,
    [switch]$HideWindow,             # developer use: keep the game window off-screen
    [switch]$Automatic,              # developer use: no prompt; 20 s with smoke, then the report
    [int]$Port = 28970               # 28970-28979; the server uses Port and Port+1, the client Port+5 and Port+6
)
# Volumetric smoke test kit: runs a self-built CoD4x client with the smoke renderer against a private
# CoD4x server on 127.0.0.1, using this PC's CoD4 1.7 install read only. Everything it writes stays in
# this kit folder. Nothing is installed; when it ends, nothing it started is left running.
$ErrorActionPreference = 'Stop'
$kit = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ($Port -lt 28970 -or $Port -gt 28979) { throw 'Use a port from 28970 to 28979.' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$run = Join-Path $kit "runs\$stamp"
$reportDir = Join-Path $kit 'reports'
New-Item -ItemType Directory -Force -Path $run, $reportDir | Out-Null
$started = [System.Collections.Generic.List[int]]::new()
$notes = [System.Collections.Generic.List[string]]::new()
function Say([string]$text) { Write-Host $text; $notes.Add("$(Get-Date -Format 'HH:mm:ss') $text") }

# 1. Find the game (read only).
function Test-Game([string]$path) { $path -and (Test-Path -LiteralPath (Join-Path $path 'iw3mp.exe')) -and (Test-Path -LiteralPath (Join-Path $path 'main\iw_00.iwd')) }
if (-not (Test-Game $GamePath)) {
    $candidates = @()
    foreach ($key in 'HKLM:\SOFTWARE\WOW6432Node\Activision\Call of Duty 4', 'HKLM:\SOFTWARE\Activision\Call of Duty 4') {
        $value = (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).InstallPath
        if ($value) { $candidates += $value }
    }
    $steam = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if ($steam) {
        $libraries = @($steam)
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) { $libraries += [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"') | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' } }
        $candidates += $libraries | ForEach-Object { Join-Path $_ 'steamapps\common\Call of Duty 4' }
    }
    $candidates += 'C:\Program Files (x86)\Activision\Call of Duty 4 - Modern Warfare', 'C:\Games\Call of Duty 4'
    $GamePath = $candidates | Where-Object { Test-Game $_ } | Select-Object -First 1
    if (-not $GamePath) {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = 'Choose your Call of Duty 4 folder (the one with iw3mp.exe). It is only read.'
        if ($dialog.ShowDialog() -ne 'OK' -or -not (Test-Game $dialog.SelectedPath)) { throw 'No Call of Duty 4 folder chosen.' }
        $GamePath = $dialog.SelectedPath
    }
}
$GamePath = (Resolve-Path -LiteralPath $GamePath).Path
Say "Game folder (read only): $GamePath"

# 2. The executable must be CoD4 MP 1.7 (PE time stamp 0x4859A219).
$exe = Join-Path $GamePath 'iw3mp.exe'
$bytes = [IO.File]::ReadAllBytes($exe)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$timestamp = [BitConverter]::ToUInt32($bytes, $pe + 8)
if ($timestamp -ne 0x4859A219) { throw ('iw3mp.exe is not version 1.7 (time stamp 0x{0:X8}).' -f $timestamp) }
$exeHash = (Get-FileHash -LiteralPath $exe).Hash.ToLower()

# 3. A private start folder for the executable (the game folder itself is never written).
$start = Join-Path $run 'exe'
New-Item -ItemType Directory -Force -Path $start | Out-Null
Copy-Item -LiteralPath $exe -Destination $start
foreach ($name in 'binkw32.dll', 'd3dx9_34.dll', 'localization.txt') {
    $source = Join-Path $GamePath $name
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $start }
}
# Miles: mss32.dll is Miles on a plain install; with CoD4x installed it is the CoD4x loader and Miles is miles32.dll.
$miles = @('mss32.dll', 'miles32.dll') | ForEach-Object { Join-Path $GamePath $_ } |
    Where-Object { (Test-Path -LiteralPath $_) -and (Get-Item -LiteralPath $_).Length -eq 434688 } | Select-Object -First 1
if (-not $miles) { throw 'The Miles sound library (434,688-byte mss32.dll or miles32.dll) was not found in the game folder.' }
Copy-Item -LiteralPath $miles -Destination (Join-Path $start 'mss32.dll')
Copy-Item -LiteralPath $miles -Destination (Join-Path $start 'miles32.dll')
if (Test-Path -LiteralPath (Join-Path $GamePath 'miles')) { Copy-Item -LiteralPath (Join-Path $GamePath 'miles') -Destination $start -Recurse }
# The engine opens its fast files relative to the executable's folder: link (not copy) the game's zone folder.
New-Item -ItemType Junction -Path (Join-Path $start 'zone') -Target (Join-Path $GamePath 'zone') | Out-Null

# 4. Private client data: app data (CoD4x zones, grids, profile) and home path.
$appdata = Join-Path $run 'appdata'
Copy-Item -LiteralPath (Join-Path $kit 'data\appdata') -Destination $appdata -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $appdata 'players\profiles\SmokeTest') | Out-Null
Set-Content -LiteralPath (Join-Path $appdata 'players\profiles\active.txt') -Value 'SmokeTest' -NoNewline -Encoding ASCII
Set-Content -LiteralPath (Join-Path $appdata 'players\profiles\SmokeTest\config_mp.cfg') -Value '' -Encoding ASCII
$homePath = Join-Path $run 'home'
New-Item -ItemType Directory -Force -Path (Join-Path $homePath 'main') | Out-Null
Set-Content -LiteralPath (Join-Path $homePath 'main\smoketest.cfg') -Value 'set smoketest_ready 1' -Encoding ASCII
$renderer = Join-Path $kit 'bin\vsmoke'
foreach ($name in 'client.log', 'control.txt') { Remove-Item -LiteralPath (Join-Path $renderer $name) -ErrorAction SilentlyContinue }

function Get-Text([string]$folder, [string]$filter) {
    (Get-ChildItem -LiteralPath $folder -Recurse -Filter $filter -ErrorAction SilentlyContinue |
        ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue }) -join "`n"
}
function Wait-Text([string]$folder, [string]$filter, [string]$pattern, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) { if ((Get-Text $folder $filter) -match $pattern) { return $true }; Start-Sleep -Milliseconds 500 }
    $false
}
function Send-Game([string[]]$commands) {
    $target = Join-Path $homePath 'commands.txt'
    $deadline = (Get-Date).AddSeconds(10)
    while ((Test-Path -LiteralPath $target) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    Set-Content -LiteralPath "$target.part" -Value ($commands -join "`n") -Encoding ASCII
    Move-Item -LiteralPath "$target.part" -Destination $target -Force
    Start-Sleep -Milliseconds 600
}
function Send-Renderer([string]$command) {
    Set-Content -LiteralPath (Join-Path $renderer 'control.txt') -Value $command -Encoding ASCII
    $deadline = (Get-Date).AddSeconds(5)
    while ((Test-Path -LiteralPath (Join-Path $renderer 'control.txt')) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
}

$client = $null
try {
    # 5. Private server on 127.0.0.1 with the smoke policy.
    $serverHome = Join-Path $run 'server-home'
    Copy-Item -LiteralPath (Join-Path $kit 'server\home') -Destination $serverHome -Recurse
    Set-Content -LiteralPath (Join-Path $serverHome 'main\smoketest_server.cfg') -Value 'sets sv_volumetricSmoke "v1"' -Encoding ASCII
    $serverArgs = @('+set', 'fs_basepath', "`"$GamePath`"", '+set', 'fs_homepath', "`"$serverHome`"", '+set', 'dedicated', '1',
        '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', [string]$Port, '+set', 'net_ip6', '::1', '+set', 'net_port6', [string]($Port + 1),
        '+set', 'sv_punkbuster', '0', '+set', 'sv_maxclients', '4', '+set', 'sv_pure', '0', '+set', 'g_gametype', 'dm',
        '+set', 'scr_dm_timelimit', '0', '+set', 'scr_dm_scorelimit', '0', '+set', 'logfile', '2', '+set', 'sv_cheats', '1',
        '+exec', 'smoketest_server.cfg', '+map', $Map)
    $server = Start-Process -FilePath (Join-Path $kit 'server\bin\cod4x18_dedrun.exe') -ArgumentList $serverArgs `
        -WorkingDirectory (Join-Path $kit 'server\bin') -WindowStyle Hidden -PassThru
    $started.Add($server.Id)
    Say 'Starting the private test server on 127.0.0.1...'
    if (-not (Wait-Text $serverHome 'qconsole.log' 'CRC32 for zone' 180)) { throw 'The test server did not load the map.' }

    # 6. The client: the kit's CoD4x build with the smoke bridge, loaded by the kit's own loader.
    $port6 = $Port + 6
    $clientArgs = @("`"$start`"", "`"$(Join-Path $kit 'bin\cod4x_021.dll')`"",
        '+set', 'fs_basepath', "`"$GamePath`"", '+set', 'fs_homepath', "`"$homePath`"", '+set', 'r_fullscreen', $(if ($Fullscreen) { '1' } else { '0' }),
        '+set', 'com_introplayed', '1', '+set', 'logfile', '2', '+set', 'developer', '1', '+set', 'ui_playerProfileAlreadyChosen', '1',
        '+set', 'cl_punkbuster', '0', '+set', 'net_ip', '127.0.0.1', '+set', 'net_ip6', '::1', '+set', 'net_port', [string]($Port + 5),
        '+set', 'net_port6', [string]$port6, '+set', 'name', 'SmokeTester', '+exec', 'smoketest.cfg')
    $env:COD4X_APPDATA_DIR = $appdata
    $env:COD4X_TEST_COMMANDS = Join-Path $homePath 'commands.txt'
    $pidFile = Join-Path $run 'client-pid.txt'
    $runner = Start-Process -FilePath (Join-Path $kit 'bin\cod4x_run.exe') -ArgumentList $clientArgs -RedirectStandardOutput $pidFile -WindowStyle Hidden -PassThru
    Remove-Item Env:\COD4X_APPDATA_DIR, Env:\COD4X_TEST_COMMANDS
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $deadline -and -not ((Test-Path $pidFile) -and (Get-Content $pidFile -ErrorAction SilentlyContinue))) { Start-Sleep -Milliseconds 250 }
    $client = [int](Get-Content $pidFile | Select-Object -First 1)
    $started.Add($client); $started.Add($runner.Id)
    if ($HideWindow) {
        $mover = Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-NonInteractive', '-File',
            "`"$(Join-Path $PSScriptRoot 'Move-GameWindows.ps1')`"", '-ProcessId', [string]$client, '-Seconds', '1800')
        $started.Add($mover.Id)
    }
    Say 'Starting the game...'
    if (-not (Wait-Text $homePath 'console_mp.log' 'execing smoketest.cfg' 120)) { throw 'The game did not start (see the console log in this run folder).' }
    Send-Game @("connect 127.0.0.1:$Port")
    if (-not (Wait-Text $renderer 'client.log' 'occupancy grid loaded|no occupancy grid|policy rejected' 120)) { Say 'The renderer did not report a map within two minutes.' }
    Start-Sleep 5
    Send-Game @('volumetricsmoke_status')

    # 7. Measurements: without smoke, then with a cloud at a fixed spot in front of a fixed camera.
    Say 'Measuring frame time without smoke (10 s)...'
    Send-Renderer 'depth_probe'
    Send-Renderer 'profile_reset'; Start-Sleep 10; Send-Renderer 'profile_detail'; Send-Renderer 'profile'
    Send-Game @('setviewpos 336 60 208 65 0')
    Send-Renderer 'spawn 505 422 210'
    Say 'A smoke cloud is growing in front of you; measuring with smoke (10 s after 12 s growth)...'
    Start-Sleep 12
    Send-Renderer 'profile_reset'; Start-Sleep 10; Send-Renderer 'profile_detail'; Send-Renderer 'profile'
    Send-Game @('volumetricsmoke_status')

    # 8. The tester's turn.
    Write-Host ''
    Write-Host 'Now play: open the game window, look at the smoke from different angles and distances,'
    Write-Host 'walk into it, check its edges against walls, floors and doorways, look for flicker, and'
    Write-Host 'shoot through it (bullets should leave holes). Throw your own smoke if your class has one.'
    Write-Host 'In this window: type s and Enter for another cloud at the test spot, or just Enter when done.'
    if ($Automatic) { Start-Sleep 20 }
    else { while ((Read-Host 'Enter = finish, s = another cloud') -eq 's') {
        Send-Renderer 'spawn 505 422 210'
        Say 'Spawned another cloud at the test spot.'
    } }
    Send-Renderer 'profile_detail'
    Send-Game @('volumetricsmoke_status')
    Start-Sleep 3
    Send-Game @('quit')
    $null = (Get-Process -Id $client -ErrorAction SilentlyContinue) | ForEach-Object { $_.WaitForExit(30000) }
}
catch { Say "Problem: $($_.Exception.Message)" }
finally {
    foreach ($id in $started) { if (Get-Process -Id $id -ErrorAction SilentlyContinue) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue } }
    # Remove the zone link itself (never its target) so the run folder can be deleted safely.
    $zoneLink = Join-Path $start 'zone'
    Start-Sleep 1
    if (Test-Path -LiteralPath $zoneLink) { [IO.Directory]::Delete($zoneLink, $false) }

    # 9. Report.
    $gpu = Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) | driver $($_.DriverVersion) ($($_.DriverDate)) | $($_.PNPDeviceID)" }
    $os = Get-CimInstance Win32_OperatingSystem
    $console = Get-Text $homePath 'console_mp.log'
    $lines = [regex]::Matches($console, '(?m)^.*(Smoke:|Volumetric smoke|ERROR|Error:|Direct3D|Shader model).*$') | ForEach-Object Value
    $rendererLog = Get-Content -LiteralPath (Join-Path $renderer 'client.log') -ErrorAction SilentlyContinue
    $crash = Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = (Get-Date).AddHours(-2) } -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match 'iw3mp' } | Select-Object -First 3 | ForEach-Object { $_.Message }
    $report = Join-Path $reportDir "smoke-report-$env:COMPUTERNAME-$stamp.txt"
    @(
        'Volumetric smoke test kit report', "Date: $(Get-Date -Format o)", "Windows: $($os.Caption) $($os.Version)",
        'GPU:', ($gpu | ForEach-Object { "  $_" }), "Game folder: $GamePath", "iw3mp.exe sha256: $exeHash", "Map: $Map",
        '', '== Kit steps', $notes,
        '', '== Game console (smoke, errors, renderer)', $lines,
        '', '== Renderer log (adapter, depth mode, shaders, grid, spawns, carving, frame and GPU timings)', $rendererLog,
        '', '== Crashes reported by Windows for iw3mp.exe in the last two hours', $(if ($crash) { $crash } else { 'none' })
    ) | Set-Content -LiteralPath $report -Encoding UTF8
    Write-Host ''
    Write-Host "Report written: $report"
    Write-Host 'Please send that file back. Nothing was installed and nothing the kit started is still running.'
}
