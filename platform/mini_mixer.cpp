
#include "SDL_mixer.h"
#include "rp_platform.h"
#include "hstx.h"
#include "pcm5102_audio.h"

#include "pico/time.h"
#include "pico/mutex.h"

#include <cstdio>
#include <cstring>

namespace {
struct Voice {
    Mix_Chunk *chunk;
    uint64_t position;
    uint32_t step;
    int volume;
    uint8_t left;
    uint8_t right;
};

Voice voices[MIX_CHANNELS]{};
volatile bool opened;
bool system_paused;
bool system_enabled=true;
int master_volume = 128;
mutex_t mixer_lock;
bool mixer_lock_initialized;
uint64_t audio_service_us;
uint32_t audio_service_calls;
uint32_t audio_generated_samples;

uint16_t read16(const uint8_t *p)
{ return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t read32(const uint8_t *p)
{ return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }

int16_t sample(const Mix_Chunk *chunk, uint32_t frame, int side)
{
    unsigned channel = chunk->channels == 1 ? 0 : static_cast<unsigned>(side);
    unsigned bytes = chunk->bits / 8;
    const uint8_t *p = chunk->abuf + (frame * chunk->channels + channel) * bytes;
    if (chunk->bits == 8) return static_cast<int16_t>((static_cast<int>(*p) - 128) << 8);
    return static_cast<int16_t>(read16(p));
}

int clamp16(int value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return value;
}

bool load_samples(Mix_Chunk *chunk)
{
    if (chunk->abuf) return true;
    FILE *fp = std::fopen(chunk->path, "rb");
    uint8_t *data = static_cast<uint8_t *>(rp_psram_malloc(chunk->alen));
    if (!fp || !data || std::fseek(fp, chunk->data_offset, SEEK_SET) ||
        std::fread(data, 1, chunk->alen, fp) != chunk->alen) {
        if (fp) std::fclose(fp);
        if (data) rp_psram_free(data);
        SDL_SetError("WAV playback load failed: %s", chunk->path);
        return false;
    }
    std::fclose(fp);
    chunk->abuf = data;
    return true;
}

void unload_if_unused(Mix_Chunk *chunk)
{
    if (!chunk || !chunk->abuf) return;
    for (const auto &voice : voices) if (voice.chunk == chunk) return;
    rp_psram_free(chunk->abuf);
    chunk->abuf = nullptr;
}

int group_available_unlocked()
{
    for (int i = 0; i < MIX_CHANNELS; ++i) if (!voices[i].chunk) return i;
    return 0;
}
}

extern "C" int Mix_OpenAudio(int, Uint16, int, int)
{
    if (!mixer_lock_initialized) {
        mutex_init(&mixer_lock);
        mixer_lock_initialized = true;
    }
    mutex_enter_blocking(&mixer_lock);
    opened = true;
    std::memset(voices, 0, sizeof(voices));
    for (auto &voice : voices) {
        voice.volume = master_volume;
        voice.left = 255;
        voice.right = 255;
    }
    mutex_exit(&mixer_lock);
    (void)hstx_di_queue_get_underrun_delta();
    pico_hdmi_set_audio_sample_rate(44100);
    (void)pcm5102_audio_init(44100);
    rp_audio_service();

    video_output_set_background_task(nullptr);
    std::printf("HDMI audio mixer: 44100 Hz, %d voices\n", MIX_CHANNELS);
    return 0;
}

extern "C" void Mix_CloseAudio(void)
{
    video_output_set_background_task(nullptr);
    Mix_Pause(-1);
    mutex_enter_blocking(&mixer_lock);
    opened = false;
    std::memset(voices, 0, sizeof(voices));
    mutex_exit(&mixer_lock);
}

extern "C" Mix_Chunk *Mix_LoadWAV(const char *path)
{
    FILE *fp = std::fopen(path, "rb");
    if (!fp) { SDL_SetError("cannot open WAV: %s", path); return nullptr; }
    uint8_t riff[12];
    if (std::fread(riff, 1, sizeof(riff), fp) != sizeof(riff) ||
        std::memcmp(riff, "RIFF", 4) || std::memcmp(riff + 8, "WAVE", 4)) {
        std::fclose(fp); SDL_SetError("invalid WAV: %s", path); return nullptr;
    }
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t frequency = 0, data_size = 0;
    long data_offset = 0;
    uint8_t header[8];
    while (std::fread(header, 1, 8, fp) == 8) {
        uint32_t size = read32(header + 4);
        long body = std::ftell(fp);
        if (!std::memcmp(header, "fmt ", 4) && size >= 16) {
            uint8_t fmt[16];
            if (std::fread(fmt, 1, 16, fp) != 16) break;
            format = read16(fmt); channels = read16(fmt + 2);
            frequency = read32(fmt + 4); bits = read16(fmt + 14);
        } else if (!std::memcmp(header, "data", 4)) {
            data_offset = body; data_size = size;
        }
        if (std::fseek(fp, body + size + (size & 1u), SEEK_SET)) break;
        if (format && data_offset) break;
    }
    if (format != 1 || !data_offset || !data_size ||
        (channels != 1 && channels != 2) || (bits != 8 && bits != 16)) {
        std::fclose(fp); SDL_SetError("unsupported WAV: %s", path); return nullptr;
    }
    Mix_Chunk *chunk = static_cast<Mix_Chunk *>(rp_psram_calloc(1, sizeof(Mix_Chunk)));
    size_t path_size = std::strlen(path) + 1;
    char *saved_path = static_cast<char *>(rp_psram_malloc(path_size));
    if (!chunk || !saved_path) {
        if (saved_path) rp_psram_free(saved_path);
        if (chunk) rp_psram_free(chunk);
        std::fclose(fp); SDL_SetError("WAV load failed: %s", path); return nullptr;
    }
    std::fclose(fp);
    std::memcpy(saved_path, path, path_size);
    chunk->allocated = 1; chunk->abuf = nullptr; chunk->alen = data_size;
    chunk->volume = 128; chunk->channels = channels; chunk->bits = bits;
    chunk->frequency = frequency; chunk->data_offset = static_cast<Uint32>(data_offset);
    chunk->path = saved_path;
    return chunk;
}

extern "C" void Mix_FreeChunk(Mix_Chunk *chunk)
{
    if (!chunk) return;
    mutex_enter_blocking(&mixer_lock);
    for (auto &voice : voices) if (voice.chunk == chunk) voice.chunk = nullptr;
    rp_psram_free(chunk->abuf);
    rp_psram_free(chunk->path);
    rp_psram_free(chunk);
    mutex_exit(&mixer_lock);
}

extern "C" int Mix_GroupAvailable(int)
{
    mutex_enter_blocking(&mixer_lock);
    int channel = group_available_unlocked();
    mutex_exit(&mixer_lock);
    return channel;
}

extern "C" int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int)
{
    if (!opened || !chunk) return -1;
    mutex_enter_blocking(&mixer_lock);
    if (channel < 0 || channel >= MIX_CHANNELS) channel = group_available_unlocked();
    if (!load_samples(chunk)) {
        mutex_exit(&mixer_lock);
        return -1;
    }
    Voice &voice = voices[channel];
    Mix_Chunk *previous = voice.chunk;
    int volume = voice.volume;
    uint8_t left = voice.left;
    uint8_t right = voice.right;
    voice = {chunk, 0, static_cast<uint32_t>((static_cast<uint64_t>(chunk->frequency) << 16) / 44100),
             volume, left, right};
    if (previous != chunk) unload_if_unused(previous);
    mutex_exit(&mixer_lock);
    return channel;
}

extern "C" int Mix_Volume(int channel, int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 128) volume = 128;
    mutex_enter_blocking(&mixer_lock);
    if (channel < 0) {
        int old = master_volume; master_volume = volume;
        for (auto &voice : voices) voice.volume = volume;
        mutex_exit(&mixer_lock);
        return old;
    }
    if (channel >= MIX_CHANNELS) {
        mutex_exit(&mixer_lock);
        return -1;
    }
    int old = voices[channel].volume; voices[channel].volume = volume;
    mutex_exit(&mixer_lock);
    return old;
}

extern "C" void Mix_Pause(int channel)
{
    mutex_enter_blocking(&mixer_lock);
    if (channel < 0) {
        Mix_Chunk *stopped[MIX_CHANNELS];
        for (int i = 0; i < MIX_CHANNELS; ++i) {
            stopped[i] = voices[i].chunk;
            voices[i].chunk = nullptr;
        }
        for (auto *chunk : stopped) unload_if_unused(chunk);
    }
    else if (channel < MIX_CHANNELS) {
        Mix_Chunk *stopped = voices[channel].chunk;
        voices[channel].chunk = nullptr;
        unload_if_unused(stopped);
    }
    mutex_exit(&mixer_lock);
}

extern "C" int Mix_SetPanning(int channel, Uint8 left, Uint8 right)
{
    if (channel < 0 || channel >= MIX_CHANNELS) return 0;
    mutex_enter_blocking(&mixer_lock);
    voices[channel].left = left; voices[channel].right = right;
    mutex_exit(&mixer_lock);
    return 1;
}

extern "C" int Mix_FadeOutChannel(int channel, int)
{ Mix_Pause(channel); return 1; }

extern "C" void rp_audio_service(void)
{
    uint32_t started_us = time_us_32();
    ++audio_service_calls;
    if (!opened) {
        audio_service_us += time_us_32() - started_us;
        return;
    }

    if (!mutex_try_enter(&mixer_lock, nullptr)) {
        audio_service_us += time_us_32() - started_us;
        return;
    }
    if (!opened) {
        mutex_exit(&mixer_lock);
        audio_service_us += time_us_32() - started_us;
        return;
    }
    int generated = 0;
    while (generated < 512 &&
           hstx_di_queue_get_level() < HSTX_AUDIO_DI_HIGH_WATERMARK &&
           pcm5102_audio_free() > 0) {
        int left = 0, right = 0;
        for (auto &voice : voices) if (voice.chunk && !system_paused) {
            Mix_Chunk *chunk = voice.chunk;
            uint32_t bytes_per_frame = chunk->channels * (chunk->bits / 8);
            uint32_t frames = chunk->alen / bytes_per_frame;
            uint32_t frame = static_cast<uint32_t>(voice.position >> 16);
            if (frame >= frames) {
                voice.chunk = nullptr;
                unload_if_unused(chunk);
                continue;
            }
            left += sample(chunk, frame, 0) * voice.volume * voice.left / (128 * 255);
            right += sample(chunk, frame, 1) * voice.volume * voice.right / (128 * 255);
            voice.position += voice.step;
        }
        int16_t left16 = system_enabled ? clamp16(left) : 0;
        int16_t right16 = system_enabled ? clamp16(right) : 0;
        hstx_push_audio_sample(left16, right16);
        pcm5102_audio_push(left16, right16);
        ++generated;
    }
    mutex_exit(&mixer_lock);
    audio_generated_samples += generated;
    audio_service_us += time_us_32() - started_us;
}

extern "C" void rp_audio_system_pause(int paused)
{

    system_paused=paused!=0;
}

extern "C" void rp_audio_system_enable(int enabled)
{
    system_enabled=enabled!=0;
}

extern "C" void rp_audio_take_perf(uint64_t *service_us, uint32_t *calls,
                                     uint32_t *samples)
{
    if (service_us) *service_us = audio_service_us;
    if (calls) *calls = audio_service_calls;
    if (samples) *samples = audio_generated_samples;
    audio_service_us = 0;
    audio_service_calls = 0;
    audio_generated_samples = 0;
}

extern "C" void rp_audio_get_health(uint32_t *hdmi_level,
                                      uint32_t *hdmi_underruns,
                                      uint32_t *pcm_level,
                                      uint32_t *pcm_starvations)
{
    if (hdmi_level) *hdmi_level = hstx_di_queue_get_level();
    if (hdmi_underruns) *hdmi_underruns = hstx_di_queue_get_underrun_count();
    if (pcm_level) *pcm_level = pcm5102_audio_level();
    if (pcm_starvations)
        *pcm_starvations = pcm5102_audio_get_starvation_count();
}
