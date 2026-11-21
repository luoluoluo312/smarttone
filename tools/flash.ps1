# Flash the generated HEX through ST-LINK using STM32CubeProgrammer CLI.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/flash.ps1
#   powershell -ExecutionPolicy Bypass -File tools/flash.ps1 -File build\my.hex
param(
    [string]$File = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $File) {
    $File = Join-Path $Root "build\STM32F103_TFT_ST7735.hex"
}
if (-not (Test-Path $File)) {
    throw "Firmware file not found: $File. Run tools/build.ps1 first."
}
$File = (Resolve-Path $File).Path

$candidates = @()
if ($env:STM32_PROGRAMMER_CLI) {
    $candidates += $env:STM32_PROGRAMMER_CLI
}
$cmd = Get-Command STM32_Programmer_CLI -ErrorAction SilentlyContinue
if ($cmd) {
    $candidates += $cmd.Source
}
$bundle = Join-Path $env:LOCALAPPDATA "stm32cube\bundles\programmer"
if (Test-Path $bundle) {
    $candidates += Get-ChildItem $bundle -Recurse -Filter "STM32_Programmer_CLI.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -ExpandProperty FullName
}
$candidates += @(
    "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)

$cli = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $cli) {
    throw @"
STM32_Programmer_CLI.exe was not found.

Install STM32CubeProgrammer:
  https://www.st.com/en/development-tools/stm32cubeprog.html

Then either add its bin directory to PATH or set STM32_PROGRAMMER_CLI to the
full path of STM32_Programmer_CLI.exe.
"@
}

Write-Host "Programmer: $cli"
Write-Host "Firmware  : $File"
Write-Host ""

$staged = Join-Path $env:TEMP (([guid]::NewGuid().ToString('N')) + [IO.Path]::GetExtension($File))
try {
    Copy-Item -LiteralPath $File -Destination $staged
    & $cli -c port=SWD -w $staged -v -rst
    if ($LASTEXITCODE -ne 0) {
        throw "STM32CubeProgrammer failed with exit code $LASTEXITCODE"
    }
} finally {
    Remove-Item -LiteralPath $staged -ErrorAction SilentlyContinue
}
Write-Host ""
Write-Host "Flash complete."
