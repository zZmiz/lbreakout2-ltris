
#include "rp_platform.h"

#include "PicoPlusPsram.h"
#include "hstx.h"
#include "nespad.h"

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/psram.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include <cstdio>
#include <cstring>

extern "C" int rp_fatfs_init(const char *asset_dir);

namespace {
PicoPlusPsram *psram = nullptr;
uint16_t cached_pads[2]{};
uint32_t last_pad_us = 0;
bool pace_initialized = false;

struct PerfWindow {
    uint32_t started_us;
    uint32_t last_present_start_us;
    uint32_t last_present_end_us;
    uint64_t active_us;
    uint64_t pace_wait_us;
    uint64_t between_present_us;
    uint64_t present_copy_us;
    uint64_t present_pixels;
    uint32_t max_frame_us;
    uint32_t max_between_us;
    uint32_t max_present_us;
    uint32_t frames;
    uint32_t over_budget;
    size_t min_psram_free;
    uint32_t last_hdmi_underruns;
    uint32_t last_pcm_starvations;
};

PerfWindow perf{};

constexpr uint32_t kFlashQuad6bClkdiv = 6;
constexpr uint32_t kFlashQuad6bMinDeselect = 8;

int __no_inline_not_in_flash_func(enable_flash_quad_6b)()
{
    constexpr uint32_t off = 0x1000;
    uint8_t gtx[4 + 16] = {0x03, static_cast<uint8_t>(off >> 16),
                           static_cast<uint8_t>(off >> 8), static_cast<uint8_t>(off)};
    uint8_t grx[4 + 16] = {};
    uint32_t irq = save_and_disable_interrupts();
    flash_do_cmd(gtx, grx, sizeof(gtx));

    uint8_t read_sr2[2] = {0x35, 0};
    uint8_t sr2_value[2] = {};
    flash_do_cmd(read_sr2, sr2_value, sizeof(read_sr2));
    if (!(sr2_value[1] & 0x02)) {
        uint8_t write_enable[1] = {0x06};
        flash_do_cmd(write_enable, write_enable, sizeof(write_enable));
        uint8_t write_qe[2] = {0x31, 0x02};
        uint8_t write_reply[2] = {};
        flash_do_cmd(write_qe, write_reply, sizeof(write_qe));
        uint8_t status_tx[2];
        uint8_t status_rx[2];
        do {
            status_tx[0] = 0x05;
            status_tx[1] = 0;
            flash_do_cmd(status_tx, status_rx, sizeof(status_tx));
        } while (status_rx[1] & 0x01);
    }

    const uint32_t saved_timing = qmi_hw->m[0].timing;
    const uint32_t saved_command = qmi_hw->m[0].rcmd;
    const uint32_t saved_format = qmi_hw->m[0].rfmt;
    const uint32_t format_6b =
        (static_cast<uint32_t>(QMI_M0_RFMT_PREFIX_WIDTH_VALUE_S) << QMI_M0_RFMT_PREFIX_WIDTH_LSB) |
        (static_cast<uint32_t>(QMI_M0_RFMT_ADDR_WIDTH_VALUE_S) << QMI_M0_RFMT_ADDR_WIDTH_LSB) |
        (static_cast<uint32_t>(QMI_M0_RFMT_DUMMY_WIDTH_VALUE_S) << QMI_M0_RFMT_DUMMY_WIDTH_LSB) |
        (static_cast<uint32_t>(QMI_M0_RFMT_DATA_WIDTH_VALUE_Q) << QMI_M0_RFMT_DATA_WIDTH_LSB) |
        (static_cast<uint32_t>(QMI_M0_RFMT_PREFIX_LEN_VALUE_8) << QMI_M0_RFMT_PREFIX_LEN_LSB) |
        (static_cast<uint32_t>(QMI_M0_RFMT_SUFFIX_LEN_VALUE_NONE) << QMI_M0_RFMT_SUFFIX_LEN_LSB) |
        (2u << QMI_M0_RFMT_DUMMY_LEN_LSB);

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const uint32_t target_hz = 252000000u / kFlashQuad6bClkdiv;
    uint32_t clkdiv = (sys_hz + target_hz - 1u) / target_hz;
    if (clkdiv < 1u) clkdiv = 1u;

    int good_rxdelay = 0;
    for (uint8_t rx = 1; rx <= 4 && !good_rxdelay; ++rx) {
        qmi_hw->m[0].timing =
            (kFlashQuad6bMinDeselect << QMI_M0_TIMING_MIN_DESELECT_LSB) |
            (static_cast<uint32_t>(rx) << QMI_M0_TIMING_RXDELAY_LSB) |
            (clkdiv << QMI_M0_TIMING_CLKDIV_LSB);
        qmi_hw->m[0].rcmd = 0x6bu << QMI_M0_RCMD_PREFIX_LSB;
        qmi_hw->m[0].rfmt = format_6b;
        __compiler_memory_barrier();

        volatile const uint8_t *src = reinterpret_cast<volatile const uint8_t *>(0x14000000u + off);
        (void)src[0];
        bool valid = true;
        for (int i = 0; i < 16; ++i) {
            if (src[i] != grx[4 + i]) {
                valid = false;
                break;
            }
        }
        if (valid) good_rxdelay = rx;
    }

    if (!good_rxdelay) {
        qmi_hw->m[0].timing = saved_timing;
        qmi_hw->m[0].rcmd = saved_command;
        qmi_hw->m[0].rfmt = saved_format;
        __compiler_memory_barrier();
    }
    restore_interrupts(irq);
    return good_rxdelay;
}

void init_clocks()
{
    constexpr uint32_t sys_khz = 252000;
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(sys_khz, true);
    const uint32_t sys_hz = sys_khz * 1000u;
    clock_configure(clk_hstx, 0,
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                    sys_hz, 126000000u);
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    sys_hz, sys_hz);
    stdio_init_all();
    sleep_ms(30);
}

void sample_pads()
{
    rp_usb_task();
    uint32_t now = time_us_32();
    if (now - last_pad_us < 1000) return;
    last_pad_us = now;
    nespad_read_start();
    busy_wait_us_32(250);
    nespad_read_finish();
    cached_pads[0] = nespad_states_ext[0] | rp_usb_pad(0);
    cached_pads[1] = nespad_states_ext[1] | rp_usb_pad(1);

    rp_system_menu_sample(((cached_pads[0]&0x0c)==0x0c ||
                           (cached_pads[1]&0x0c)==0x0c) ? 0x0c : 0);
}
}

extern "C" int rp_platform_init(const char *asset_dir, const char *program_name)
{
    init_clocks();
    rp_system_menu_init(program_name);
    std::printf("\n=== %s / LGames Port v2.1 ===\n", program_name);
    std::printf("CPU: %lu kHz, HSTX: %lu kHz\n",
                static_cast<unsigned long>(clock_get_hz(clk_sys) / 1000),
                static_cast<unsigned long>(clock_get_hz(clk_hstx) / 1000));

    int psram_params = psram_set_params(4, 4, 31, 3);
    int psram_reinit = psram_reinitialize();
    std::printf("PSRAM timing: params=%d reinit=%d\n", psram_params, psram_reinit);
    int flash_rxdelay = enable_flash_quad_6b();
    qmi_hw->m[1].timing &= ~QMI_M1_TIMING_COOLDOWN_BITS;
    __compiler_memory_barrier();
    std::printf("QMI coexistence: flash 0x6b rxdelay=%d, PSRAM cooldown=0\n", flash_rxdelay);
    psram = &PicoPlusPsram::getInstance();
    if (psram->GetMemorySize() == 0) {
        std::printf("FATAL: PSRAM not detected on GPIO47\n");
        return 0;
    }
    std::printf("PSRAM: %lu KiB\n", static_cast<unsigned long>(psram->GetMemorySize() / 1024));

    hstx_init(false);
    std::printf("HDMI ready: HSTX=%lu kHz (expected 126000)\n",
                static_cast<unsigned long>(clock_get_hz(clk_hstx) / 1000));
    hstx_setScanLines(0);
    rp_platform_loading("SD mount");

    nespad_begin(0, 252000, 6, 8, 7, pio1);
    nespad_begin(1, 252000, 9, 11, 10, pio1);
    rp_usb_init();

    if (!rp_fatfs_init(asset_dir)) {
        std::printf("FATAL: cannot mount/open %s\n", asset_dir);
        return 0;
    }
    std::printf("SD data: %s\n", asset_dir);
    rp_system_menu_load_preferences();
    return 1;
}

extern "C" void rp_platform_bootsel(void)
{
    reset_usb_boot(0,0);
}

namespace {
uint32_t present_begin()
{
    uint32_t present_start_us = time_us_32();
    if (!perf.started_us) {
        perf.started_us = present_start_us;
        perf.min_psram_free = rp_psram_available();
    }
    if (perf.last_present_start_us) {
        uint32_t frame_us = present_start_us - perf.last_present_start_us;
        if (frame_us > perf.max_frame_us) perf.max_frame_us = frame_us;
        if (frame_us > 16667u) ++perf.over_budget;
    }
    if (perf.last_present_end_us) {
        uint32_t between_us = present_start_us - perf.last_present_end_us;
        perf.active_us += between_us;
        perf.between_present_us += between_us;
        if (between_us > perf.max_between_us) perf.max_between_us = between_us;
    }
    perf.last_present_start_us = present_start_us;
    return present_start_us;
}

void present_finish(uint32_t present_start_us)
{
    if (!pace_initialized) {
        hstx_paceFrame(true);
        pace_initialized = true;
    }
    uint32_t pace_start_us = time_us_32();
    uint32_t present_us = pace_start_us - present_start_us;
    perf.active_us += present_us;
    perf.present_copy_us += present_us;
    if (present_us > perf.max_present_us) perf.max_present_us = present_us;
    hstx_paceFrame(false);
    uint32_t present_end_us = time_us_32();
    perf.pace_wait_us += present_end_us - pace_start_us;
    perf.last_present_end_us = present_end_us;
    perf.frames++;

    size_t psram_free = rp_psram_available();
    if (psram_free < perf.min_psram_free) perf.min_psram_free = psram_free;
    uint32_t elapsed_us = present_end_us - perf.started_us;
    if (elapsed_us >= 1000000u || perf.frames >= 60) {
        uint64_t measured_us = perf.active_us + perf.pace_wait_us;
        uint32_t hdmi_level = 0, hdmi_underruns = 0;
        uint32_t pcm_level = 0, pcm_starvations = 0;
        uint64_t audio_service_us = 0, sdl_blit_us = 0;
        uint64_t sdl_fill_us = 0, sdl_convert_us = 0;
        uint32_t audio_calls = 0, audio_samples = 0, sdl_blit_calls = 0;
        rp_audio_get_health(&hdmi_level, &hdmi_underruns,
                            &pcm_level, &pcm_starvations);
        rp_audio_take_perf(&audio_service_us, &audio_calls, &audio_samples);
        rp_sdl_take_perf(&sdl_blit_us, &sdl_fill_us, &sdl_convert_us,
                         &sdl_blit_calls);
        unsigned long fps_x100 = elapsed_us
            ? static_cast<unsigned long>(perf.frames * 100000000ull / elapsed_us) : 0;
        unsigned long frame_avg_x100 = perf.frames
            ? static_cast<unsigned long>(measured_us / (perf.frames * 10ull)) : 0;
        unsigned long busy_x10 = measured_us
            ? static_cast<unsigned long>(perf.active_us * 1000ull / measured_us) : 0;
        std::printf("[perf] fps=%lu.%02lu elapsed_ms=%lu frames=%lu over_16_67ms=%lu frame_avg=%lu.%02lums frame_max=%lu.%03lums "
                    "cpu_active=%lu.%lu%% psram_free=%lu min=%lu "
                    "audio_hdmi_level=%lu underrun=%lu(+%lu) "
                    "audio_pcm_level=%lu starvation=%lu(+%lu)\n",
                    fps_x100 / 100, fps_x100 % 100,
                    static_cast<unsigned long>(elapsed_us/1000),
                    static_cast<unsigned long>(perf.frames),
                    static_cast<unsigned long>(perf.over_budget),
                    frame_avg_x100 / 100, frame_avg_x100 % 100,
                    static_cast<unsigned long>(perf.max_frame_us / 1000),
                    static_cast<unsigned long>(perf.max_frame_us % 1000),
                    busy_x10 / 10, busy_x10 % 10,
                    static_cast<unsigned long>(psram_free),
                    static_cast<unsigned long>(perf.min_psram_free),
                    static_cast<unsigned long>(hdmi_level),
                    static_cast<unsigned long>(hdmi_underruns),
                    static_cast<unsigned long>(hdmi_underruns - perf.last_hdmi_underruns),
                    static_cast<unsigned long>(pcm_level),
                    static_cast<unsigned long>(pcm_starvations),
                    static_cast<unsigned long>(pcm_starvations - perf.last_pcm_starvations));
        unsigned long between_avg_x100 = perf.frames
            ? static_cast<unsigned long>(perf.between_present_us / (perf.frames * 10ull)) : 0;
        unsigned long present_avg_x100 = perf.frames
            ? static_cast<unsigned long>(perf.present_copy_us / (perf.frames * 10ull)) : 0;
        unsigned long blit_avg_x100 = perf.frames
            ? static_cast<unsigned long>(sdl_blit_us / (perf.frames * 10ull)) : 0;
        unsigned long fill_avg_x100 = perf.frames
            ? static_cast<unsigned long>(sdl_fill_us / (perf.frames * 10ull)) : 0;
        std::printf("[perf_detail] between=%lu.%02lums(max=%lu.%03lu) "
                    "present=%lu.%02lums(max=%lu.%03lu) "
                    "pixels=%lu "
                    "blit=%lu.%02lums/%lu calls fill=%lu.%02lums "
                    "convert_total=%lums audio_total=%lums/%lu calls/%lu samples\n",
                    between_avg_x100 / 100, between_avg_x100 % 100,
                    static_cast<unsigned long>(perf.max_between_us / 1000),
                    static_cast<unsigned long>(perf.max_between_us % 1000),
                    present_avg_x100 / 100, present_avg_x100 % 100,
                    static_cast<unsigned long>(perf.max_present_us / 1000),
                    static_cast<unsigned long>(perf.max_present_us % 1000),
                    static_cast<unsigned long>(perf.present_pixels),
                    blit_avg_x100 / 100, blit_avg_x100 % 100,
                    static_cast<unsigned long>(sdl_blit_calls),
                    fill_avg_x100 / 100, fill_avg_x100 % 100,
                    static_cast<unsigned long>(sdl_convert_us / 1000),
                    static_cast<unsigned long>(audio_service_us / 1000),
                    static_cast<unsigned long>(audio_calls),
                    static_cast<unsigned long>(audio_samples));
        perf.started_us = present_end_us;
        perf.active_us = perf.pace_wait_us = 0;
        perf.between_present_us = perf.present_copy_us = 0;
        perf.present_pixels = 0;
        perf.max_frame_us = 0;
        perf.max_between_us = perf.max_present_us = 0;
        perf.frames = 0;
        perf.over_budget = 0;
        perf.min_psram_free = psram_free;
        perf.last_hdmi_underruns = hdmi_underruns;
        perf.last_pcm_starvations = pcm_starvations;
    }
}
}

extern "C" void rp_platform_present_rects(const uint16_t *rgb565, int width,
                                             int height, int pitch_pixels,
                                             int numrects,
                                             const rp_platform_rect *rects)
{
    if (!rgb565 || width <= 0 || height <= 0 || numrects < 0 ||
        (numrects && !rects)) return;

    uint32_t present_start_us = present_begin();
    auto *dst = hstx_getframebuffer();

    if (width == 640 && height == 480 && numrects > 0) {
        for (int i = 0; i < numrects; ++i) {
            int x0 = rects[i].x;
            int y0 = rects[i].y;
            int x1 = x0 + rects[i].w;
            int y1 = y0 + rects[i].h;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > 640) x1 = 640;
            if (y1 > 480) y1 = 480;
            if (x0 >= x1 || y0 >= y1) continue;
            perf.present_pixels += static_cast<uint64_t>(x1 - x0) * (y1 - y0);
            for (int y = y0; y < y1; ++y) {
                const uint16_t *src = rgb565 + y * pitch_pixels + x0;
                uint8_t *out = dst + y * 640 + x0;
                for (int x = x0; x < x1; ++x) {
                    uint16_t p = *src++;
                    *out++ = static_cast<uint8_t>(((p >> 8) & 0xe0u) |
                                                  ((p >> 6) & 0x1cu) |
                                                  ((p >> 3) & 3u));
                }
                if ((y & 15) == 0) rp_platform_service();
            }
        }
    } else if (numrects != 0) {
        perf.present_pixels += 640u * 480u;
        for (int y = 0; y < 480; ++y) {
            int sy = (y * height) / 480;
            const uint16_t *src = rgb565 + sy * pitch_pixels;
            uint8_t *out = dst + y * 640;
            for (int x = 0; x < 640; ++x) {
                uint16_t p = src[width == 640 ? x : (x * width) / 640];
                out[x] = static_cast<uint8_t>(((p >> 8) & 0xe0u) |
                                             ((p >> 6) & 0x1cu) |
                                             ((p >> 3) & 3u));
            }
            if ((y & 31) == 0) rp_platform_service();
        }
    }
    present_finish(present_start_us);
}

extern "C" void rp_platform_present(const uint16_t *rgb565, int width,
                                      int height, int pitch_pixels)
{
    const rp_platform_rect full = {0, 0, static_cast<uint16_t>(width),
                                    static_cast<uint16_t>(height)};
    rp_platform_present_rects(rgb565, width, height, pitch_pixels, 1, &full);
}

extern "C" void rp_platform_present_rect_atomic(
    const uint16_t *rgb565, int width, int height, int pitch_pixels,
    const rp_platform_rect *rect, uint8_t *staging, size_t staging_bytes)
{
    if (!rgb565 || !rect || !staging || width != 640 || height != 480) return;
    int x0 = rect->x;
    int y0 = rect->y;
    int x1 = x0 + rect->w;
    int y1 = y0 + rect->h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    if (x0 >= x1 || y0 >= y1) return;

    const size_t row_bytes = static_cast<size_t>(x1 - x0);
    const size_t needed = row_bytes * static_cast<size_t>(y1 - y0);
    if (needed > staging_bytes) {
        rp_platform_present_rects(rgb565, width, height, pitch_pixels,
                                  1, rect);
        return;
    }

    uint8_t *staged = staging;
    for (int y = y0; y < y1; ++y) {
        const uint16_t *src = rgb565 + y * pitch_pixels + x0;
        for (int x = x0; x < x1; ++x) {
            uint16_t p = *src++;
            *staged++ = static_cast<uint8_t>(((p >> 8) & 0xe0u) |
                                             ((p >> 6) & 0x1cu) |
                                             ((p >> 3) & 3u));
        }
        if ((y & 15) == 0) rp_platform_service();
    }

    hstx_waitForVSync();
    uint8_t *dst = hstx_getframebuffer() + y0 * width + x0;
    staged = staging;
    for (int y = y0; y < y1; ++y) {
        std::memcpy(dst, staged, row_bytes);
        dst += width;
        staged += row_bytes;
    }
    __dmb();
}

extern "C" void rp_platform_service(void)
{
    sample_pads();
    rp_audio_service();
}

extern "C" void rp_platform_loading(const char *stage)
{
    std::printf("[load %lu ms] %s\n", static_cast<unsigned long>(to_ms_since_boot(get_absolute_time())), stage);
    uint8_t *fb = hstx_getframebuffer();
    std::memset(fb, 0, 640 * 480);

    static const uint8_t letters[7][7] = {
        {16,16,16,16,16,16,31}, {14,17,17,17,17,17,14},
        {14,17,17,31,17,17,17}, {30,17,17,17,17,17,30},
        {31,4,4,4,4,4,31}, {17,25,25,21,19,19,17}, {14,17,16,23,17,17,14}
    };
    for (int c = 0; c < 7; ++c) for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 5; ++x) if (letters[c][y] & (16 >> x))
            for (int yy = 0; yy < 3; ++yy) for (int xx = 0; xx < 3; ++xx)
                fb[(228 + y * 3 + yy) * 640 + 258 + c * 18 + x * 3 + xx] = 0xff;
}

extern "C" uint16_t rp_platform_read_pad(int index)
{
    sample_pads();
    return index >= 0 && index < 2 ? cached_pads[index] : 0;
}

extern "C" void *rp_psram_malloc(size_t size)
{ return psram && size ? psram->Malloc(size) : nullptr; }
extern "C" void *rp_psram_calloc(size_t count, size_t size)
{ return psram && count && size ? psram->Calloc(count, size) : nullptr; }
extern "C" void *rp_psram_realloc(void *ptr, size_t size)
{ return psram ? psram->Realloc(ptr, size) : nullptr; }
extern "C" void rp_psram_free(void *ptr)
{ if (psram && ptr) psram->Free(ptr); }
extern "C" size_t rp_psram_available(void)
{ return psram ? psram->GetAvailableBytes() : 0; }

extern "C" const char *rp_getenv(const char *name)
{
    return name && std::strcmp(name, "HOME") == 0 ? "." : nullptr;
}

extern "C" void rp_exit(int status)
{
    std::printf("Application exit (%d); rebooting to launcher\n", status);
    sleep_ms(25);
    watchdog_hw->scratch[7] = 0xB007BACEu;
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}

extern "C" void _exit(int status) { rp_exit(status); }
