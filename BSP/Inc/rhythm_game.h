#ifndef RHYTHM_GAME_H
#define RHYTHM_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 游戏内部状态（也用于主机测试） */
typedef enum {
    RHYTHM_STATE_HOME = 0,
    RHYTHM_STATE_COUNTDOWN,
    RHYTHM_STATE_PLAY,
    RHYTHM_STATE_PAUSE,
    RHYTHM_STATE_RESULT
} rhythm_game_state_t;

typedef enum {
    RHYTHM_GRADE_S = 0,
    RHYTHM_GRADE_A,
    RHYTHM_GRADE_B,
    RHYTHM_GRADE_C,
    RHYTHM_GRADE_D
} rhythm_grade_t;

typedef struct {
    uint8_t  state;
    uint16_t note_count;
    uint32_t points;          /* 音符得分之和：PERFECT 100 / GREAT 70 / GOOD 40 */
    uint16_t combo;
    uint16_t max_combo;
    uint16_t perfect;
    uint16_t great;
    uint16_t good;
    uint16_t miss;
    uint16_t extra;           /* 无目标的错误按键次数 */
    uint32_t chart_ms;        /* 当前曲谱时间（暂停时冻结） */
    uint32_t final_score;     /* 0..100000 */
    uint8_t  grade;
    uint8_t  difficulty;      /* 0=LOW 1=MED 2=HIGH */
    uint8_t  speed_pct;       /* 100 / 125 / 150 */
} rhythm_stats_t;

/* 注册按键回调、初始化内部状态。main 里调用一次即可。 */
void rhythm_game_init(void);

/* 从主菜单进入 / 离开。enter 会保存音区、移调、音色与节拍器状态并切到 PIANO；
 * leave 负责恢复它们，并停掉游戏声音。 */
void rhythm_game_enter(void);
void rhythm_game_leave(void);

/* 页面 tick：返回 true 表示请求回到主菜单（由 ui_page 负责真正切页）。 */
bool rhythm_game_tick(uint32_t now_ms);

/* 页面重绘（进入页面、从暂停恢复等由 ui_page 调用）。 */
void rhythm_game_draw(void);

/* EC11 事件（短按/长按/旋转）。 */
void rhythm_game_handle_event(bsp_encoder_event_t event);

/* bsp_notes 的按键回调：按下/松开都会进来。 */
void rhythm_game_on_key(uint8_t note, bool pressed, uint32_t now_ms);

/* 供主机测试与调试读取当前统计。 */
void rhythm_game_get_stats(rhythm_stats_t *out);

bool rhythm_game_active(void);

#ifdef __cplusplus
}
#endif

#endif /* RHYTHM_GAME_H */
