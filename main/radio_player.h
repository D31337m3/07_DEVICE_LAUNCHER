#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize internet radio player
void radio_player_init(void);

// Play a radio stream from URL
void radio_player_play(const char *url);

#ifdef __cplusplus
}
#endif
