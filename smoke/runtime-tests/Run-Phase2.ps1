[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Runtime,                 # folder made by Stage-Runtime.ps1
    [ValidateSet('R1','R2','R3','R4a','R5','R5u','R6','R7')][string[]]$Run = @('R1'),
    # '' = minimised, non-activated windows on the current desktop. D3D9 cannot create a device
    # on a separate (non-input) desktop, so the game cannot be moved to one.
    [string]$Desktop = '',
    [ValidateRange(28970, 28979)][int]$ServerPort = 28970,
    [string]$RealVariant = 'real',                         # client folder used as the real renderer
    [string]$Class = 'sniper_mp,0',                         # default class with a smoke grenade (stock rules)
    [switch]$CrashCatch,                                    # attach crashcatch.exe to each client
    [switch]$AllowRemoteSession
)
# Runtime tests for the volumetric smoke bridge. Every process is started from the private
# runtime, bound to 127.0.0.1 / ::1 on ports 28970-28979, and stopped by its own process id.
# Clients are TEST_ISOLATION builds: app data from COD4X_APPDATA_DIR, no Steam, Discord or
# registry associations, and commands handed over through the COD4X_TEST_COMMANDS file
# (the stock "wait" command does not delay commands executed from the start-up config).
$ErrorActionPreference = 'Stop'
$Runtime = (Resolve-Path -LiteralPath $Runtime).Path
$game = Join-Path $Runtime 'game'
if (-not $AllowRemoteSession -and (& query.exe session 2>$null | Select-String '^>rdp-tcp')) {
    throw 'This session is a Remote Desktop session. Run from the console or pass -AllowRemoteSession.'
}
$started = [System.Collections.Generic.List[int]]::new()
$results = [System.Collections.Generic.List[object]]::new()

function New-RunFolder([string]$name) {
    $folder = Join-Path $Runtime ('logs\' + $name + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $folder
}

function Get-Log([string]$homePath) {
    $files = Get-ChildItem -LiteralPath $homePath -Recurse -Filter '*.log' -ErrorAction SilentlyContinue
    ($files | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue }) -join "`n"
}

function Wait-Log([string]$homePath, [string]$pattern, [int]$seconds = 120) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if ((Get-Log $homePath) -match $pattern) { return $true }
        Start-Sleep -Milliseconds 500
    }
    $false
}

function Wait-Exit([int]$processId, [int]$seconds = 60) {
    $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
    if (-not $process) { return $true }
    $process.WaitForExit($seconds * 1000)
}

function Start-Client([string]$folder, [string]$variant, [string]$label, [string]$name = 'VSmokeA') {
    $appdata = Join-Path $folder "appdata-$label"
    & robocopy.exe (Join-Path $Runtime 'appdata') $appdata /E /NFL /NDL /NJH /NJS /NP | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $appdata 'players\profiles\VSmokeTest') | Out-Null
    Set-Content -LiteralPath (Join-Path $appdata 'players\profiles\active.txt') -Value 'VSmokeTest' -NoNewline -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $appdata 'players\profiles\VSmokeTest\config_mp.cfg') -Value '' -Encoding ASCII
    $homePath = Join-Path $folder "home-$label"
    New-Item -ItemType Directory -Force -Path (Join-Path $homePath 'mods\vsmoketest') | Out-Null
    Set-Content -LiteralPath (Join-Path $homePath 'mods\vsmoketest\vsmoke_run.cfg') -Value 'set vsmoke_test_ready 1' -Encoding ASCII
    $port = $ServerPort + $(if ($label -eq 'b') { 7 } else { 5 })
    $arguments = @('+set', 'fs_basepath', "`"$game`"", '+set', 'fs_homepath', "`"$homePath`"", '+set', 'fs_game', 'mods/vsmoketest',
        '+set', 'r_fullscreen', '0', '+set', 'com_introplayed', '1', '+set', 'logfile', '2', '+set', 'developer', '1',
        '+set', 'ui_playerProfileAlreadyChosen', '1', '+set', 'cl_punkbuster', '0', '+set', 'sv_punkbuster', '0',
        '+set', 'net_ip', '127.0.0.1', '+set', 'net_ip6', '::1', '+set', 'net_port', [string]$port, '+set', 'net_port6', [string]($port + 1),
        '+set', 'name', $name, '+set', 'com_maxfps', '60', '+exec', 'vsmoke_run.cfg')
    $env:COD4X_APPDATA_DIR = $appdata
    $env:COD4X_TEST_COMMANDS = Join-Path $homePath 'commands.txt'
    $runnerArgs = @()
    if ($Desktop) { $runnerArgs += @('--desktop', $Desktop) }
    $runnerArgs += @("`"$game`"", "`"$(Join-Path $Runtime "clients\$variant\cod4x_021.dll")`"") + $arguments
    $out = Join-Path $folder "runner-$label.txt"
    $runner = Start-Process -FilePath (Join-Path $Runtime 'cod4x_run.exe') -ArgumentList $runnerArgs -RedirectStandardOutput $out -WindowStyle Hidden -PassThru
    Remove-Item Env:\COD4X_APPDATA_DIR, Env:\COD4X_TEST_COMMANDS
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $deadline -and -not ((Test-Path $out) -and (Get-Content $out -ErrorAction SilentlyContinue))) { Start-Sleep -Milliseconds 250 }
    $gamePid = [int](Get-Content $out | Select-Object -First 1)
    $started.Add($gamePid); $started.Add($runner.Id)
    # Keep the game's windows off-screen and never activated for the whole run.
    $mover = Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-NonInteractive', '-File',
        "`"$(Join-Path $PSScriptRoot 'Move-GameWindows.ps1')`"", '-ProcessId', [string]$gamePid, '-Seconds', '900')
    $started.Add($mover.Id)
    if ($CrashCatch) {
        # Records the faulting address, registers, stack and modules if the client crashes.
        $catcher = Start-Process -FilePath (Join-Path $Runtime 'crashcatch.exe') -ArgumentList ([string]$gamePid) `
            -RedirectStandardOutput (Join-Path $folder "crash-$label.txt") -WindowStyle Hidden -PassThru
        $started.Add($catcher.Id)
    }
    $client = [pscustomobject]@{ Pid = $gamePid; Home = $homePath; Name = $name }
    $null = Wait-Log $homePath 'execing vsmoke_run.cfg' 90
    $client
}

# TEST_ISOLATION clients read COD4X_TEST_COMMANDS every 250 ms and run its lines.
function Send-Commands($client, [string[]]$commands) {
    $target = Join-Path $client.Home 'commands.txt'
    $deadline = (Get-Date).AddSeconds(10)
    while ((Test-Path -LiteralPath $target) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    $temporary = $target + '.part'
    Set-Content -LiteralPath $temporary -Value ($commands -join "`n") -Encoding ASCII
    Move-Item -LiteralPath $temporary -Destination $target -Force
    Start-Sleep -Milliseconds 600
}

function Start-Server([string]$folder, [string]$map, [string]$policy, [string]$fsGame = '', [switch]$Devmap) {
    $serverHome = Join-Path $folder "server-home-$map"
    & robocopy.exe (Join-Path $Runtime 'server\home') $serverHome /E /NFL /NDL /NJH /NJS /NP | Out-Null
    $arguments = @('+set', 'fs_basepath', "`"$game`"", '+set', 'fs_homepath', "`"$serverHome`"", '+set', 'dedicated', '1',
        '+set', 'net_ip', '127.0.0.1', '+set', 'net_port', [string]$ServerPort, '+set', 'net_ip6', '::1', '+set', 'net_port6', [string]($ServerPort + 1),
        '+set', 'sv_punkbuster', '0', '+set', 'sv_maxclients', '4', '+set', 'sv_pure', '0', '+set', 'g_gametype', 'war',
        '+set', 'scr_war_timelimit', '0', '+set', 'scr_war_scorelimit', '0', '+set', 'logfile', '2',
        '+set', 'scr_game_playerwaittime', '0', '+set', 'scr_game_matchstarttime', '0')
    if ($fsGame) { $arguments += @('+set', 'fs_game', $fsGame) }
    # Map Kit's test-server rules for the PromodFlex mod (no platform plugin), plus, when given, the
    # documented admin step for smoke: one "sets" line in a server config.
    $config = @('set promod_mode "custom_public"', 'set promod_economy "0"', 'set cp_platform_enabled "0"', 'set sv_allowDownload "0"')
    if ($policy) { $config += "sets sv_volumetricSmoke `"$policy`"" }
    Set-Content -LiteralPath (Join-Path $serverHome 'main\vsmoke_server.cfg') -Value $config -Encoding ASCII
    $arguments += @('+exec', 'vsmoke_server.cfg')
    # "+devmap" stalls this server copy after loading common_mp; cheats are enabled at start-up instead.
    if ($Devmap) { $arguments += @('+set', 'sv_cheats', '1') }
    $arguments += @('+map', $map)
    $server = Start-Process -FilePath (Join-Path $Runtime 'server\bin\cod4x18_dedrun.exe') -ArgumentList $arguments `
        -WorkingDirectory (Join-Path $Runtime 'server\bin') -WindowStyle Hidden -PassThru
    $started.Add($server.Id)
    $s = [pscustomobject]@{ Pid = $server.Id; Home = $serverHome }
    $null = Wait-Log $serverHome 'CRC32 for zone' 150
    $s
}

function Connect-Client($client, $server) {
    Send-Commands $client @("connect 127.0.0.1:$ServerPort")
    Wait-Log $server.Home ([regex]::Escape($client.Name)) 120
}

function Join-Game($client, [switch]$Spectate) {
    if ($Spectate) { Send-Commands $client @('openscriptmenu team_marinesopfor spectator'); Start-Sleep 3; return }
    Send-Commands $client @('openscriptmenu team_marinesopfor autoassign'); Start-Sleep 3
    Send-Commands $client @("openscriptmenu changeclass $Class", "openscriptmenu changeclass_marines $Class", "openscriptmenu changeclass_opfor $Class"); Start-Sleep 2
    # PromodFlex: pick a class on the team menu, then accept it.
    Send-Commands $client @("openscriptmenu changeclass_marines_mw assault", "openscriptmenu changeclass_opfor_mw assault"); Start-Sleep 2
    Send-Commands $client @("openscriptmenu changeclass_mw go"); Start-Sleep 5
}

function Invoke-Combat($client) {
    Send-Commands $client @('take flash_grenade_mp', 'take concussion_grenade_mp', 'give smoke_grenade_mp'); Start-Sleep 1
    Send-Commands $client @('+attack'); Start-Sleep 1; Send-Commands $client @('-attack'); Start-Sleep 1
    Send-Commands $client @('+smoke'); Start-Sleep 1; Send-Commands $client @('-smoke'); Start-Sleep 12
}

function Stop-Client($client) {
    Send-Commands $client @('quit')
    Wait-Exit $client.Pid 60
}

function Stop-Started {
    foreach ($processId in $started) {
        if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
            # Still running at the end of a run: record its windows (error dialogs) before stopping it.
            if ($script:currentFolder) {
                & (Join-Path $PSScriptRoot 'Get-GameWindows.ps1') -ProcessId $processId -Desktop $Desktop |
                    Add-Content -LiteralPath (Join-Path $script:currentFolder "windows-$processId.txt")
            }
            Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
        }
    }
    $started.Clear()
}

function Add-Result([string]$run, [bool]$pass, [string]$evidence, [datetime]$from) {
    $results.Add([pscustomobject]@{ Run = $run; Pass = $pass; Seconds = [int]((Get-Date) - $from).TotalSeconds; Evidence = $evidence })
}

function Get-StatusLines($client, [string]$pattern) {
    (Select-String -InputObject (Get-Log $client.Home) -Pattern $pattern -AllMatches).Matches | ForEach-Object Value
}

try {
    foreach ($name in $Run) {
        $folder = New-RunFolder $name
        $script:currentFolder = $folder
        $from = Get-Date
        switch ($name) {
            'R1' {  # self-built client (bridge compiled out) joins the private server and quits cleanly
                $s = Start-Server $folder 'mp_crash' ''
                $c = Start-Client $folder 'off' 'a'
                $joined = Connect-Client $c $s
                Start-Sleep 10
                $exited = Stop-Client $c
                $version = [regex]::Match((Get-Log $c.Home), 'CoD4 MP 2\d\.\d').Value
                Add-Result $name ($joined -and $exited) "joined=$joined clean-quit=$exited version=$version" $from
            }
            'R2' {  # smoke build without a renderer: nothing patched, status says so
                $s = Start-Server $folder 'mp_crash' 'v1'
                $c = Start-Client $folder 'smoke' 'a'
                $joined = Connect-Client $c $s
                Start-Sleep 8
                Send-Commands $c @('volumetricsmoke_status')
                $off = Wait-Log $c.Home 'Volumetric smoke: off' 20
                $exited = Stop-Client $c
                Add-Result $name ($joined -and $off -and $exited) "joined=$joined status-off=$off clean-quit=$exited" $from
            }
            'R3' {  # stub renderer: every hook fires in play; vid_restart and quit survive
                $s = Start-Server $folder 'mp_crash' 'v1' -Devmap
                $c = Start-Client $folder 'stub' 'a'
                $joined = Connect-Client $c $s
                Start-Sleep 8
                Join-Game $c
                Invoke-Combat $c
                Send-Commands $c @('volumetricsmoke_status')
                $first = Wait-Log $c.Home 'stub map=.*composites=[1-9]' 20
                Send-Commands $c @('vid_restart'); Start-Sleep 15
                Send-Commands $c @('volumetricsmoke_status'); Start-Sleep 3
                $exited = Stop-Client $c
                $lines = Get-StatusLines $c 'stub map=[^\r\n]*'
                Add-Result $name ($joined -and $first -and $exited) "joined=$joined clean-quit=$exited $($lines -join ' || ')" $from
            }
            'R4a' {  # private 21.2 server copy: client accepted, policy arrives through serverinfo
                $s = Start-Server $folder 'mp_crash' 'v1'
                $c = Start-Client $folder 'stub' 'a'
                $joined = Connect-Client $c $s
                Start-Sleep 8
                Send-Commands $c @('volumetricsmoke_status')
                $policy = Wait-Log $c.Home 'server policy v1' 20
                $exited = Stop-Client $c
                Add-Result $name ($policy -and $joined) "joined=$joined policy=$policy clean-quit=$exited" $from
            }
            'R5' {  # real renderer on a stock map with the grid pack; smoke from a real grenade and from a control spawn
                # Stock rules (no mod): nothing forces r_zFeather off. The stock-map grid pack is <appdata>\main\jcod4x_vsmoke_stock.iwd.
                $s = Start-Server $folder 'mp_crash' 'v1' -Devmap
                $c = Start-Client $folder $RealVariant 'a'
                $joined = Connect-Client $c $s
                $grid = Wait-Log $c.Home 'occupancy grid loaded for mp_crash' 60
                Join-Game $c
                Send-Commands $c @('setviewpos 336 60 208 65 0'); Start-Sleep 2
                Invoke-Combat $c
                Set-Content -LiteralPath (Join-Path $Runtime "clients\$RealVariant\vsmoke\control.txt") -Value 'spawn 505 422 210' -Encoding ASCII
                Start-Sleep 12
                Send-Commands $c @('volumetricsmoke_status'); Start-Sleep 4
                $exited = Stop-Client $c
                $status = Get-StatusLines $c 'map=mp_crash[^\r\n]*' | Select-Object -Last 1
                Add-Result $name ($grid -and $status -match 'clouds=1') "joined=$joined grid=$grid status=$status feather=$((Get-Log $c.Home) -match 'r_zFeather enabled') clean-quit=$exited" $from
            }
            'R5u' {  # real renderer on a Map Kit usermap: grid read from the usermap's own IWD
                $s = Start-Server $folder 'mp_mkhot' 'v1' 'mods/pmflex' -Devmap
                $c = Start-Client $folder $RealVariant 'a'
                $joined = Connect-Client $c $s
                $grid = Wait-Log $c.Home 'occupancy grid loaded for mp_mkhot' 60
                $exited = Stop-Client $c
                Add-Result $name $grid "joined=$joined grid-from-usermap-iwd=$grid clean-quit=$exited" $from
            }
            'R6' {  # two clients on one server see the same detonation
                Copy-Item -LiteralPath (Join-Path $Runtime 'clients\real') -Destination (Join-Path $Runtime 'clients\real2') -Recurse -Force
                foreach ($v in 'real', 'real2') { Remove-Item -LiteralPath (Join-Path $Runtime "clients\$v\vsmoke\client.log") -ErrorAction SilentlyContinue }
                # PromodFlex rules: their default loadout throws a smoke grenade with +smoke.
                $s = Start-Server $folder 'mp_crash' 'v1' 'mods/pmflex' -Devmap
                $a = Start-Client $folder $RealVariant 'a' 'VSmokeA'
                $b = Start-Client $folder 'real2' 'b' 'VSmokeB'
                # The server refuses a second connection from one address while another is still loading.
                $joinedA = Connect-Client $a $s
                Start-Sleep 10
                Join-Game $a
                $joinedB = Connect-Client $b $s
                Start-Sleep 10
                Join-Game $b -Spectate
                Invoke-Combat $a
                Send-Commands $a @('volumetricsmoke_status'); Send-Commands $b @('volumetricsmoke_status'); Start-Sleep 3
                $null = Stop-Client $a; $null = Stop-Client $b
                $spawns = foreach ($v in 'real', 'real2') {
                    (Get-Content -LiteralPath (Join-Path $Runtime "clients\$v\vsmoke\client.log") -ErrorAction SilentlyContinue | Select-String 'spawn queued') -join ' / '
                }
                Add-Result $name ([bool]$spawns[0] -and [bool]$spawns[1]) "joined=$joinedA,$joinedB A: $($spawns[0]) || B: $($spawns[1])" $from
            }
            'R7' {  # negatives: wrong pinned hash, missing export, no policy, map without grid
                $s = Start-Server $folder 'mp_crash' ''
                $checks = [ordered]@{}
                foreach ($case in @(@('badhash', 'does not match'), @('noexport', 'does not export'), @('real', 'not set \(stock smoke\)'))) {
                    $c = Start-Client $folder $case[0] $case[0]
                    $null = Connect-Client $c $s; Start-Sleep 6
                    Send-Commands $c @('volumetricsmoke_status')
                    $checks[$case[0]] = Wait-Log $c.Home $case[1] 20
                    $null = Stop-Client $c
                }
                Stop-Started
                $s = Start-Server $folder 'mp_shipment' 'v1'
                $c = Start-Client $folder 'real' 'nogrid'
                $null = Connect-Client $c $s
                $checks['nogrid'] = Wait-Log $c.Home 'no occupancy grid' 60
                $null = Stop-Client $c
                Add-Result $name (-not ($checks.Values -contains $false)) (($checks.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ' ') $from
            }
        }
        Stop-Started
    }
}
finally {
    Stop-Started
    $results | Format-Table -AutoSize -Wrap | Out-String -Width 250
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Runtime ('logs\results-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')) -Encoding ASCII
}
