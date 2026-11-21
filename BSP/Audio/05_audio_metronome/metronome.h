/* ============================================================================
 *  metronome.h   —— 节拍器
 *
 *  模块：05_audio_metronome
 *  说明：只产生节拍提示音，不负责按钮和界面。BPM 的设置与开关由 H 调用。
 *        提示音由 PB1/TIM3_CH4 独立产生，不占用演奏声部。
 * ========================================================================== */

#ifndef METRONOME_H
#define METRONOME_H

#include "audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void     Metronome_Init(void);

/* 返回 false 表示 BPM 超出 40~240 */
bool     Metronome_SetBpm(uint16_t bpm);
uint16_t Metronome_GetBpm(void);

/* 拍号：每小节几拍，2~6。重音落在每小节第一拍。
 * 界面提供 2/4、3/4、4/4、6/8 四种预设，对应 beats = 2、3、4、6。 */
bool    Metronome_SetBeatsPerBar(uint8_t beats);
uint8_t Metronome_GetBeatsPerBar(void);

/* PB1 节拍器音量：1=轻，2=中，3=响；越界返回 false。
 * 只影响节拍器点击；游戏倒计时提示音固定用第 3 档。 */
#define METRONOME_VOLUME_MIN      1U
#define METRONOME_VOLUME_MAX      3U
#define METRONOME_VOLUME_DEFAULT  3U

bool    Metronome_SetVolume(uint8_t level);
uint8_t Metronome_GetVolume(void);

void     Metronome_Start(void);
void     Metronome_Stop(void);
bool     Metronome_IsRunning(void);

/* 主循环里周期调用，内部按 HAL_GetTick 判断节拍点，不阻塞 */
void     Metronome_Tick(void);

#ifdef __cplusplus
}
#endif

#endif /* METRONOME_H */
