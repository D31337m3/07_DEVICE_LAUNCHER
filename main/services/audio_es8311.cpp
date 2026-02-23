#include "services/audio_es8311.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "nvs.h"

#include "es8311.h"

#include "app_pins.h"
#include "i2c_bus.h"

static const char *TAG = "audio";

static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;
static es8311_handle_t s_codec = NULL;
static bool s_ready = false;
static bool s_ui_sounds_enabled = true;

static bool s_stream_active = false;
static int s_hw_sample_rate = 16000;
static SemaphoreHandle_t s_hw_mutex = NULL;

static bool s_enabled = true;
static bool s_muted = false;
static int s_volume = 70;
static int s_mic_gain_ui = 40;

static int s_mic_level = 0;
static uint32_t s_last_mic_level_ms = 0;

static void nvs_load_ui_sounds(void)
{
    nvs_handle_t h;
    if (nvs_open("audio", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 1;
    if (nvs_get_u8(h, "ui_snd", &v) == ESP_OK) {
        s_ui_sounds_enabled = (v != 0);
    }

    if (nvs_get_u8(h, "en", &v) == ESP_OK) {
        s_enabled = (v != 0);
    }
    if (nvs_get_u8(h, "mute", &v) == ESP_OK) {
        s_muted = (v != 0);
    }
    int32_t iv = 0;
    if (nvs_get_i32(h, "vol", &iv) == ESP_OK) {
        s_volume = (int)iv;
    }
    if (nvs_get_i32(h, "mic", &iv) == ESP_OK) {
        s_mic_gain_ui = (int)iv;
    }
    nvs_close(h);
}

static void nvs_save_ui_sounds(void)
{
    nvs_handle_t h;
    if (nvs_open("audio", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "ui_snd", s_ui_sounds_enabled ? 1 : 0);
    nvs_set_u8(h, "en", s_enabled ? 1 : 0);
    nvs_set_u8(h, "mute", s_muted ? 1 : 0);
    nvs_set_i32(h, "vol", s_volume);
    nvs_set_i32(h, "mic", s_mic_gain_ui);
    nvs_commit(h);
    nvs_close(h);
}

static void pa_gpio_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << APP_PIN_NUM_AUDIO_PA_EN);
    gpio_config(&io_conf);
    gpio_set_level(APP_PIN_NUM_AUDIO_PA_EN, 1);
}

static void pa_set_enabled(bool enabled)
{
    gpio_set_level(APP_PIN_NUM_AUDIO_PA_EN, enabled ? 1 : 0);
}

static es8311_mic_gain_t mic_gain_from_ui(int ui_0_100)
{
    if (ui_0_100 < 0) ui_0_100 = 0;
    if (ui_0_100 > 100) ui_0_100 = 100;
    if (ui_0_100 < 10) return ES8311_MIC_GAIN_0DB;
    if (ui_0_100 < 25) return ES8311_MIC_GAIN_6DB;
    if (ui_0_100 < 40) return ES8311_MIC_GAIN_12DB;
    if (ui_0_100 < 55) return ES8311_MIC_GAIN_18DB;
    if (ui_0_100 < 70) return ES8311_MIC_GAIN_24DB;
    if (ui_0_100 < 80) return ES8311_MIC_GAIN_30DB;
    if (ui_0_100 < 90) return ES8311_MIC_GAIN_36DB;
    return ES8311_MIC_GAIN_42DB;
}

// Forward declarations for helpers defined later.
static void deinit_audio_hw(void);
static esp_err_t init_audio_hw(void);

static void apply_codec_settings(void)
{
    if (!s_codec) {
        return;
    }

    es8311_voice_volume_set(s_codec, s_muted ? 0 : s_volume, NULL);
    es8311_voice_mute(s_codec, s_muted);
    es8311_microphone_config(s_codec, false);
    es8311_microphone_gain_set(s_codec, mic_gain_from_ui(s_mic_gain_ui));
}

static esp_err_t ensure_audio_ready(void)
{
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ready) {
        // Best-effort: keep PA on.
        pa_set_enabled(true);
        return ESP_OK;
    }
    return init_audio_hw();
}

static esp_err_t write_with_recover(const void *data, size_t len, TickType_t timeout)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_audio_ready();
    if (err != ESP_OK) {
        return err;
    }

    size_t bytes_written = 0;
    err = i2s_channel_write(s_tx, data, len, &bytes_written, timeout);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "i2s write failed (%s); reinitializing audio and retrying", esp_err_to_name(err));
    deinit_audio_hw();
    err = init_audio_hw();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio reinit failed: %s", esp_err_to_name(err));
        return err;
    }

    bytes_written = 0;
    return i2s_channel_write(s_tx, data, len, &bytes_written, timeout);
}

static void deinit_audio_hw(void)
{
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
    if (s_rx) {
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    if (s_codec) {
        es8311_delete(s_codec);
        s_codec = NULL;
    }
    pa_set_enabled(false);
    s_ready = false;
}

static esp_err_t init_audio_hw_with_rate(int sample_rate_hz)
{
    pa_gpio_init();
    pa_set_enabled(true);

    // I2S TX + RX
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "i2s_new_channel failed");

    const uint32_t rate_hz = (sample_rate_hz <= 0) ? 16000u : (uint32_t)sample_rate_hz;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = APP_PIN_NUM_I2S_MCLK,
            .bclk = APP_PIN_NUM_I2S_BCLK,
            .ws = APP_PIN_NUM_I2S_WS,
            .dout = APP_PIN_NUM_I2S_DOUT,
            .din = APP_PIN_NUM_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "i2s tx init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg), TAG, "i2s rx init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s tx enable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "i2s rx enable failed");

    // Codec over shared I2C
    s_codec = es8311_create(app_i2c_bus(), ES8311_ADDRRES_0);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "es8311_create failed");
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = (int)(rate_hz * 384u),
        .sample_frequency = (int)rate_hz,
    };
    ESP_RETURN_ON_ERROR(es8311_init(s_codec, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), TAG, "es8311_init failed");

    apply_codec_settings();

    s_ready = true;
    s_hw_sample_rate = (int)rate_hz;
    ESP_LOGI(TAG, "Audio init OK (rate=%d enabled=%d muted=%d vol=%d mic=%d)", (int)rate_hz, (int)s_enabled, (int)s_muted, s_volume, s_mic_gain_ui);
    return ESP_OK;
}

static esp_err_t init_audio_hw(void)
{
    return init_audio_hw_with_rate(16000);
}

esp_err_t audio_es8311_init(void)
{
    if (!s_hw_mutex) {
        s_hw_mutex = xSemaphoreCreateMutex();
    }
    nvs_load_ui_sounds();
    if (!s_enabled) {
        // Respect power-saving disable on boot.
        deinit_audio_hw();
        ESP_LOGW(TAG, "Audio disabled by settings");
        return ESP_OK;
    }
    return init_audio_hw();
}

void audio_es8311_set_enabled(bool enabled)
{
    s_enabled = enabled;
    nvs_save_ui_sounds();

    if (!enabled && s_stream_active) {
        audio_es8311_stream_end();
    }

    if (!enabled) {
        deinit_audio_hw();
        return;
    }
    if (!s_ready) {
        init_audio_hw();
    }
}

bool audio_es8311_get_enabled(void)
{
    return s_enabled;
}

void audio_es8311_set_volume(int volume_0_100)
{
    if (volume_0_100 < 0) volume_0_100 = 0;
    if (volume_0_100 > 100) volume_0_100 = 100;
    s_volume = volume_0_100;
    nvs_save_ui_sounds();
    if (s_ready) {
        apply_codec_settings();
    }
}

int audio_es8311_get_volume(void)
{
    return s_volume;
}

void audio_es8311_set_muted(bool muted)
{
    s_muted = muted;
    nvs_save_ui_sounds();
    if (s_ready) {
        apply_codec_settings();
    }
}

bool audio_es8311_get_muted(void)
{
    return s_muted;
}

void audio_es8311_set_mic_gain(int gain_0_100)
{
    if (gain_0_100 < 0) gain_0_100 = 0;
    if (gain_0_100 > 100) gain_0_100 = 100;
    s_mic_gain_ui = gain_0_100;
    nvs_save_ui_sounds();
    if (s_ready) {
        apply_codec_settings();
    }
}

int audio_es8311_get_mic_gain(void)
{
    return s_mic_gain_ui;
}

int audio_es8311_get_mic_level(void)
{
    if (!s_ready || !s_enabled || !s_rx) {
        return 0;
    }

    // Limit sampling rate to keep UI light.
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (now_ms - s_last_mic_level_ms < 80) {
        return s_mic_level;
    }
    s_last_mic_level_ms = now_ms;

    int16_t samples[256];
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx, samples, sizeof(samples), &bytes_read, 0);
    if (err != ESP_OK || bytes_read < 4) {
        // decay
        s_mic_level = (s_mic_level * 8) / 10;
        return s_mic_level;
    }

    const int count = (int)(bytes_read / sizeof(int16_t));
    int peak = 0;
    for (int i = 0; i < count; i++) {
        int v = samples[i];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    int level = (peak * 100) / 32767;
    if (level > 100) level = 100;
    // small smoothing
    s_mic_level = (s_mic_level * 7 + level * 3) / 10;
    return s_mic_level;
}

esp_err_t audio_es8311_mic_read(void *dst, size_t dst_bytes, size_t *bytes_read, int timeout_ms)
{
    if (bytes_read) {
        *bytes_read = 0;
    }
    if (!dst || dst_bytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready || !s_enabled || !s_rx || !s_hw_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream_active) {
        // Avoid fighting with stream reconfiguration.
        return ESP_ERR_INVALID_STATE;
    }

    const TickType_t to = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_hw_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t br = 0;
    esp_err_t err = i2s_channel_read(s_rx, dst, dst_bytes, &br, to);
    xSemaphoreGive(s_hw_mutex);
    if (bytes_read) {
        *bytes_read = br;
    }
    return err;
}

void audio_es8311_set_ui_sounds_enabled(bool enabled)
{
    s_ui_sounds_enabled = enabled;
    nvs_save_ui_sounds();
}

bool audio_es8311_get_ui_sounds_enabled(void)
{
    return s_ui_sounds_enabled;
}

esp_err_t audio_es8311_play_click(void)
{
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream_active) {
        return ESP_OK;
    }
    if (s_muted) {
        return ESP_OK;
    }
    if (!s_ui_sounds_enabled) {
        return ESP_OK;
    }

    constexpr int kSampleRate = 16000;
    constexpr float kFreq = 1200.0f;
    constexpr int kMs = 35;
    constexpr int kSamples = (kSampleRate * kMs) / 1000;

    static int16_t samples[kSamples * 2];
    for (int i = 0; i < kSamples; i++) {
        const float env = 1.0f - ((float)i / (float)kSamples);
        const float x = sinf(2.0f * (float)M_PI * kFreq * (float)i / (float)kSampleRate);
        const int16_t s = (int16_t)(x * env * 5000);
        samples[i * 2 + 0] = s;
        samples[i * 2 + 1] = s;
    }

    return write_with_recover(samples, sizeof(samples), pdMS_TO_TICKS(250));
}

esp_err_t audio_es8311_play_beep(void)
{
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream_active) {
        return ESP_OK;
    }
    if (s_muted) {
        return ESP_OK;
    }
    constexpr int kSampleRate = 16000;
    constexpr float kFreq = 880.0f;
    constexpr int kMs = 200;
    constexpr int kSamples = (kSampleRate * kMs) / 1000;

    // 16-bit stereo interleaved
    static int16_t samples[kSamples * 2];
    for (int i = 0; i < kSamples; i++) {
        float x = sinf(2.0f * (float)M_PI * kFreq * (float)i / (float)kSampleRate);
        int16_t s = (int16_t)(x * 8000);
        samples[i * 2 + 0] = s;
        samples[i * 2 + 1] = s;
    }

    return write_with_recover(samples, sizeof(samples), pdMS_TO_TICKS(1000));
}

esp_err_t audio_es8311_play_spray_rattle(void)
{
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_stream_active) {
        return ESP_OK;
    }
    if (s_muted) {
        return ESP_OK;
    }
    
    constexpr int kSampleRate = 16000;
    constexpr int kDurationMs = 800;  // 800ms spray can rattle
    constexpr int kTotalSamples = (kSampleRate * kDurationMs) / 1000;
    
    // Allocate buffer for stereo interleaved samples
    static int16_t samples[kTotalSamples * 2];
    
    // Generate spray paint can rattle effect:
    // 3 short bursts of noise with decay envelope
    int burst_positions[] = {0, 2400, 4800};  // Start times in samples
    int burst_lengths[] = {2000, 1800, 1600}; // Decreasing burst lengths
    
    memset(samples, 0, sizeof(samples));
    
    for (int b = 0; b < 3; b++) {
        int start = burst_positions[b];
        int length = burst_lengths[b];
        
        for (int i = 0; i < length && (start + i) < kTotalSamples; i++) {
            // Generate white noise
            int16_t noise = (int16_t)((esp_random() % 16000) - 8000);
            
            // Apply envelope (attack + exponential decay)
            float envelope;
            if (i < 100) {
                // Fast attack
                envelope = (float)i / 100.0f;
            } else {
                // Exponential decay
                float decay_pos = (float)(i - 100) / (float)(length - 100);
                envelope = expf(-decay_pos * 3.0f);
            }
            
            // Apply high-pass character (spray can metallic rattle)
            float hp_factor = 0.7f + 0.3f * sinf(2.0f * M_PI * 3000.0f * (float)i / (float)kSampleRate);
            
            int16_t s = (int16_t)(noise * envelope * hp_factor * 0.6f);
            
            int idx = start + i;
            samples[idx * 2 + 0] = s;
            samples[idx * 2 + 1] = s;
        }
    }
    
    return write_with_recover(samples, sizeof(samples), pdMS_TO_TICKS(2000));
}

esp_err_t audio_es8311_stream_begin(int sample_rate_hz)
{
    if (!s_hw_mutex) {
        s_hw_mutex = xSemaphoreCreateMutex();
    }
    if (!s_hw_mutex) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_hw_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_enabled) {
        s_enabled = true;
        nvs_save_ui_sounds();
    }

    if (!s_ready) {
        esp_err_t err = init_audio_hw_with_rate(sample_rate_hz);
        if (err != ESP_OK) {
            xSemaphoreGive(s_hw_mutex);
            return err;
        }
    } else if (s_hw_sample_rate != sample_rate_hz) {
        deinit_audio_hw();
        esp_err_t err = init_audio_hw_with_rate(sample_rate_hz);
        if (err != ESP_OK) {
            // Best-effort restore default rate.
            deinit_audio_hw();
            init_audio_hw();
            xSemaphoreGive(s_hw_mutex);
            return err;
        }
    }

    s_stream_active = true;
    xSemaphoreGive(s_hw_mutex);
    return ESP_OK;
}

esp_err_t audio_es8311_stream_write(const void *pcm_s16_interleaved, size_t bytes, int timeout_ms)
{
    if (!s_ready || !s_enabled || !s_tx || !pcm_s16_interleaved || bytes == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_hw_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    const TickType_t to = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_hw_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx, pcm_s16_interleaved, bytes, &bytes_written, to);
    xSemaphoreGive(s_hw_mutex);
    return err;
}

void audio_es8311_stream_end(void)
{
    if (!s_hw_mutex) {
        s_stream_active = false;
        return;
    }
    if (xSemaphoreTake(s_hw_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        s_stream_active = false;
        return;
    }
    s_stream_active = false;
    if (s_ready && s_hw_sample_rate != 16000) {
        deinit_audio_hw();
        init_audio_hw();
    }
    xSemaphoreGive(s_hw_mutex);
}

bool audio_es8311_stream_is_active(void)
{
    return s_stream_active;
}
