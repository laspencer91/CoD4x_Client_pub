[CmdletBinding()]
param([Parameter(Mandatory)][string]$ModPath, [string]$ModSourcePath = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$source = Join-Path $repo 'ui\rml'
$destination = [IO.Path]::GetFullPath((Join-Path $ModPath 'ui\rml'))
if ($destination -eq $source) { throw 'Choose a separate private runtime mod folder.' }
$ancestor = $destination
while ($ancestor) {
    if (Test-Path -LiteralPath $ancestor) {
        if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to stage through a junction or symlink: $ancestor"
        }
    }
    $parent = Split-Path -Parent $ancestor
    if ($parent -eq $ancestor) { break }
    $ancestor = $parent
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
if (!$ModSourcePath) { $ModSourcePath = Join-Path $repo '..\promod-econ-na\ui\rml' }
if (Test-Path -LiteralPath $ModSourcePath) { Copy-Item -Path (Join-Path $ModSourcePath '*') -Destination $destination -Recurse -Force }
$files = @(Get-ChildItem -LiteralPath $destination -Recurse -File | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($destination.Length + 1).Replace('\', '/');
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
[ordered]@{ version = 1; namespace = 'ui/rml'; files = $files } | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $ModPath 'rml-assets.json') -Encoding UTF8
Write-Output "Staged $($files.Count) UI assets in $destination"
