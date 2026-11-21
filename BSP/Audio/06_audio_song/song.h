/* ============================================================================
 *  song.h   —— 内置曲目播放
 *
 *  模块：06_audio_song
 *  说明：曲谱是一张静态表，播放器按毫秒时值逐音调度，非阻塞。
 *        播放时套用当前的移调，但不套用当前音区——音区由曲谱自己决定。
 * ========================================================================== */

#ifndef SONG_H
#define SONG_H

#include "audio_types.h"
#include "sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 休止符：这一格不发声，只占时间 */
#define SONG_NOTE_REST     0xFFU

/* 一个音符：音高 + 音区 + 时值（毫秒）
 *
 * freq_hz 不为 0 时**直接按这个频率发声**，note / octave 被忽略。
 * 这样曲谱不必被七个自然音限制，可以写升降音、1000 Hz 的提示音、
 * 低频的爆破声之类的任意频率。
 */
typedef struct {
    uint8_t  note;      /* NoteId，或 SONG_NOTE_REST */
    uint8_t  octave;    /* OctaveId */
    uint16_t dur_ms;    /* 时值，毫秒 */
    uint16_t freq_hz;   /* 0 = 按 note/octave 算；非 0 = 直接用这个频率 */
} SongNote;

/* 一首曲子 */
typedef struct {
    const char     *name;    /* ASCII 名称，显示在曲库列表 */
    const SongNote *notes;
    uint16_t        count;
    bool            loop;
    uint8_t         timbre;  /* 0 = current timbre; TIMBRE_x + 1 = temporary preset */
    const SongNote *accompaniment; /* Optional second monophonic track, same origin. */
    uint16_t        accompaniment_count;
    const SampleDef *sample; /* Optional original recording; notes/count are unused. */
} SongDef;

typedef enum {
    SONG_IDLE    = 0,
    SONG_PLAYING = 1,
    SONG_PAUSED  = 2
} SongState;

/* 两个音之间的短间隙，让相邻同度音听得出来是两下 */
#define SONG_ARTICULATION_GAP_MS   30U

void      Song_Init(void);

/* 开始播放。返回 false 表示曲谱指针或内容非法 */
bool      Song_Play(const SongDef *def);

/* 以固定音量播放：开机琶音、系统提示音使用，不跟随全局音量。
 * permille 为 0 时等同于 Song_Play（跟随全局音量）。 */
bool      Song_PlayGain(const SongDef *def, uint16_t permille);

void      Song_Pause(void);
void      Song_Resume(void);     /* 单轨重播当前格；双轨从共同暂停位置恢复 */
void      Song_Stop(void);

/* 播放速度：0 = 0.5 倍（慢），1 = 1.0 倍（正常），2 = 2.0 倍（快）。
 * 曲谱仅缩放时间轴，音高不变；双轨保持当前位置，单轨下一格生效。
 * PCM 保持采样位置并立即改变读取速率，音高随倍速变化。 */
void      Song_SetSpeed(uint8_t level);
uint8_t   Song_GetSpeed(void);

SongState Song_GetState(void);
const SongDef *Song_GetCurrent(void);

/* ---------------------------------------------------------------- 播放进度
 *
 * 供回放界面画进度条。进度按毫秒累计，暂停时不增长。
 */
uint8_t  Song_GetProgress(void);    /* 0 ~ 100 */
uint32_t Song_GetElapsedMs(void);
uint32_t Song_GetTotalMs(void);

/* 主循环里周期调用，内部按 HAL_GetTick 调度，不阻塞 */
void      Song_Tick(void);

/* ------------------------------------------------------------ 内置曲目库
 *
 * H 的曲库页面用它来列举，不需要知道曲谱内容。
 */
uint8_t        Song_GetBuiltinCount(void);
const SongDef *Song_GetBuiltin(uint8_t index);

/* 开机音效：一段很短的琶音，不进曲库列表，只在启动时播一次 */
const SongDef *Song_GetBootChime(void);

/* 菜单提示音：从主菜单进入子界面时响一下 */
const SongDef *Song_GetMenuBlip(uint8_t index);   /* index = 主菜单条目号 0~6 */

#ifdef __cplusplus
}
#endif

#endif /* SONG_H */
