param([string]$Compiler = "gcc")
$ErrorActionPreference = "Stop"
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $includes = @('-Itests/mock', '-IBSP/Inc')
    $includes += Get-ChildItem BSP/Audio -Directory | ForEach-Object { '-I' + $_.FullName }
    $sources = @('tests/host_st7735_test.c', 'BSP/Src/bsp_tft.c', 'BSP/Src/bsp_encoder.c',
        'BSP/Src/bsp_audio_tim.c', 'BSP/Src/record.c', 'BSP/Src/boot_logo.c',
        'BSP/Src/font.c', 'BSP/Src/ui_page.c', 'BSP/Src/ui_font_ascii.c',
        'BSP/Src/rhythm_game.c', 'BSP/Src/melody_maker.c', 'BSP/Src/ds_room.c', 'BSP/Src/rhythm_chart.c')
    $sources += Get-ChildItem BSP/Audio -Filter *.c -Recurse | ForEach-Object { $_.FullName }
    $prefix = @()
    if ([IO.Path]::GetFileNameWithoutExtension($Compiler) -eq 'zig') { $prefix = @('cc') }
    & $Compiler @prefix -std=c11 -Wall -Wextra -DRECORD_HOST_TEST @includes @sources -o build/host_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & '.\build\host_test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Host regression tests failed' }
} finally { Pop-Location }
