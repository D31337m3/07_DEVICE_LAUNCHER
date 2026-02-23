#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_es8311_init(void);

// Global enable/disable (power saving). When disabled, codec + I2S are stopped.
void audio_es8311_set_enabled(bool enabled);
bool audio_es8311_get_enabled(void);

// Output controls
void audio_es8311_set_volume(int volume_0_100);
int audio_es8311_get_volume(void);
void audio_es8311_set_muted(bool muted);
bool audio_es8311_get_muted(void);

// Microphone gain (0-100 UI scale, mapped to ES8311 gain steps)
void audio_es8311_set_mic_gain(int gain_0_100);
int audio_es8311_get_mic_gain(void);

// Mic level meter (0-100). Non-blocking.
int audio_es8311_get_mic_level(void);

// Read raw microphone PCM samples from the codec RX path.
// Format: 16-bit signed interleaved stereo at the current hardware sample rate (default 16000).
// Returns ESP_OK and sets bytes_read on success.
esp_err_t audio_es8311_mic_read(void *dst, size_t dst_bytes, size_t *bytes_read, int timeout_ms);
esp_err_t audio_es8311_play_beep(void);
esp_err_t audio_es8311_play_click(void);
esp_err_t audio_es8311_play_spray_rattle(void);

// Stream playback (MP3 player). Temporarily reconfigures I2S+codec to a
// different sample rate. While streaming, UI click/beep sounds are suppressed.
esp_err_t audio_es8311_stream_begin(int sample_rate_hz);
esp_err_t audio_es8311_stream_write(const void *pcm_s16_interleaved, size_t bytes, int timeout_ms);
void audio_es8311_stream_end(void);
bool audio_es8311_stream_is_active(void);

// UI click sounds preference (persisted in NVS).
void audio_es8311_set_ui_sounds_enabled(bool enabled);
bool audio_es8311_get_ui_sounds_enabled(void);

#ifdef __cplusplus
}
#endif
