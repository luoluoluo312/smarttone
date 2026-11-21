# ============================================================================
#  check_glyphs.ps1  ——  检查源码字符串里是否有字库中没有的字符
#
#  背景：字体是按需裁剪的子集（ASCII 与汉字都只收录用到的字）。
#        渲染器遇到字库外的字符会**静默跳过**，不报错也不提示，
#        屏幕上看就是"某个字不见了"。这个坑已经踩过多次
#        （缺汉字、缺 '#'、缺 'b'、缺小写 'l'/'c'/'w'/'i' 等）。
#        注意：ASCII 大小写必须分别统计，不能把 'l' 当作 'L'。
#
#  用法：
#      powershell -ExecutionPolicy Bypass -File tools\check_glyphs.ps1
#
#  退出码：0 = 全部字符都有字形；1 = 存在缺失（会列出文件和行号）
# ============================================================================

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $root) { $root = (Get-Location).Path }

# ---------------------------------------------------------------- 读字库
# 注意：PowerShell 的 @{} 默认不区分大小写，会把 'l' 当成 'L' 已存在。
# 这里改用字符码点做键，大小写必须分别统计，否则小写字形缺失查不出来。
$asciiSet = @{}
$asciiFile = Join-Path $root 'BSP\Src\ui_font_ascii.c'
foreach ($ln in [IO.File]::ReadAllLines($asciiFile, [Text.UTF8Encoding]::new($false))) {
    $m = [regex]::Match($ln, "\{'(.)'")
    if ($m.Success) { $asciiSet[("{0:X4}" -f [int][char]$m.Groups[1].Value)] = 1 }
}

$cnSet = @{}
$fontFile = Join-Path $root 'BSP\Src\font.c'
$fontTxt = [IO.File]::ReadAllText($fontFile, [Text.UTF8Encoding]::new($false))
foreach ($m in [regex]::Matches($fontTxt, '\{0x([0-9A-Fa-f]{4})u, font12_')) {
    $cnSet[[Convert]::ToInt32($m.Groups[1].Value, 16)] = 1
}

# ---------------------------------------------------------------- 扫描源码
$srcDirs = @('BSP\Src', 'BSP\Audio', 'Core\Src')
$files = @()
foreach ($d in $srcDirs) {
    $p = Join-Path $root $d
    if (Test-Path $p) { $files += Get-ChildItem $p -Recurse -File -Include *.c, *.h }
}

$missing = @{}
foreach ($f in $files) {
    $raw = [IO.File]::ReadAllText($f.FullName, [Text.UTF8Encoding]::new($false))
    $raw = [regex]::Replace($raw, '(?s)/\*.*?\*/', '')
    $raw = [regex]::Replace($raw, '//[^\n]*', '')
    $lines = $raw -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # 只取形如 "..." 的字符串字面量，跳过注释行与 #include（文件名不显示）
        if ($lines[$i] -match '^\s*(//|\*|/\*)') { continue }
        if ($lines[$i] -match '^\s*#\s*include') { continue }
        foreach ($m in [regex]::Matches($lines[$i], '"([^"\\]{1,80})"')) {
            $literal = $m.Groups[1].Value
            # snprintf 的格式说明符不会绘制到屏幕；%% 仍代表可见的百分号。
            if ($lines[$i] -match '\bsnprintf\s*\(') {
                $literal = [regex]::Replace($literal, '%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn])', {
                    param($format)
                    if ($format.Value -eq '%%') { '%' } else { '' }
                })
            }
            foreach ($ch in $literal.ToCharArray()) {
                $cp = [int]$ch
                if ($cp -ge 0x20 -and $cp -le 0x7E) {
                    if (-not $asciiSet.ContainsKey(("{0:X4}" -f $cp))) {
                        $key = "$([string]$ch)"
                        if (-not $missing.ContainsKey($key)) {
                            $missing[$key] = "$($f.Name):$($i + 1)"
                        }
                    }
                } elseif ($cp -gt 0x7E) {
                    if (-not $cnSet.ContainsKey($cp)) {
                        $key = "$([string]$ch)"
                        if (-not $missing.ContainsKey($key)) {
                            $missing[$key] = "$($f.Name):$($i + 1)"
                        }
                    }
                }
            }
        }
    }
}

# ---------------------------------------------------------------- 输出
Write-Host ""
Write-Host "字库:  ASCII $($asciiSet.Count) 个   汉字 $($cnSet.Count) 个"
Write-Host "扫描:  $($files.Count) 个源文件"
Write-Host ""

if ($missing.Count -eq 0) {
    Write-Host "[OK] 所有字符串用到的字符都有字形" -ForegroundColor Green
    exit 0
}

Write-Host "[FAIL] 发现 $($missing.Count) 个字符没有字形，屏幕上会被静默丢弃：" -ForegroundColor Red
foreach ($k in ($missing.Keys | Sort-Object)) {
    $cp = [int][char]$k
    Write-Host ("   '{0}'  U+{1:X4}   首次出现于 {2}" -f $k, $cp, $missing[$k])
}
Write-Host ""
Write-Host "补字形的方法：" -ForegroundColor Yellow
Write-Host "  ASCII  -> 在 BSP\Src\ui_font_ascii.c 的表中加一行 { 'x', {7 字节位图} },"
Write-Host "  汉字   -> 在 BSP\Src\font.c 中加 font12_xxxx 数组并在 font12_glyphs 表里登记"
exit 1
