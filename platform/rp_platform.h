
#ifndef LGAMES_PORT_PLATFORM_H
#define LGAMES_PORT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rp_platform_init(const char *asset_dir, const char *program_name);
void rp_platform_present(const uint16_t *rgb565, int width, int height, int pitch_pixels);
typedef struct rp_platform_rect {
    int16_t x, y;
    uint16_t w, h;
} rp_platform_rect;
void rp_platform_present_rects(const uint16_t *rgb565, int width, int height,
                               int pitch_pixels, int numrects,
                               const rp_platform_rect *rects);
void rp_platform_present_rect_atomic(const uint16_t *rgb565, int width,
                                     int height, int pitch_pixels,
                                     const rp_platform_rect *rect,
                                     uint8_t *staging, size_t staging_bytes);
uint16_t rp_platform_read_pad(int index);
void rp_platform_service(void);
void rp_system_menu_init(const char *program_name);
void rp_system_menu_load_preferences(void);
void rp_platform_bootsel(void);
void rp_system_menu_sample(uint16_t pad);
uint32_t rp_system_menu_poll(void);
int rp_system_game_list(void);
void rp_system_launch(int game);
void rp_platform_launch_slot(int game);
void rp_system_dispatch_early(void);
void rp_system_game_finished(void);
void rp_audio_system_pause(int paused);
void rp_audio_system_enable(int enabled);
void rp_platform_loading(const char *stage);
void rp_usb_init(void);
void rp_usb_task(void);
void rp_audio_service(void);
void rp_audio_get_health(uint32_t *hdmi_level, uint32_t *hdmi_underruns,
                         uint32_t *pcm_level, uint32_t *pcm_starvations);
void rp_audio_take_perf(uint64_t *service_us, uint32_t *calls,
                        uint32_t *samples);
void rp_sdl_take_perf(uint64_t *blit_us, uint64_t *fill_us,
                      uint64_t *convert_us, uint32_t *blit_calls);
uint16_t rp_usb_pad(int index);
uint32_t rp_usb_identity(void);
void *rp_psram_malloc(size_t size);
void *rp_psram_calloc(size_t count, size_t size);
void *rp_psram_realloc(void *ptr, size_t size);
void rp_psram_free(void *ptr);
size_t rp_psram_available(void);
const char *rp_getenv(const char *name);
void rp_exit(int status) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
#endif
