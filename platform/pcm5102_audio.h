
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool pcm5102_audio_init(uint32_t sample_rate);
void pcm5102_audio_push(int16_t left, int16_t right);
unsigned pcm5102_audio_free(void);
unsigned pcm5102_audio_level(void);
uint32_t pcm5102_audio_get_starvation_count(void);

#ifdef __cplusplus
}
#endif
