
#ifndef LGAMES_PORT_SDL_MIXER_H
#define LGAMES_PORT_SDL_MIXER_H

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIX_DEFAULT_FREQUENCY 22050
#define MIX_DEFAULT_FORMAT 0x8010
#define MIX_DEFAULT_CHANNELS 2
#define MIX_CHANNELS 8
#ifndef AUDIO_S16LSB
#define AUDIO_S16LSB 0x8010
#endif

typedef struct Mix_Chunk {
    int allocated;
    Uint8 *abuf;
    Uint32 alen;
    Uint8 volume;
    Uint8 channels;
    Uint8 bits;
    Uint32 frequency;
    Uint32 data_offset;
    char *path;
} Mix_Chunk;

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
void Mix_CloseAudio(void);
Mix_Chunk *Mix_LoadWAV(const char *path);
void Mix_FreeChunk(Mix_Chunk *chunk);
int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops);
int Mix_GroupAvailable(int tag);
int Mix_Volume(int channel, int volume);
void Mix_Pause(int channel);
int Mix_SetPanning(int channel, Uint8 left, Uint8 right);
int Mix_FadeOutChannel(int channel, int ms);
void rp_audio_service(void);

#ifdef __cplusplus
}
#endif
#endif
