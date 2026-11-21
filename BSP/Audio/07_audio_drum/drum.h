/* ============================================================================
 *  drum.h   —— 打击乐（鼓组）
 *
 *  模块：07_audio_drum
 *  说明：用频率轨迹模拟打击乐，全部非阻塞。
 *
 *        原理：蜂鸣器本身只能发单频，但"音高快速变化的过程"能被耳朵感知成
 *              打击乐的特征——向下扫频像鼓皮的张力变化，高频交替像噪音成分，
 *              接近谐振点的高频短促像踩镲。
 *
 *        本模块只占用一个声部，优先级是最低的，永远不会挤掉琴音或旋律。
 *        声部被抢占时自动停止，不会残留响声。
 * ========================================================================== */

#ifndef DRUM_H
#define DRUM_H

#include "audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DRUM_KICK       = 0,   /* 低音鼓 */
    DRUM_SNARE      = 1,   /* 小军鼓 */
    DRUM_CLOSED_HAT = 2,   /* 踩镲（闭合） */
    DRUM_OPEN_HAT   = 3,   /* 踩镲（打开） */
    DRUM_TOM        = 4,   /* 通鼓 */
    DRUM_COUNT      = 5
} DrumId;

void Drum_Init(void);

/* 敲一下。返回 false 表示当前没有可用声部（琴音占满了）。 */
bool Drum_Play(DrumId id);

/* 立即停止正在进行的打击乐 */
void Drum_Stop(void);

bool Drum_IsPlaying(void);

/* 主循环周期调用，推进频率轨迹 */
void Drum_Tick(void);

/* 鼓件英文名，供界面显示 */
const char *Drum_GetName(DrumId id);

#ifdef __cplusplus
}
#endif

#endif /* DRUM_H */
