[CmdletBinding()]
param([string]$BuildPath = '', [string]$RuntimePath = '', [string]$SourcePath = '', [string]$ModSourcePath = '', [switch]$Packaged)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$BuildPath) { $BuildPath = Join-Path $repo 'build-rml' }
if (!$RuntimePath) { $RuntimePath = Join-Path $BuildPath 'lab' }
$cache = Get-Content -LiteralPath (Join-Path $BuildPath 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^TEST_ISOLATION:BOOL=ON\s*$' -or $cache -notmatch '(?m)^RMLUI:BOOL=ON\s*$') {
    throw 'Use a client built with both RMLUI and TEST_ISOLATION enabled.'
}
$runtime = Get-Content -LiteralPath (Join-Path $RuntimePath 'runtime.json') -Raw | ConvertFrom-Json
if (Get-Process iw3mp -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $runtime.game 'iw3mp.exe') }) {
    throw 'This private test runtime is already running.'
}
if (!$SourcePath) { $SourcePath = Join-Path $repo 'ui\rml' }
if (!$Packaged -and !(Test-Path -LiteralPath (Join-Path $SourcePath 'palette.rml'))) {
    throw "Missing development palette source: $SourcePath"
}
$runner = Join-Path $BuildPath 'bin\rml_game_runner.exe'
$client = Join-Path $BuildPath 'bin\cod4x_021.dll'
foreach ($file in @($runner, $client, (Join-Path $runtime.game 'miles32.dll'))) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing runtime file: $file" }
}
$oldAppdata = $env:COD4X_APPDATA_DIR
$oldCommands = $env:COD4X_TEST_COMMANDS
$oldSource = $env:COD4X_RML_SOURCE
$oldModSource = $env:COD4X_RML_MOD_SOURCE
if (!$ModSourcePath) { $ModSourcePath = Join-Path $repo '..\promod-econ-na\ui\rml' }
try {
    $env:COD4X_RML_SOURCE = if ($Packaged) { $null } else { (Resolve-Path -LiteralPath $SourcePath).Path }
    $env:COD4X_RML_MOD_SOURCE = if (!$Packaged -and (Test-Path -LiteralPath $ModSourcePath)) { (Resolve-Path -LiteralPath $ModSourcePath).Path } else { $null }
    $env:COD4X_APPDATA_DIR = $runtime.appdata
    $env:COD4X_TEST_COMMANDS = Join-Path $RuntimePath 'commands.txt'
    $arguments = @(('"' + $runtime.game + '"'), ('"' + $client + '"'),
        '+set', 'fs_basepath', ('"' + $runtime.game + '"'), '+set', 'fs_homepath', ('"' + $runtime.game + '"'),
        '+set', 'fs_game', $runtime.fsGame, '+set', 'com_introplayed', '1', '+set', 'ui_playerProfileAlreadyChosen', '1',
        '+set', 'r_fullscreen', '0', '+set', 'r_mode', '1280x800',
        '+set', 'logfile', '2', '+set', 'cl_punkbuster', '0', '+set', 'sv_punkbuster', '0',
        '+set', 'net_noudp', '1', '+set', 'net_noipx', '1', '+exec', 'rml-lab.cfg')
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $RuntimePath 'game-pid.txt')
    if (!$process.WaitForExit(15000)) { throw 'The private runner did not finish loading the client within 15 seconds.' }
    if ($process.ExitCode) { throw "The private runner failed with code $($process.ExitCode)." }
    $gameProcessId = (Get-Content -LiteralPath (Join-Path $RuntimePath 'game-pid.txt') -Raw).Trim()
    Write-Output "Started private game PID $gameProcessId. Activate the game; F8 opens the palette, F9 reloads it."
    if (!$Packaged) { Write-Output "RML/RCSS saves in $SourcePath reload automatically." }
} finally {
    $env:COD4X_APPDATA_DIR = $oldAppdata
    $env:COD4X_TEST_COMMANDS = $oldCommands
    $env:COD4X_RML_SOURCE = $oldSource
    $env:COD4X_RML_MOD_SOURCE = $oldModSource
}
