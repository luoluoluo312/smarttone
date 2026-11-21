#ifndef SMARTOME_BSP_AUDIO_COMPAT_H
#define SMARTOME_BSP_AUDIO_COMPAT_H

/*
 * 兼容头：H 侧历史上包含 BSP/Inc/audio.h。
 * 真正的唯一音频接口定义在 BSP/Audio/04_audio_core/audio.h，
 * 这里只做转发，避免出现第二份过期的 3 音色/3 音量定义。
 */
#include "../Audio/04_audio_core/audio.h"

#endif /* SMARTOME_BSP_AUDIO_COMPAT_H */
