# Build the CubeMX-style Makefile project with GNU make.
# Usage: powershell -ExecutionPolicy Bypass -File tools/build_make.ps1 [-Clean]
param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root

function Find-Tool {
    param([string]$Name, [string[]]$Fallbacks)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in $Fallbacks) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}

$make = Find-Tool -Name "mingw32-make" -Fallbacks @(
    "C:\Program Files (x86)\Dev-Cpp\MinGW64\bin\mingw32-make.exe",
    "C:\Program Files\Dev-Cpp\MinGW64\bin\mingw32-make.exe"
)
if (-not $make) {
    $make = Find-Tool -Name "make" -Fallbacks @()
}
if (-not $make) {
    throw "GNU make was not found. Install MinGW make or add mingw32-make.exe to PATH."
}

$gccCandidates = @()
if ($env:ARM_TOOLCHAIN_PATH) {
    $gccCandidates += (Join-Path $env:ARM_TOOLCHAIN_PATH "arm-none-eabi-gcc.exe")
}
$bundleRoot = Join-Path $env:LOCALAPPDATA "stm32cube\bundles\gnu-tools-for-stm32"
if (Test-Path $bundleRoot) {
    $gccCandidates += Get-ChildItem $bundleRoot -Recurse -Filter "arm-none-eabi-gcc.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -ExpandProperty FullName
}
$gcc = Find-Tool -Name "arm-none-eabi-gcc" -Fallbacks $gccCandidates
if (-not $gcc) {
    throw "arm-none-eabi-gcc was not found. Set ARM_TOOLCHAIN_PATH to its bin directory."
}
$toolBin = (Split-Path $gcc -Parent).Replace('\', '/')

if ($Clean) {
    & $make clean
    if ($LASTEXITCODE -ne 0) { throw "make clean failed" }
}

& $make -j4 "ARM_TOOLCHAIN_PATH=$toolBin"
if ($LASTEXITCODE -ne 0) { throw "Makefile build failed" }

Write-Host ""
Write-Host "Makefile build complete:"
Write-Host "  $Root\build\STM32F103_TFT_ST7735.elf"
Write-Host "  $Root\build\STM32F103_TFT_ST7735.hex"
Write-Host "  $Root\build\STM32F103_TFT_ST7735.bin"
