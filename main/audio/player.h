#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 播放器状态（与 audio_player_callback_event_t 对应） */
enum {
    AUDIO_EVT_IDLE = 0,
    AUDIO_EVT_NEXT,
    AUDIO_EVT_PLAYING,
    AUDIO_EVT_PAUSE,
    AUDIO_EVT_SHUTDOWN,
    AUDIO_EVT_UNKNOWN,
};

/* 初始化 ES8311 + 播放任务（首次进入音乐页时调用） */
esp_err_t audio_init(void);
bool audio_ready(void);

esp_err_t audio_play_file(const char *path);
void audio_pause(void);
void audio_resume(void);
void audio_stop(void);
bool audio_is_playing(void);

/* 交给其它模块（视频音轨）使用前释放 ES8311 输出 */
void audio_release(void);

/* 彻底释放播放器任务与 codec（视频播放前调用，腾出内部 RAM） */
void audio_shutdown(void);

void audio_set_volume(int volume);
int audio_volume(void);

void audio_set_state_cb(void (*cb)(int state));

#ifdef __cplusplus
}
#endif
