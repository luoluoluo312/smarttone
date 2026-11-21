# -*- coding: utf-8 -*-
"""重做 5 个音色：重新生成谐波完全不同的波形表，并让电贝斯拥有独立波表。"""
import math, re, shutil, os

ROOT = r"D:\wxbsoundsoftware\ST_current"
AP = os.path.join(ROOT, r"BSP\Audio\03_audio_pwm\audio_pwm.c")
TY = os.path.join(ROOT, r"BSP\Audio\01_audio_config\audio_types.h")
AC = os.path.join(ROOT, r"BSP\Audio\04_audio_core\audio.c")

for p in (AP, TY, AC):
    shutil.copyfile(p, p + ".bak")
print("已备份 3 个文件为 .bak")

N = 128

def norm(v):
    m = max(abs(x) for x in v) or 1.0
    return [int(round(x / m * 120)) for x in v]

def harmonic(partials):
    out = []
    for n in range(N):
        s = 0.0
        for k, a, ph in partials:
            s += a * math.sin(2 * math.pi * k * n / N + ph)
        out.append(s)
    return norm(out)

# 1/2 钢琴：基频强 + 2/3/4 次递减；暗表只留 1/2 次
piano   = harmonic([(1,1.0,0),(2,0.55,0),(3,0.32,0.4),(4,0.18,0.8),(5,0.09,1.2),(6,0.05,1.6)])
piano_m = harmonic([(1,1.0,0),(2,0.22,0),(3,0.07,0.4)])

# 吉他：全谐波 + 软削波
g = []
for n in range(N):
    s = sum((1.0 / k) * math.sin(2 * math.pi * k * n / N) for k in range(1, 11))
    g.append(math.tanh(s * 1.7))
guitar   = norm(g)
guitar_b = harmonic([(1,1.0,0),(2,0.45,0),(3,0.30,0.3),(4,0.20,0.6),(5,0.13,0.9)])

# 3 手风琴：奇次+偶次同时强（两根簧片），非常"沙"
acc = harmonic([(1,1.0,0),(2,0.72,0),(3,0.66,0),(4,0.46,0.5),
                (5,0.44,0.5),(6,0.26,1.0),(7,0.24,1.0),(8,0.14,1.5)])

# 4 电贝斯：几乎纯正弦，只加一点 2 次 —— 和手风琴形成最大反差
bass = harmonic([(1,1.0,0),(2,0.12,0),(3,0.04,0)])

# 5 八音盒：基频 + 强 3/4/6 次，金属叮当感
bell = harmonic([(1,1.0,0),(2,0.28,0),(3,0.80,0.6),(4,0.55,0.9),
                 (6,0.34,1.4),(9,0.18,2.0)])

def emit(name, vals):
    body = []
    for i in range(0, N, 12):
        body.append("    " + ", ".join(str(v) for v in vals[i:i+12]) + ",")
    return "static const int8_t %s[128] = {\n%s\n};\n" % (name, "\n".join(body))

tables = {
    "s_wave_piano_bright": piano,
    "s_wave_piano_mellow": piano_m,
    "s_wave_bell":         bell,
    "s_wave_organ":        acc,
    "s_wave_guitar_bright": guitar,
    "s_wave_guitar_body":  guitar_b,
}

src = open(AP, encoding="utf-8").read()
count = 0
for name, vals in tables.items():
    pat = re.compile(r"static const int8_t " + name + r"\[128\] = \{.*?\n\};", re.S)
    new = emit(name, vals).rstrip("\n")
    src, n = pat.subn(lambda m: new, src, count=1)
    count += n
print("替换波形表数量:", count)

# 新增电贝斯专用表（插在第一张表之前）
if "s_wave_bass" not in src:
    anchor = "static const int8_t s_wave_piano_bright"
    src = src.replace(anchor, emit("s_wave_bass", bass) + "\n" + anchor, 1)
    print("已新增 s_wave_bass")

# ISR 分支：在 GUITAR 分支之后加 BASS
if "AUDIO_WAVE_BASS" not in src:
    pat = re.compile(r"(\} else if \(s_pwm\[i\]\.wave == AUDIO_WAVE_GUITAR\) \{.*?\n            \})", re.S)
    m = pat.search(src)
    if m:
        src = src[:m.end()] + ("\n            else if (s_pwm[i].wave == AUDIO_WAVE_BASS) {"
                               "\n                v = (int32_t)s_wave_bass[idx];"
                               "\n            }") + src[m.end():]
        print("ISR 已加 BASS 分支")
    else:
        print("!! 未找到 GUITAR ISR 分支")

# 上限钳位
src = src.replace("if (wave > (uint8_t)AUDIO_WAVE_GUITAR)", "if (wave > (uint8_t)AUDIO_WAVE_BASS)")
open(AP, "w", encoding="utf-8").write(src)

# 枚举里加 AUDIO_WAVE_BASS
ty = open(TY, encoding="utf-8").read()
if "AUDIO_WAVE_BASS" not in ty:
    m = re.search(r"(AUDIO_WAVE_BELL[^,\n]*)(,?)", ty)
    if m:
        ty = ty[:m.end()] + ", AUDIO_WAVE_BASS" + ty[m.end():]
        print("已加 AUDIO_WAVE_BASS 枚举")
    else:
        print("!! 未找到 AUDIO_WAVE_BELL")
open(TY, "w", encoding="utf-8").write(ty)

# audio.c：第 4 个音色改用 BASS 表
ac = open(AC, encoding="utf-8").read()
m = re.search(r"(/\* 4 电贝斯.*?)(\(uint8_t\)AUDIO_WAVE_ORGAN)", ac, re.S)
if m:
    ac = ac[:m.start(2)] + "(uint8_t)AUDIO_WAVE_BASS" + ac[m.end(2):]
    print("电贝斯 -> AUDIO_WAVE_BASS")
else:
    print("!! 未匹配电贝斯条目")
open(AC, "w", encoding="utf-8").write(ac)
print("完成")
