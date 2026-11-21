/* ============================================================================
 *  song_builtin.c   —— 内置曲谱库
 *
 *  模块：06_audio_song
 *  说明：曲谱全部是静态常量表，占用的是程序 Flash，不占 RAM。
 *        只使用七个自然音，正好对应七个琴键，不需要半音。
 *
 *        时值单位是毫秒：400ms 约等于一拍，800ms 是两拍。
 *        曲名用 ASCII，避免不同编译器下的中文编码问题。
 * ========================================================================== */

#include <stddef.h>
#include "song.h"

/* 曲谱书写助手：Q = 一拍，H = 两拍，音区统一放在中音区 */
#define SN_Q(n)   { (uint8_t)(n), (uint8_t)OCT_MID, 400U, 0U }
#define SN_H(n)   { (uint8_t)(n), (uint8_t)OCT_MID, 800U, 0U }
/* 指定音区：o 取 OCT_LOW / OCT_MID / OCT_HIGH，写跨八度的旋律用这两个 */
#define SNO_Q(n, o)  { (uint8_t)(n), (uint8_t)(o), 400U, 0U }
#define SNO_H(n, o)  { (uint8_t)(n), (uint8_t)(o), 800U, 0U }
/* 直接指定频率：音高精确到任意 Hz，不受七个自然音限制 */
#define SN_F(f, ms)  { (uint8_t)NOTE_DO, (uint8_t)OCT_MID, (uint16_t)(ms), (uint16_t)(f) }
/* 休止符 */
#define SN_R(ms)     { SONG_NOTE_REST, 0U, (uint16_t)(ms), 0U }

/* -------------------------------------------------------------- 小星星
 *  Do Do Sol Sol La La Sol | Fa Fa Mi Mi Re Re Do
 *  Sol Sol Fa Fa Mi Mi Re   | Sol Sol Fa Fa Mi Mi Re
 *  Do Do Sol Sol La La Sol | Fa Fa Mi Mi Re Re Do
 */
static const SongNote s_twinkle[] = {
    SN_Q(NOTE_DO),  SN_Q(NOTE_DO),  SN_Q(NOTE_SOL), SN_Q(NOTE_SOL), SN_Q(NOTE_LA), SN_Q(NOTE_LA), SN_H(NOTE_SOL),
    SN_Q(NOTE_FA),  SN_Q(NOTE_FA),  SN_Q(NOTE_MI),  SN_Q(NOTE_MI),  SN_Q(NOTE_RE), SN_Q(NOTE_RE), SN_H(NOTE_DO),

    SN_Q(NOTE_SOL), SN_Q(NOTE_SOL), SN_Q(NOTE_FA),  SN_Q(NOTE_FA),  SN_Q(NOTE_MI), SN_Q(NOTE_MI), SN_H(NOTE_RE),
    SN_Q(NOTE_SOL), SN_Q(NOTE_SOL), SN_Q(NOTE_FA),  SN_Q(NOTE_FA),  SN_Q(NOTE_MI), SN_Q(NOTE_MI), SN_H(NOTE_RE),

    SN_Q(NOTE_DO),  SN_Q(NOTE_DO),  SN_Q(NOTE_SOL), SN_Q(NOTE_SOL), SN_Q(NOTE_LA), SN_Q(NOTE_LA), SN_H(NOTE_SOL),
    SN_Q(NOTE_FA),  SN_Q(NOTE_FA),  SN_Q(NOTE_MI),  SN_Q(NOTE_MI),  SN_Q(NOTE_RE), SN_Q(NOTE_RE), SN_H(NOTE_DO)
};

/* -------------------------------------------------------------- 两只老虎
 *  Do Re Mi Do | Do Re Mi Do | Mi Fa Sol | Mi Fa Sol
 *  Sol La Sol Fa Mi Do | Sol La Sol Fa Mi Do | Do Sol Do | Do Sol Do
 */
static const SongNote s_tigers[] = {
    SN_Q(NOTE_DO),  SN_Q(NOTE_RE),  SN_Q(NOTE_MI),  SN_Q(NOTE_DO),
    SN_Q(NOTE_DO),  SN_Q(NOTE_RE),  SN_Q(NOTE_MI),  SN_Q(NOTE_DO),

    SN_Q(NOTE_MI),  SN_Q(NOTE_FA),  SN_H(NOTE_SOL),
    SN_Q(NOTE_MI),  SN_Q(NOTE_FA),  SN_H(NOTE_SOL),

    SN_Q(NOTE_SOL), SN_Q(NOTE_LA),  SN_Q(NOTE_SOL), SN_Q(NOTE_FA), SN_Q(NOTE_MI), SN_H(NOTE_DO),
    SN_Q(NOTE_SOL), SN_Q(NOTE_LA),  SN_Q(NOTE_SOL), SN_Q(NOTE_FA), SN_Q(NOTE_MI), SN_H(NOTE_DO),

    SN_Q(NOTE_DO),  SN_Q(NOTE_SOL), SN_H(NOTE_DO),
    SN_Q(NOTE_DO),  SN_Q(NOTE_SOL), SN_H(NOTE_DO)
};

/* --------------------------------------------------------- 备用曲谱模板
 *
 * 《稻香》这一段已经移除：我之前凭空转写的旋律和原曲对不上，
 * 与其在曲库里放一首"名字对、调子错"的曲子，不如先拿掉。
 *
 * 想把任何一首歌放进来，只需要按简谱把音写成一串 SN_Q / SN_H：
 *
 *   简谱 1 2 3 4 5 6 7  对应  NOTE_DO RE MI FA SOL LA SI
 *   SN_Q(NOTE_x)      = 中音区一拍（400 ms）
 *   SN_H(NOTE_x)      = 中音区两拍（800 ms）
 *   SNO_Q(NOTE_x, o)  = 指定音区一拍，o = OCT_LOW / OCT_MID / OCT_HIGH
 *   SNO_H(NOTE_x, o)  = 指定音区两拍
 *   SN_F(频率, 毫秒)  = 直接给频率，可写任意 Hz
 *   SN_R(毫秒)        = 休止
 *
 * 把简谱贴给我，我按这张表转成下面的数组即可，不需要你手写 C 代码。
 */

/* ------------------------------------------------------ C4 定时炸弹倒计时
 *
 * 上包后 C4 的"嘀——嘀——"加速提示，最后一声低频爆响。
 * 全部用 SN_F 直接指定频率：1000 Hz 是电子计时器的提示音，
 * 140 Hz 是模拟爆炸的低频冲击。
 *
 * 加速的方式是**间隔逐次缩短**，从 1.6 秒一路压到 0.1 秒，
 * 最后三声提速到 1500 Hz，紧接一记 140 Hz 的低频爆响。
 * 全程约 23 秒，前面留足时间，越到后面越急。
 */
static const SongNote s_c4[] = {
    SN_F(1000U, 60U), SN_R(1600U),
    SN_F(1000U, 60U), SN_R(1500U),
    SN_F(1000U, 60U), SN_R(1400U),
    SN_F(1000U, 60U), SN_R(1300U),
    SN_F(1000U, 60U), SN_R(1200U),
    SN_F(1000U, 60U), SN_R(1100U),
    SN_F(1000U, 60U), SN_R(1000U),
    SN_F(1000U, 60U), SN_R(950U),
    SN_F(1000U, 60U), SN_R(900U),
    SN_F(1000U, 60U), SN_R(850U),
    SN_F(1000U, 60U), SN_R(800U),
    SN_F(1000U, 60U), SN_R(750U),
    SN_F(1000U, 60U), SN_R(700U),
    SN_F(1000U, 60U), SN_R(650U),
    SN_F(1000U, 60U), SN_R(600U),
    SN_F(1000U, 60U), SN_R(550U),
    SN_F(1000U, 60U), SN_R(500U),
    SN_F(1000U, 60U), SN_R(450U),
    SN_F(1000U, 60U), SN_R(400U),
    SN_F(1000U, 60U), SN_R(350U),
    SN_F(1000U, 60U), SN_R(300U),
    SN_F(1000U, 60U), SN_R(260U),
    SN_F(1200U, 60U), SN_R(220U),
    SN_F(1200U, 60U), SN_R(180U),
    SN_F(1500U, 60U), SN_R(140U),
    SN_F(1500U, 60U), SN_R(100U),
    SN_F(1500U, 60U), SN_R(60U),
    SN_F(1500U, 60U), SN_R(60U),
    SN_F(140U, 700U), SN_R(400U)          /* 爆响 */
};


#include "song_qinghuaci.inc"
#include "song_faruxue.inc"
#include "song_accompaniment.inc"
#include "song_c4_sample.inc"
#include "song_luxiaoyu.inc"

/* -------------------------------------------------------------- 曲库表 */
static const SongDef s_builtin[] = {
    {
        "Twinkle Star",
        s_twinkle,
        (uint16_t)(sizeof(s_twinkle) / sizeof(s_twinkle[0])),
        true,
        0U, NULL, 0U, NULL
    },
    {
        "Two Tigers",
        s_tigers,
        (uint16_t)(sizeof(s_tigers) / sizeof(s_tigers[0])),
        true,
        0U, NULL, 0U, NULL
    },
    {
        "C4 Timer",
        s_c4,
        (uint16_t)(sizeof(s_c4) / sizeof(s_c4[0])),
        false,
        0U, NULL, 0U, NULL
    },
    {
        "QingHuaCi",
        s_qinghuaci,
        (uint16_t)(sizeof(s_qinghuaci) / sizeof(s_qinghuaci[0])),
        false,
        TIMBRE_PIANO + 1U,
        s_qinghuaci_accompaniment,
        (uint16_t)(sizeof(s_qinghuaci_accompaniment) / sizeof(s_qinghuaci_accompaniment[0])), NULL
    },
    {
        "FaRuXue",
        s_faruxue,
        (uint16_t)(sizeof(s_faruxue) / sizeof(s_faruxue[0])),
        false,
        TIMBRE_PIANO + 1U,
        s_faruxue_accompaniment,
        (uint16_t)(sizeof(s_faruxue_accompaniment) / sizeof(s_faruxue_accompaniment[0])), NULL
    },
    {
        "C4", NULL, 0U, false, 0U, NULL, 0U, &s_c4_sample
    },
    {
        "LuXiaoYu", s_luxiaoyu,
        (uint16_t)(sizeof(s_luxiaoyu) / sizeof(s_luxiaoyu[0])),
        false, TIMBRE_PIANO + 1U, s_luxiaoyu_accompaniment,
        (uint16_t)(sizeof(s_luxiaoyu_accompaniment) / sizeof(s_luxiaoyu_accompaniment[0])), NULL
    }
};

/* -------------------------------------------------------------- 对外接口 */
uint8_t Song_GetBuiltinCount(void)
{
    return (uint8_t)(sizeof(s_builtin) / sizeof(s_builtin[0]));
}

const SongDef *Song_GetBuiltin(uint8_t index)
{
    if (index >= Song_GetBuiltinCount()) {
        return NULL;
    }
    return &s_builtin[index];
}

/* ------------------------------------------------------------ 开机音效
 *
 * 上电时两只蜂鸣器快速交替弹一段上行的 8-Bit 琶音，复古游戏味道。
 * 不进曲库列表，只在启动画面期间播一次。
 * 用 SN_F 精确指定频率，避免受音区/移调影响。
 */
static const SongNote s_boot_chime[] = {
    SN_F(523U,  90U), SN_R(20U),      /* C5 */
    SN_F(659U,  90U), SN_R(20U),      /* E5 */
    SN_F(784U,  90U), SN_R(20U),      /* G5 */
    SN_F(1047U, 90U), SN_R(20U),      /* C6 */
    SN_F(659U,  70U), SN_R(15U),      /* E5 */
    SN_F(1047U, 70U), SN_R(15U),      /* C6 */
    SN_F(1568U, 260U), SN_R(80U)      /* G6，收尾 */
};

static const SongDef s_boot_chime_def = {
    "Boot Chime",
    s_boot_chime,
    (uint16_t)(sizeof(s_boot_chime) / sizeof(s_boot_chime[0])),
    false,
    0U, NULL, 0U, NULL
};

const SongDef *Song_GetBootChime(void)
{
    return &s_boot_chime_def;
}

/* ------------------------------------------------------------ 菜单提示音
 *
 * 主菜单七个条目各配一个不同的短音，风格区分开：
 *   index 0 演奏     明亮上行五度，像"开始"
 *   index 1 音区设置 下行两度，空间感
 *   index 2 移调设置 小三度上去再回来，有一点犹豫
 *   index 3 节拍器   两声同音，就是节拍器本身的"嗒嗒"
 *   index 4 录音     下行大六度，红色警示感
 *   index 5 曲库     三连音上行，像翻书
 *   index 6 系统设置 低音沉稳，机械感
 *
 * 全部用 SN_F 精确指定频率，不受音区/移调影响；都不进曲库列表。
 */
static const SongNote s_blip_perform[]  = { SN_F(1047U,45U), SN_R(12U), SN_F(1568U,70U), SN_R(10U) };
static const SongNote s_blip_octave[]   = { SN_F(1318U,45U), SN_R(12U), SN_F( 880U,70U), SN_R(10U) };
static const SongNote s_blip_transpose[]= { SN_F(1047U,35U), SN_R(8U),  SN_F(1245U,35U), SN_R(8U), SN_F(1047U,55U), SN_R(10U) };
static const SongNote s_blip_metronome[]= { SN_F(1500U,40U), SN_R(70U), SN_F(1500U,40U), SN_R(10U) };
static const SongNote s_blip_record[]   = { SN_F(1760U,55U), SN_R(15U), SN_F(1175U,80U), SN_R(10U) };
static const SongNote s_blip_library[]  = { SN_F( 880U,35U), SN_R(8U),  SN_F(1047U,35U), SN_R(8U), SN_F(1318U,60U), SN_R(10U) };
static const SongNote s_blip_settings[] = { SN_F( 660U,60U), SN_R(15U), SN_F( 494U,90U), SN_R(10U) };

#define BLIP_ENTRY(name, arr) \
    { name, arr, (uint16_t)(sizeof(arr) / sizeof(arr[0])), false, 0U, NULL, 0U, NULL }

static const SongDef s_menu_blips[] = {
    BLIP_ENTRY("Blip Perform",   s_blip_perform),
    BLIP_ENTRY("Blip Octave",    s_blip_octave),
    BLIP_ENTRY("Blip Transpose", s_blip_transpose),
    BLIP_ENTRY("Blip Metronome", s_blip_metronome),
    BLIP_ENTRY("Blip Record",    s_blip_record),
    BLIP_ENTRY("Blip Library",   s_blip_library),
    BLIP_ENTRY("Blip Settings",  s_blip_settings)
};

const SongDef *Song_GetMenuBlip(uint8_t index)
{
    if (index >= (uint8_t)(sizeof(s_menu_blips) / sizeof(s_menu_blips[0]))) {
        return NULL;
    }
    return &s_menu_blips[index];
}
