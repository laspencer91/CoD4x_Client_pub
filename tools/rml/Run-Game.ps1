[CmdletBinding()]
param([string]$BuildPath = '', [string]$RuntimePath = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$BuildPath) { $BuildPath = Join-Path $repo 'build-rml' }
if (!$RuntimePath) { $RuntimePath = Join-Path $BuildPath 'lab' }
$cache = Get-Content -LiteralPath (Join-Path $BuildPath 'CMakeCache.txt') -Raw
if ($cache -notmatch '(?m)^TEST_ISOLATION:BOOL=ON\s*$' -or $cache -notmatch '(?m)^RMLUI:BOOL=ON\s*$') {
    throw 'Use a client built with both RMLUI and TEST_ISOLATION enabled.'
}
$runtime = Get-Content -LiteralPath (Join-Path $RuntimePath 'runtime.json') -Raw | ConvertFrom-Json
$runner = Join-Path $BuildPath 'bin\rml_game_runner.exe'
$client = Join-Path $BuildPath 'bin\cod4x_021.dll'
foreach ($file in @($runner, $client, (Join-Path $runtime.game 'miles32.dll'))) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing runtime file: $file" }
}
$oldAppdata = $env:COD4X_APPDATA_DIR
$oldCommands = $env:COD4X_TEST_COMMANDS
try {
    $env:COD4X_APPDATA_DIR = $runtime.appdata
    $env:COD4X_TEST_COMMANDS = Join-Path $RuntimePath 'commands.txt'
    $arguments = @(('"' + $runtime.game + '"'), ('"' + $client + '"'),
        '+set', 'fs_basepath', ('"' + $runtime.game + '"'), '+set', 'fs_homepath', ('"' + $runtime.game + '"'),
        '+set', 'fs_game', $runtime.fsGame, '+set', 'com_introplayed', '1', '+set', 'ui_playerProfileAlreadyChosen', '1',
        '+set', 'logfile', '2', '+set', 'cl_punkbuster', '0', '+set', 'sv_punkbuster', '0',
        '+set', 'net_noudp', '1', '+set', 'net_noipx', '1', '+exec', 'rml-lab.cfg')
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $RuntimePath 'game-pid.txt')
    Write-Output "Started private runner PID $($process.Id). Activate the game; F8 opens the diagnostic, F9 reloads it."
} finally {
    $env:COD4X_APPDATA_DIR = $oldAppdata
    $env:COD4X_TEST_COMMANDS = $oldCommands
}
