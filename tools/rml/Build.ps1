[CmdletBinding()]
param(
    [string]$Toolchain = '',
    [string]$BuildPath = '',
    [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$Toolchain) { $Toolchain = Join-Path $repo '..\Tools\cod4-build' }
if (!$BuildPath) { $BuildPath = Join-Path $repo 'build-rml' }
$compiler = Join-Path $Toolchain 'mingw32\bin'
$nasm = Join-Path $Toolchain 'nasm\nasm-2.16.03\nasm.exe'
foreach ($file in @((Join-Path $compiler 'gcc.exe'), (Join-Path $compiler 'g++.exe'), $nasm)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing build tool: $file" }
}
$previousPath = $env:PATH
try {
    $env:PATH = "$compiler;$(Split-Path $nasm);$previousPath"
    & cmake -S $repo -B $BuildPath -G 'MinGW Makefiles' "-DCMAKE_C_COMPILER=$compiler/gcc.exe" `
        "-DCMAKE_CXX_COMPILER=$compiler/g++.exe" "-DCMAKE_ASM_NASM_COMPILER=$nasm" `
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRMLUI=ON -DRMLUI_TESTS=ON -DTEST_ISOLATION=ON `
        -DCOD4X_APPDATA_SUBDIR=PMFLEX_RML_TEST -DVOLUMETRIC_SMOKE=ON
    if ($LASTEXITCODE) { throw 'RmlUi configure failed.' }
    & cmake --build $BuildPath --target cod4x test_rml rml_game_runner --parallel $Jobs
    if ($LASTEXITCODE) { throw 'RmlUi build failed.' }
    & ctest --test-dir $BuildPath --output-on-failure -R rml
    if ($LASTEXITCODE) { throw 'RmlUi checks failed.' }
} finally { $env:PATH = $previousPath }
Write-Output "Built optional client and native checks in $BuildPath. No installed game files changed."
