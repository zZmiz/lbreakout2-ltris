
#include "rp_platform.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <cstddef>
#ifdef RP_HOST_SRAM_BUDGET
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
#endif

static auto start = std::chrono::steady_clock::now();
static size_t used, peak;
static unsigned frames;
static unsigned startup_audited_frames, startup_black_frames;
struct alignas(std::max_align_t) Block {
    size_t size;
    size_t id;
    Block *prev;
    Block *next;
};
static Block *blocks;
static size_t next_block_id;
extern "C" uint64_t get_absolute_time() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
}
extern "C" uint32_t to_ms_since_boot(uint64_t t) { return t; }
extern "C" uint32_t time_us_32() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-start).count());
}
extern "C" void sleep_ms(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
extern "C" void rp_platform_service() {}
extern "C" uint32_t rp_system_menu_poll() { return 0; }
extern "C" uint16_t rp_platform_read_pad(int index) {
    if (index || !std::getenv("RP_HOST_PLAY")) return 0;
    uint64_t t=get_absolute_time();
#ifdef RP_GAME_LTRIS
    const char *test_case = std::getenv("RP_HOST_CASE");
    if (test_case && std::strcmp(test_case, "cancel") == 0) {
        if (t>=3400 && t<3480) return 1u<<2;
        if (t>=3700 && t<3780) return 1u<<1;
    }
    if (test_case && std::strcmp(test_case, "yes") == 0) {
        if (t>=3400 && t<3480) return 1u<<2;
        if (t>=3700 && t<3780) return 1u<<9;
    }
#endif
#ifdef RP_GAME_LBREAKOUT2
    const char *test_case = std::getenv("RP_HOST_CASE");
    if (test_case && std::strcmp(test_case, "addon_scroll") == 0) {

        if ((t>=800 && t<880) || (t>=1400 && t<1480) ||
            (t>=1650 && t<1730) || (t>=1800 && t<1880)) return 1u<<5;
        if ((t>=1100 && t<1180) || (t>=1950 && t<2030)) return 1u<<3;
        if (t >= 2250 && t < 6250) {
            uint64_t phase = (t - 2250) % 200;
            if (phase < 80) return 1u<<5;
        }
        if (t >= 6500) {
            std::printf("ADDON SCROLL PASS: navigation-only used=%zu free=%zu peak=%zu\n",
                        used, 8*1024*1024-used, peak);
            std::fflush(stdout);
            std::_Exit(0);
        }
        return 0;
    }
    if (test_case && (std::strcmp(test_case, "freakout") == 0 ||
                      std::strcmp(test_case, "invaders") == 0)) {
        if ((t>=800 && t<880) || (t>=1400 && t<1480) ||
            (t>=1650 && t<1730) || (t>=1800 && t<1880)) return 1u<<5;
        if ((t>=1100 && t<1180) || (t>=1950 && t<2030)) return 1u<<3;
        if (std::strcmp(test_case, "freakout") == 0) {
            if (t>=2250 && t<2330) return 1u<<5;
            if ((t>=2600 && t<2700) || (t>=3500 && t<3600)) return 1u<<0;
            if (t>=2900 && t<3300) return 1u<<7;
        } else {
            if ((t>=2200 && t<2280) || (t>=2450 && t<2530) ||
                (t>=2700 && t<2780) || (t>=2950 && t<3030) ||
                (t>=3200 && t<3280) || (t>=3450 && t<3530) ||
                (t>=3700 && t<3780)) return 1u<<5;
            if ((t>=4050 && t<4150) || (t>=4850 && t<4950)) return 1u<<0;
            if (t>=5150 && t<5550) return 1u<<7;
        }
        return 0;
    }
    if (test_case && std::strcmp(test_case, "help") == 0) {
        if ((t>=800 && t<880) || (t>=1000 && t<1080)) return 1u<<5;
        if (t>=1300 && t<1380) return 1u<<0;
        if (t>=2000 && t<2080) return 1u<<1;
        return 0;
    }
    if (test_case && std::strcmp(test_case, "addon") == 0 && t>=1800 && t<1880)
        return 1u<<5;
    if (test_case && std::strcmp(test_case, "cancel") == 0) {
        if (t>=3800 && t<3880) return 1u<<2;
        if (t>=4100 && t<4180) return 1u<<1;
    }
#endif
    if ((t>=800 && t<880) || (t>=1400 && t<1480)) return 1u<<5;
#ifdef RP_GAME_LBREAKOUT2
    if (t>=1650 && t<1730) return 1u<<5;
#endif
    if ((t>=1100 && t<1180) || (t>=1950 && t<2030)) return 1u<<3;
    if ((t>=2600 && t<2700) || (t>=3500 && t<3600)) return 1u<<0;
    if (t>=2900 && t<3300) return 1u<<7;
    return 0;
}
extern "C" void *rp_psram_malloc(size_t n) {
    if (used+n > 8*1024*1024) { std::printf("PSRAM OOM: request=%zu used=%zu\n",n,used); return nullptr; }
    auto *b = static_cast<Block *>(
#ifdef RP_HOST_SRAM_BUDGET
        __real_malloc(sizeof(Block)+n)
#else
        std::malloc(sizeof(Block)+n)
#endif
    );
    if (!b) return nullptr;
    b->size=n; b->id=++next_block_id; b->prev=nullptr; b->next=blocks;
    if (blocks) blocks->prev=b;
    blocks=b; used+=n; if (used>peak) peak=used;
    return b+1;
}
extern "C" void rp_psram_free(void *p) {
    if (!p) return;
    auto *b=static_cast<Block *>(p)-1;
    if (b->prev) b->prev->next=b->next; else blocks=b->next;
    if (b->next) b->next->prev=b->prev;
    used-=b->size;
#ifdef RP_HOST_SRAM_BUDGET
    __real_free(b);
#else
    std::free(b);
#endif
}
extern "C" void *rp_psram_calloc(size_t n,size_t m) { void *p=rp_psram_malloc(n*m); if(p) std::memset(p,0,n*m); return p; }
extern "C" void *rp_psram_realloc(void *p,size_t n) {
    if(!p) return rp_psram_malloc(n);
    void *q=rp_psram_malloc(n); if(!q) return nullptr;
    size_t old=(static_cast<Block *>(p)-1)->size;
    std::memcpy(q,p,old<n?old:n); rp_psram_free(p); return q;
}
extern "C" size_t rp_psram_available() { return 8*1024*1024-used; }
extern "C" size_t rp_psram_debug_mark() { return next_block_id; }
extern "C" void rp_psram_debug_dump_since(size_t mark) {
    for (Block *b=blocks; b; b=b->next)
        if (b->id>mark) std::printf("PSRAM LEAK: id=%zu size=%zu\n",b->id,b->size);
}
extern "C" void rp_platform_present(const uint16_t *pixels,int w,int h,int pitch) {
    ++frames;

    if (std::getenv("RP_HOST_STARTUP_AUDIT") && get_absolute_time() >= 1950) {
        bool any_visible = false;
        for (int y=0; y<h && !any_visible; ++y)
            for (int x=0; x<w; ++x)
                if (pixels[y*pitch+x] != 0) { any_visible = true; break; }
        ++startup_audited_frames;
        if (!any_visible) ++startup_black_frames;
    }
    unsigned capture_ms = std::getenv("RP_HOST_PLAY") ? 4500 : 1500;
    if (const char *value = std::getenv("RP_HOST_CAPTURE_MS"))
        capture_ms = static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    if (frames >= 120 && get_absolute_time() >= capture_ms) {
        FILE *f=std::fopen("frame.ppm","wb");
        std::fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            unsigned p=pixels[y*pitch+x];
            uint8_t rgb[3]={static_cast<uint8_t>((p>>11)*255/31),static_cast<uint8_t>(((p>>5)&63)*255/63),static_cast<uint8_t>((p&31)*255/31)};
            std::fwrite(rgb,1,3,f);
        }
        std::fclose(f);
        if (std::getenv("RP_HOST_STARTUP_AUDIT")) {
            std::printf("STARTUP PIXEL AUDIT: submitted=%u black=%u\n",
                        startup_audited_frames,startup_black_frames);
            if (!startup_audited_frames || startup_black_frames) {
                std::fflush(stdout); std::_Exit(2);
            }
        }
        std::printf("HOST SMOKE PASS: frames=%u PSRAM peak=%zu\n",frames,peak);
        std::fflush(stdout); std::_Exit(0);
    }
}
extern "C" void rp_platform_present_rects(const uint16_t *pixels,int w,int h,int pitch,
                                            int numrects,const rp_platform_rect *rects) {
    (void)numrects; (void)rects;
    rp_platform_present(pixels,w,h,pitch);
}
extern "C" void rp_platform_present_rect_atomic(const uint16_t *pixels,int w,int h,int pitch,
                                                   const rp_platform_rect *rect,
                                                   uint8_t *staging,size_t staging_bytes) {
    (void)rect; (void)staging; (void)staging_bytes;
    rp_platform_present(pixels,w,h,pitch);
}
