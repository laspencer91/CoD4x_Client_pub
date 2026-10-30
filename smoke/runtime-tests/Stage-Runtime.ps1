[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameSource,     # an existing private CoD4 1.7 game folder (never a Steam install)
    [Parameter(Mandatory)][string]$Runtime,        # new private test folder
    [Parameter(Mandatory)][string]$ClientAssets,   # CoD4x release assets: jcod4x_00.iwd, cod4x_*.ff
    [Parameter(Mandatory)][string]$ServerSource,   # bin\cod4x18_dedrun.exe, bin\main\xbase_00.iwd, home\main\{xbase_00,jcod4x_00}.iwd
    [string[]]$Usermaps = @()
)
# Builds a private, self-contained runtime for the volumetric smoke runtime tests:
#   <Runtime>\game      copy of the game files (stock Miles mss32.dll, no CoD4x loader)
#   <Runtime>\appdata   COD4X_APPDATA_DIR for TEST_ISOLATION clients (CoD4x zones and IWD)
#   <Runtime>\server    dedicated server binary and its home folder
#   <Runtime>\clients   one folder per client variant (filled by Run-Phase2.ps1)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $GameSource).Path
if ($source -match '\\steamapps\\') { throw 'Refusing a Steam library folder as the game source.' }
$stockExe = 'f50f8a520581754e73cb7cae67b02adcbad5f2e71aaa412a013bb3ef57a391a5'
if ((Get-FileHash -LiteralPath (Join-Path $source 'iw3mp.exe')).Hash.ToLower() -ne $stockExe) { throw 'iw3mp.exe is not the large-address-aware 1.7 build.' }
if ((Get-Item -LiteralPath (Join-Path $source 'mss32.dll')).Length -ne 434688) { throw 'mss32.dll is not the stock Miles library.' }
$game = Join-Path $Runtime 'game'
New-Item -ItemType Directory -Force -Path $game, (Join-Path $Runtime 'appdata\main'), (Join-Path $Runtime 'appdata\zone'),
    (Join-Path $Runtime 'server\bin'), (Join-Path $Runtime 'server\home\main'), (Join-Path $Runtime 'clients'), (Join-Path $Runtime 'logs') | Out-Null
foreach ($name in 'iw3mp.exe', 'mss32.dll', 'miles32.dll', 'binkw32.dll', 'd3dx9_34.dll', 'localization.txt') {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $game -Force
}
foreach ($folder in 'main', 'zone', 'miles') {
    & robocopy.exe (Join-Path $source $folder) (Join-Path $game $folder) /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copying $folder failed." }
}
foreach ($map in $Usermaps) {
    & robocopy.exe (Join-Path $source "usermaps\$map") (Join-Path $game "usermaps\$map") /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copying usermap $map failed." }
}
Copy-Item -LiteralPath (Join-Path $ClientAssets 'jcod4x_00.iwd') -Destination (Join-Path $Runtime 'appdata\main') -Force
foreach ($zone in 'cod4x_patch.ff', 'cod4x_patchv2.ff', 'cod4x_ambfix.ff') {
    Copy-Item -LiteralPath (Join-Path $ClientAssets $zone) -Destination (Join-Path $Runtime 'appdata\zone') -Force
}
New-Item -ItemType Directory -Force -Path (Join-Path $Runtime 'server\bin\main') | Out-Null
foreach ($file in 'bin\cod4x18_dedrun.exe', 'bin\main\xbase_00.iwd', 'home\main\xbase_00.iwd', 'home\main\jcod4x_00.iwd') {
    Copy-Item -LiteralPath (Join-Path $ServerSource $file) -Destination (Join-Path (Join-Path $Runtime 'server') $file) -Force
}
$hashes = Get-ChildItem -LiteralPath $Runtime -Recurse -File | Where-Object { $_.Length -lt 64MB -and $_.FullName -notlike '*\logs\*' } |
    ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash.ToLower(), $_.FullName.Substring($Runtime.Length + 1) }
$hashes | Set-Content -LiteralPath (Join-Path $Runtime 'logs\staged-files.sha256') -Encoding ASCII
Write-Output "Staged private runtime in $Runtime"
