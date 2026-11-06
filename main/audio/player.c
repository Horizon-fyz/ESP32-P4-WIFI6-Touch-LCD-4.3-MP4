#include "player.h"

#include <stdio.h>
#include <string.h>

#include "audio_player.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio";

static esp_codec_dev_handle_t s_codec;
static bool s_opened;
static bool s_inited;
static int s_volume = 70;
static void (*s_state_cb)(int state);

static esp_err_t codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    if (!s_codec) {
        return ESP_FAIL;
    }
    if (s_opened) {
        esp_codec_dev_close(s_codec);
        s_opened = false;
    }

    esp_codec_dev_sample_info_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.bits_per_sample = (uint8_t)bits_cfg;
    fs.channel = (ch == I2S_SLOT_MODE_MONO) ? 1 : 2;
    fs.channel_mask = (fs.channel == 2) ? 0x03 : 0x01;
    fs.sample_rate = rate;
    fs.mclk_multiple = 256;

    int ret = esp_codec_dev_open(s_codec, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "打开 ES8311 失败: %d (%u Hz, %u bit, %u ch)", ret, (unsigned)rate,
                 (unsigned)bits_cfg, (unsigned)fs.channel);
        return ESP_FAIL;
    }
    s_opened = true;
    esp_codec_dev_set_out_vol(s_codec, s_volume);
    return ESP_OK;
}

static esp_err_t codec_write(void *audio_buffer, size_t len, size_t *bytes_written,
                             uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_codec || !s_opened) {
        return ESP_FAIL;
    }
    int ret = esp_codec_dev_write(s_codec, (void *)audio_buffer, (int)len);
    if (ret != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    if (bytes_written) {
        *bytes_written = len;
    }
    return ESP_OK;
}

static esp_err_t codec_mute(AUDIO_PLAYER_MUTE_SETTING setting)
{
    if (!s_codec) {
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_mute(s_codec, setting == AUDIO_PLAYER_MUTE);
    return ESP_OK;
}

static void player_event_cb(audio_player_cb_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    int state = AUDIO_EVT_UNKNOWN;
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        state = AUDIO_EVT_IDLE;
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT:
        state = AUDIO_EVT_NEXT;
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        state = AUDIO_EVT_PLAYING;
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        state = AUDIO_EVT_PAUSE;
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN:
        state = AUDIO_EVT_SHUTDOWN;
        break;
    default:
        state = AUDIO_EVT_UNKNOWN;
        break;
    }
    ESP_LOGI(TAG, "播放器事件 %d", state);
    if (s_state_cb) {
        s_state_cb(state);
    }
}

esp_err_t audio_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_codec = bsp_audio_codec_speaker_init();
    if (!s_codec) {
        ESP_LOGE(TAG, "ES8311 初始化失败");
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_codec, s_volume);

    audio_player_config_t config = {
        .mute_fn = codec_mute,
        .clk_set_fn = codec_set_fs,
        .write_fn = codec_write,
        .priority = 5,
        .coreID = tskNO_AFFINITY,
        .force_stereo = true,
        .write_fn2 = NULL,
        .write_ctx = NULL,
    };

    esp_err_t err = audio_player_new(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio_player_new 失败: %s", esp_err_to_name(err));
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
        return err;
    }
    audio_player_callback_register(player_event_cb, NULL);

    s_inited = true;
    ESP_LOGI(TAG, "音频播放器就绪（MP3 / WAV）");
    return ESP_OK;
}

bool audio_ready(void)
{
    return s_inited;
}

esp_err_t audio_play_file(const char *path)
{
    if (!s_inited || !path) {
        return ESP_ERR_INVALID_STATE;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "打开失败: %s", path);
        return ESP_FAIL;
    }
    esp_err_t err = audio_player_play(fp);
    if (err != ESP_OK) {
        fclose(fp);
        ESP_LOGE(TAG, "播放失败: %s", esp_err_to_name(err));
    }
    return err;
}

void audio_pause(void)
{
    if (s_inited) {
        audio_player_pause();
    }
}

void audio_resume(void)
{
    if (s_inited) {
        audio_player_resume();
    }
}

void audio_stop(void)
{
    if (s_inited) {
        audio_player_stop();
    }
}

bool audio_is_playing(void)
{
    return s_inited && audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING;
}

void audio_release(void)
{
    if (!s_inited) {
        return;
    }
    audio_player_stop();
    for (int i = 0; i < 25 && audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_opened) {
        esp_codec_dev_close(s_codec);
        s_opened = false;
    }
}

void audio_shutdown(void)
{
    if (!s_inited) {
        return;
    }
    audio_player_stop();
    for (int i = 0; i < 25 && audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    audio_player_delete();
    if (s_codec) {
        if (s_opened) {
            esp_codec_dev_close(s_codec);
            s_opened = false;
        }
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
    }
    s_inited = false;
    ESP_LOGI(TAG, "音频播放器已释放");
}

void audio_set_volume(int volume)
{
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    s_volume = volume;
    if (s_codec) {
        esp_codec_dev_set_out_vol(s_codec, s_volume);
    }
}

int audio_volume(void)
{
    return s_volume;
}

void audio_set_state_cb(void (*cb)(int state))
{
    s_state_cb = cb;
}

