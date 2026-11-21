#ifndef RHYTHM_CHART_H
#define RHYTHM_CHART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 曲谱里的一格。time_ms 是相对"游戏时间零点"的命中时刻；
 * track 0..6 对应 Do/Re/Mi/Fa/Sol/La/Si；dur_ms 只用于后续视觉/判定扩展，
 * 当前版本不靠它控制发声时长（发声由玩家按下/松开决定）。 */
typedef struct {
    uint16_t time_ms;
    uint8_t  track;
    uint16_t dur_ms;
} rhythm_chart_note_t;

typedef struct {
    const char *title;          /* 首页显示的曲名（ASCII） */
    const char *subtitle;       /* 首页副标题（ASCII） */
    const char *source;         /* "ORIGINAL" 或正式授权来源标记 */
    const rhythm_chart_note_t *notes;
    uint16_t count;
    uint16_t duration_ms;       /* 整局时长，到点进结算 */
} rhythm_chart_t;

/* 游戏状态按这个上限分配 note 状态数组；换曲谱时不要超过。 */
#define RHYTHM_CHART_MAX_NOTES 96U

/* 当前曲谱。替换曲谱只需要改 rhythm_chart.c，不动游戏逻辑。 */
extern const rhythm_chart_t g_rhythm_chart;

#ifdef __cplusplus
}
#endif

#endif /* RHYTHM_CHART_H */
