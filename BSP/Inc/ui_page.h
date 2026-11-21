#ifndef UI_PAGE_H
#define UI_PAGE_H

#include "bsp_encoder.h"

#include <stdbool.h>
#include <stdint.h>

/* 高级视觉效果开关：调试或性能降级时可单独关闭。 */
#ifndef UI_ENABLE_GRADIENTS
#define UI_ENABLE_GRADIENTS 1
#endif
#ifndef UI_ENABLE_ICONS
#define UI_ENABLE_ICONS 1
#endif
#ifndef UI_ENABLE_ANIMATIONS
#define UI_ENABLE_ANIMATIONS 1
#endif
#ifndef UI_ENABLE_ALPHA
#define UI_ENABLE_ALPHA 1
#endif

typedef enum {
    UI_PAGE_MAIN = 0,
    UI_PAGE_PERFORM,
    UI_PAGE_OCTAVE,
    UI_PAGE_TRANSPOSE,
    UI_PAGE_METRONOME,
    UI_PAGE_RECORD,
    UI_PAGE_LIBRARY,
    UI_PAGE_PLAYBACK,
    UI_PAGE_SETTINGS,
    UI_PAGE_RHYTHM,       /* 音乐游戏：内部首页/倒计时/游戏/暂停/结算 */
    UI_PAGE_MELODY,       /* 自动作曲：音色选择/生成播放 */
    UI_PAGE_DS_ROOM,
    UI_PAGE_HOME          /* 欢迎页：开机后停留在这里，长按旋钮才进主菜单 */
} ui_page_id_t;

typedef struct {
    ui_page_id_t page;

    uint8_t main_sel;       /* 主菜单选中项 0..8 */
    uint16_t bpm;           /* 节拍器 40..240 */

    uint8_t library_sel;    /* 曲库选中项 0..14（5 首示例 + 最多 10 条录音） */
    uint8_t playback_track; /* 回放曲目在合并列表中的索引 */
    bool playing;           /* 回放播放/暂停 */

    uint8_t settings_sel;   /* 系统设置选中项 0..2 */
    uint8_t brightness;     /* 1..5，UI 亮度 */

    uint8_t param_sel;      /* 演奏页/录音页当前调节对象：0=OCT 1=VOL 2=KEY */
    uint8_t record_mode;    /* 录音模式：0=新建 1=叠录已有录音 */
    uint8_t record_sel;     /* 叠录模式下选中的录音下标（0..track_count-1） */
    uint8_t metro_sig;      /* 节拍器拍号预设序号：0=2/4 1=3/4 2=4/4 3=6/8 */
    uint8_t metro_focus;    /* 节拍器调节焦点：0=BPM 1=拍号 */

    uint8_t saved_brightness;
    uint8_t saved_volume;
    uint8_t saved_tone;
} ui_page_state_t;

void ui_page_init(void);
void ui_page_handle_event(bsp_encoder_event_t event);
void ui_page_handle_func(void);
void ui_page_handle_func_long(void);   /* FUNC 长按：全局开关节拍器 */
void ui_page_tick(uint32_t now_ms);
ui_page_id_t ui_page_current(void);
const ui_page_state_t *ui_page_get_state(void);

#endif /* UI_PAGE_H */
