#ifndef AUDIO_PLATFORM_H
#define AUDIO_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_profile.h"

typedef void (*audio_mic_cb_t)(const uint8_t *pcm, size_t len, void *ctx);

bool audio_platform_init(const board_profile_t *board);
bool audio_platform_start_mic(audio_mic_cb_t cb, void *ctx);
void audio_platform_stop_mic(void);
bool audio_platform_write_speaker(const uint8_t *pcm, size_t len);
bool audio_platform_set_volume(uint8_t percent);
uint8_t audio_platform_get_volume(void);
bool audio_platform_speaker_idle(void);
void audio_platform_clear_speaker_buffer(void);
void audio_platform_finish_speaker(void);

#endif
