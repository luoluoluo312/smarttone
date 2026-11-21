/* ============================================================================
 *  rhythm_chart.c —— 音乐游戏曲谱数据（与游戏逻辑分离）
 *
 *  曲目：欢乐颂 / Ode to Joy
 *        贝多芬第九交响曲主题的简易单音改编
 *  来源：公有领域的古典旋律。本文件是独立的八小节七键编配，
 *        不是任何商业 MIDI 的逐事件精确转录。
 *  参考：https://piano.rc-digital.it/en/ode-to-joy.html
 *        （该站简易 MIDI 标注 Public Domain；本包未下载、未附带其 MIDI 文件）
 *
 *  结构：4/4，72 BPM（四分音符约 833 ms），8 小节，30 个目标音符；
 *        首音 1500 ms，末音 26500 ms（dur 1500），整局固定 30000 ms；
 *        仅使用轨道 0..4（Do..Sol），保留 7 轨 UI；
 *        无升降号、无双音、无自动伴奏。
 *
 *  字段顺序（必须和 rhythm_chart_note_t 一致）：
 *      { uint16_t time_ms; uint8_t track; uint16_t dur_ms; }
 *      CSV 表头 time_ms,track,dur_ms —— 一致。
 *
 *  ⚠ 注意：录音模块的 NoteEvent 是另一个 12 字节结构：
 *      { uint8_t note; uint8_t octave; uint16_t freq_hz;
 *        uint32_t start_ms; uint16_t dur_ms; }
 *      字段顺序完全不同，不能把本文件的曲谱事件粘贴进 NoteEvent / 录音槽位。
 *
 *  换曲谱只需要替换本文件的 s_notes / 元数据，不动游戏逻辑、评分、UI。
 * ========================================================================== */

#include "rhythm_chart.h"

#include <stddef.h>

/* 布局自检：字段顺序/偏移/大小必须与 CSV 的 time_ms,track,dur_ms 对应，
 * 也防止以后有人把字段顺序改掉却继续用位置初始化。 */
typedef char rhythm_chart_time_offset_check[
    (offsetof(rhythm_chart_note_t, time_ms) == 0U) ? 1 : -1];
typedef char rhythm_chart_track_offset_check[
    (offsetof(rhythm_chart_note_t, track) == 2U) ? 1 : -1];
typedef char rhythm_chart_dur_offset_check[
    (offsetof(rhythm_chart_note_t, dur_ms) == 4U) ? 1 : -1];
typedef char rhythm_chart_note_size_check[
    (sizeof(rhythm_chart_note_t) == 6U) ? 1 : -1];

/* 30 个目标音符，时间必须递增；track 0=Do 1=Re 2=Mi 3=Fa 4=Sol。 */
static const rhythm_chart_note_t s_notes[] = {
    { 1500U, 2U,  750U}, /* Mi */
    { 2333U, 2U,  750U}, /* Mi */
    { 3167U, 3U,  750U}, /* Fa */
    { 4000U, 4U,  750U}, /* Sol */
    { 4833U, 4U,  750U}, /* Sol */
    { 5667U, 3U,  750U}, /* Fa */
    { 6500U, 2U,  750U}, /* Mi */
    { 7333U, 1U,  750U}, /* Re */
    { 8167U, 0U,  750U}, /* Do */
    { 9000U, 0U,  750U}, /* Do */
    { 9833U, 1U,  750U}, /* Re */
    {10667U, 2U,  750U}, /* Mi */
    {11500U, 2U,  750U}, /* Mi */
    {12333U, 1U,  750U}, /* Re */
    {13167U, 1U, 1500U}, /* Re（长音） */
    {14833U, 2U,  750U}, /* Mi */
    {15667U, 2U,  750U}, /* Mi */
    {16500U, 3U,  750U}, /* Fa */
    {17333U, 4U,  750U}, /* Sol */
    {18167U, 4U,  750U}, /* Sol */
    {19000U, 3U,  750U}, /* Fa */
    {19833U, 2U,  750U}, /* Mi */
    {20667U, 1U,  750U}, /* Re */
    {21500U, 0U,  750U}, /* Do */
    {22333U, 0U,  750U}, /* Do */
    {23167U, 1U,  750U}, /* Re */
    {24000U, 2U,  750U}, /* Mi */
    {24833U, 1U,  750U}, /* Re */
    {25667U, 0U,  750U}, /* Do */
    {26500U, 0U, 1500U}, /* Do（收尾长音） */
};

const rhythm_chart_t g_rhythm_chart = {
    "ODE TO JOY",
    "BEETHOVEN",
    "PUBLIC DOMAIN",
    s_notes,
    (uint16_t)(sizeof(s_notes) / sizeof(s_notes[0])),
    30000U
};
