[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GamePath,
    [string]$ModSource = '',
    [string]$RuntimePath = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$gameSource = (Resolve-Path -LiteralPath $GamePath).Path
if (!$RuntimePath) { $RuntimePath = Join-Path $repo 'build-rml\lab' }
$runtime = [IO.Path]::GetFullPath($RuntimePath)
if (!$runtime.StartsWith($repo + '\build', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Stage this development runtime under the client repository build directory.'
}
foreach ($folder in @('main','zone','miles')) {
    if (!(Test-Path -LiteralPath (Join-Path $gameSource $folder))) { throw "Missing game assets: $folder" }
}
$miles = @('miles32.dll', 'mss32.dll') | ForEach-Object { Join-Path $gameSource $_ } |
    Where-Object { (Test-Path -LiteralPath $_) -and (Get-Item -LiteralPath $_).Length -eq 434688 } | Select-Object -First 1
if (!$miles) { throw 'The original Miles library was not found (miles32.dll, or stock mss32.dll).' }
foreach ($part in @($runtime, (Split-Path -Parent $runtime))) {
    if ((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Runtime output cannot be a junction or symlink: $part"
    }
}
$game = Join-Path $runtime 'game'
$appdata = Join-Path $runtime 'appdata'
$mod = Join-Path $game 'mods\rmllab'
New-Item -ItemType Directory -Force -Path $game, $mod, "$appdata\main", "$appdata\zone", "$appdata\players\profiles\RmlLab" | Out-Null
foreach ($file in @('iw3mp.exe', 'binkw32.dll', 'd3dx9_34.dll', 'localization.txt')) {
    Copy-Item -LiteralPath (Join-Path $gameSource $file) -Destination $game -Force
}
# The legacy entry point supplies the EXE HINSTANCE to Sys_LoadModules, not the client DLL handle.
Copy-Item -LiteralPath $miles -Destination (Join-Path $game 'mss32.dll') -Force
Copy-Item -LiteralPath $miles -Destination (Join-Path $game 'miles32.dll') -Force
foreach ($folder in @('main', 'zone', 'miles')) {
    $destination = Join-Path $game $folder
    if (!(Test-Path -LiteralPath $destination)) {
        New-Item -ItemType Junction -Path $destination -Target (Join-Path $gameSource $folder) | Out-Null
    }
}
Copy-Item -LiteralPath (Join-Path $repo 'assets\cod4x\main\jcod4x_00.iwd') -Destination "$appdata\main" -Force
Copy-Item -Path (Join-Path $repo 'assets\cod4x\zone\*.ff') -Destination "$appdata\zone" -Force
if ($ModSource) {
    foreach ($extension in @('*.ff', '*.iwd', '*.cfg')) {
        Get-ChildItem -LiteralPath $ModSource -Filter $extension -File | Copy-Item -Destination $mod -Force
    }
}
if (!(Test-Path -LiteralPath "$appdata\players\profiles\active.txt")) {
    'RmlLab' | Set-Content -LiteralPath "$appdata\players\profiles\active.txt" -NoNewline -Encoding ASCII
    '' | Set-Content -LiteralPath "$appdata\players\profiles\RmlLab\config_mp.cfg" -Encoding ASCII
}
@'
bind F8 "rml_show"
bind F9 "rml_reload"
set r_fullscreen 0
set r_mode -1
set r_customwidth 1280
set r_customheight 800
set com_maxfps 60
'@ | Set-Content -LiteralPath (Join-Path $mod 'rml-lab.cfg') -Encoding ASCII
& (Join-Path $PSScriptRoot 'Stage-Assets.ps1') -ModPath $mod
[ordered]@{ game = $game; appdata = $appdata; fsGame = 'mods/rmllab';
    milesSha256 = (Get-FileHash -LiteralPath (Join-Path $game 'miles32.dll')).Hash.ToLowerInvariant() } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runtime 'runtime.json') -Encoding UTF8
Write-Output "Staged private game in $game. Installed game files were read only."
