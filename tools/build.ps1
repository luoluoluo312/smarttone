# Build the firmware without requiring CMake or Ninja.
#
# Usage (from VS Code's terminal or PowerShell):
#   powershell -ExecutionPolicy Bypass -File tools/build.ps1
#   powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Clean
#   powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Debug
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $Root
# GNU assembler cannot write object paths containing non-ASCII characters.
if ($Root -match '[^\x00-\x7F]') {
    $drive = 90..68 | ForEach-Object { ([char]$_).ToString() + ':' } |
        Where-Object { -not (Test-Path ($_ + '\')) -and -not (Get-PSDrive $_.TrimEnd(':') -ErrorAction SilentlyContinue) } |
        Select-Object -First 1
    if (-not $drive) { throw "No free drive letter for the compiler path alias" }
    & subst $drive $Root
    if ($LASTEXITCODE -ne 0) { throw "Failed to map the compiler path" }
    try {
        & ($drive + '\tools\build.ps1') -Configuration $Configuration -Clean:$Clean
    } finally {
        Set-Location $Root
        & subst $drive /D
    }
    return
}

$BuildDir = Join-Path $Root "build"
$ObjDir = Join-Path $BuildDir "obj"

if ($Clean -and (Test-Path $BuildDir)) {
    $resolved = (Resolve-Path $BuildDir).Path
    $expected = [System.IO.Path]::GetFullPath($BuildDir)
    if ($resolved -ne $expected) {
        throw "Refusing to delete unexpected path: $resolved"
    }
    Write-Host "Cleaning $resolved"
    Remove-Item -Recurse -Force $resolved
}

New-Item -ItemType Directory -Force -Path $BuildDir, $ObjDir | Out-Null

function Find-Executable {
    param(
        [string]$Name,
        [string[]]$Fallbacks
    )
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($path in $Fallbacks) {
        if ($path -and (Test-Path $path)) { return $path }
    }
    return $null
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
$Gcc = Find-Executable -Name "arm-none-eabi-gcc" -Fallbacks $gccCandidates
if (-not $Gcc) {
    throw @"
arm-none-eabi-gcc was not found.

Install one of:
  * STM32CubeCLT / GNU Tools for STM32 (recommended)
  * Arm GNU Toolchain for arm-none-eabi

Then either add its bin directory to PATH or set ARM_TOOLCHAIN_PATH to that
directory before running this script.
"@
}

$ToolBin = Split-Path $Gcc -Parent
$Objcopy = Join-Path $ToolBin "arm-none-eabi-objcopy.exe"
$Size = Join-Path $ToolBin "arm-none-eabi-size.exe"
if (-not (Test-Path $Objcopy) -or -not (Test-Path $Size)) {
    throw "objcopy/size were not found next to $Gcc"
}

$commonFlags = @(
    "-mcpu=cortex-m3",
    "-mthumb",
    "-mfloat-abi=soft",
    "-ffunction-sections",
    "-fdata-sections",
    "-fno-common",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-std=gnu11"
)

if ($Configuration -eq "Debug") {
    $commonFlags += @("-Og", "-g3", "-gdwarf-2")
} else {
    $commonFlags += @("-Os", "-g3")
}

$includes = @(
    "-ICore/Inc",
    "-IBSP/Inc",
    "-IBSP/Audio/01_audio_config",
    "-IBSP/Audio/02_audio_pitch",
    "-IBSP/Audio/03_audio_pwm",
    "-IBSP/Audio/04_audio_core",
    "-IBSP/Audio/05_audio_metronome",
    "-IBSP/Audio/06_audio_song",
    "-IBSP/Audio/07_audio_drum",
    "-IDrivers/STM32F1xx_HAL_Driver/Inc",
    "-IDrivers/CMSIS/Device/ST/STM32F1xx/Include",
    "-IDrivers/CMSIS/Include"
)
$defines = @("-DUSE_HAL_DRIVER", "-DSTM32F103xB")

$appSources = @(
    "Core/Src/main.c",
    "Core/Src/stm32f1xx_hal_msp.c",
    "Core/Src/stm32f1xx_it.c",
    "Core/Src/system_stm32f1xx.c",
    "Core/Src/syscalls.c",
    "Core/Src/sysmem.c",
    "BSP/Src/bsp_tft.c",
    "BSP/Src/bsp_encoder.c",
    "BSP/Src/bsp_func.c",
    "BSP/Src/bsp_notes.c",
    "BSP/Src/bsp_audio_tim.c",
    "BSP/Audio/02_audio_pitch/audio_pitch.c",
    "BSP/Audio/03_audio_pwm/audio_pwm.c",
    "BSP/Audio/04_audio_core/audio.c",
    "BSP/Audio/05_audio_metronome/metronome.c",
    "BSP/Audio/06_audio_song/song.c",
    "BSP/Audio/06_audio_song/sample.c",
    "BSP/Audio/06_audio_song/song_builtin.c",
    "BSP/Audio/07_audio_drum/drum.c",
    "BSP/Src/boot_logo.c",
    "BSP/Src/record.c",
    "BSP/Src/font.c",
    "BSP/Src/ui_page.c",
    "BSP/Src/ui_font_ascii.c",
    "BSP/Src/rhythm_game.c",
    "BSP/Src/melody_maker.c",
    "BSP/Src/ds_room.c",
    "BSP/Src/rhythm_chart.c"
)

$halSources = @(
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c",
    "Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c"
)

Write-Host "Toolchain : $Gcc"
Write-Host "Config    : $Configuration"
Write-Host "Output    : $BuildDir"
Write-Host ""

$objects = @()
foreach ($src in ($appSources + $halSources)) {
    $full = Join-Path $Root $src
    if (-not (Test-Path $full)) { throw "Source not found: $full" }
    $objName = ($src -replace "[\\/]", "_") + ".o"
    $obj = Join-Path $ObjDir $objName
    Write-Host "CC  $src"
    & $Gcc @commonFlags @defines @includes "-c" $full "-o" $obj
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $src" }
    $objects += $obj
}

$startup = Join-Path $Root "Core/Startup/startup_stm32f103c8tx.s"
if (-not (Test-Path $startup)) { throw "Startup file not found: $startup" }
$startupObj = Join-Path $ObjDir "startup_stm32f103c8tx.o"
Write-Host "AS  Core/Startup/startup_stm32f103c8tx.s"
& $Gcc @commonFlags @defines @includes "-x" "assembler-with-cpp" "-c" $startup "-o" $startupObj
if ($LASTEXITCODE -ne 0) { throw "Assembly failed: $startup" }

$elf = Join-Path $BuildDir "STM32F103_TFT_ST7735.elf"
$hex = Join-Path $BuildDir "STM32F103_TFT_ST7735.hex"
$bin = Join-Path $BuildDir "STM32F103_TFT_ST7735.bin"
$map = Join-Path $BuildDir "STM32F103_TFT_ST7735.map"
$ld = Join-Path $Root "STM32F103C8TX_FLASH.ld"

Write-Host "LD  $elf"
& $Gcc @commonFlags "-T" $ld "-Wl,--gc-sections" "-Wl,--print-memory-usage" "-Wl,-Map=$map" `
    "--specs=nano.specs" "--specs=nosys.specs" "-o" $elf $startupObj @objects
if ($LASTEXITCODE -ne 0) { throw "Link failed" }

& $Objcopy -O ihex $elf $hex
if ($LASTEXITCODE -ne 0) { throw "objcopy ihex failed" }
& $Objcopy -O binary $elf $bin
if ($LASTEXITCODE -ne 0) { throw "objcopy binary failed" }

Write-Host ""
Write-Host "Firmware size:"
& $Size $elf
if ($LASTEXITCODE -ne 0) { throw "size failed" }

Write-Host ""
Write-Host "Built:"
Write-Host "  $elf"
Write-Host "  $hex"
Write-Host "  $bin"
