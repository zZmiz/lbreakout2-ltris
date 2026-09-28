
#include "SDL.h"
#include "rp_platform.h"

#include "pico/stdlib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(RP_GAME_LTRIS) && defined(PICO_ON_DEVICE)
#include <malloc.h>
extern "C" char __end__, __HeapLimit;
#endif

namespace {
char error_text[160] = "no error";
SDL_Surface *video_surface = nullptr;
SDL_PixelFormat video_format{};
SDL_VideoInfo video_info{};
SDL_EventFilter event_filter = nullptr;
Uint8 key_state[SDLK_LAST]{};
Uint8 event_states[32]{};
SDL_Cursor default_cursor{};
SDL_Cursor *current_cursor = &default_cursor;
int unicode_enabled = 0;

constexpr int EVENT_QUEUE_SIZE = 32;
SDL_Event event_queue[EVENT_QUEUE_SIZE]{};
unsigned event_head = 0;
unsigned event_tail = 0;
uint16_t old_pad[2]{};
bool start_consumed[2]{};
uint32_t last_pad_poll = 0;
uint32_t last_render_service_ms = 0;
uint32_t system_paused_ms = 0;
uint64_t sdl_blit_us = 0;
uint64_t sdl_fill_us = 0;
uint64_t sdl_convert_us = 0;
uint32_t sdl_blit_calls = 0;
#if defined(RP_GAME_LTRIS)
constexpr size_t ATOMIC_HINT_STAGING_BYTES = 64u * 1024u;
alignas(4) Uint8 atomic_hint_staging[ATOMIC_HINT_STAGING_BYTES];

alignas(4) char background_file_buffer[4096];
struct BackgroundCacheEntry {
    char path[600];
    Uint8 *pixels;
    size_t loaded;
    uint32_t steps, max_step_us;
    bool ready, failed;
};
struct BackgroundCache {
    BackgroundCacheEntry entries[4];
    FILE *file;
    int next, width, height;
    size_t bytes;
};

BackgroundCache *background_cache=nullptr;
constexpr size_t BACKGROUND_CACHE_RESERVE=2u*1024u*1024u;
#endif

struct ScopedPerf {
    uint32_t started;
    uint64_t *total;
    explicit ScopedPerf(uint64_t *counter) : started(time_us_32()), total(counter) {}
    ~ScopedPerf() { *total += time_us_32() - started; }
};

void pump_input();

void service_if_due()
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_render_service_ms < 2) return;
    last_render_service_ms = now;
    rp_platform_service();

    pump_input();
}

void set_format(SDL_PixelFormat *f)
{
    std::memset(f, 0, sizeof(*f));
    f->BitsPerPixel = 16;
    f->BytesPerPixel = 2;
    f->Rmask = 0xF800;
    f->Gmask = 0x07E0;
    f->Bmask = 0x001F;
    f->Rshift = 11;
    f->Gshift = 5;
    f->Bshift = 0;
    f->Rloss = 3;
    f->Gloss = 2;
    f->Bloss = 3;
    f->alpha = SDL_ALPHA_OPAQUE;
}

bool queue_empty() { return event_head == event_tail; }

void push_event(SDL_Event event)
{
    if (event.type < sizeof(event_states) && event_states[event.type] == SDL_IGNORE) return;
    if (event_filter && !event_filter(&event)) return;
    unsigned next = (event_head + 1) % EVENT_QUEUE_SIZE;
    if (next == event_tail) return;
    event_queue[event_head] = event;
    event_head = next;
}

void push_key(SDLKey key, bool pressed, Uint8 scancode = 0)
{
    if (key <= SDLK_UNKNOWN || key >= SDLK_LAST) return;
    key_state[key] = pressed ? 1 : 0;
    SDL_Event event{};
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.type = event.type;
    event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = key;
    event.key.keysym.unicode = unicode_enabled && key < 128 ? static_cast<Uint16>(key) : 0;
    push_event(event);
}

SDLKey key_for_pad_bit(int pad, int bit)
{
    (void)pad;
    switch (bit) {
        case 4: return SDLK_UP;
        case 5: return SDLK_DOWN;
        case 6: return SDLK_LEFT;
        case 7: return SDLK_RIGHT;
#if defined(RP_GAME_LTRIS)
        case 0: return SDLK_END;
        case 1: return SDLK_PAGEDOWN;
#else
        case 0: return SDLK_SPACE;
        case 1: return SDLK_y;
#endif
        case 2: return SDLK_UNKNOWN;
        case 3: return SDLK_RETURN;
        case 8: return SDLK_DELETE;
#if defined(RP_GAME_LBREAKOUT2)
        case 9: return SDLK_y;
#else
        case 9: return SDLK_p;
#endif
        default: return SDLK_UNKNOWN;
    }
}

void pump_input()
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now == last_pad_poll) return;
    last_pad_poll = now;
    for (int pad = 0; pad < 2; ++pad) {
        uint16_t state = rp_platform_read_pad(pad);
        uint16_t changed = state ^ old_pad[pad];
        if((state&0x0c)==0x0c) start_consumed[pad]=true;
        for (int bit = 0; bit < 12; ++bit) {
            if(bit==3) {

                if((changed&8) && !(state&8) && !start_consumed[pad]) {
                    push_key(SDLK_RETURN,true,4);
                    push_key(SDLK_RETURN,false,4);
                }
                continue;
            }
            if (changed & (1u << bit))
                push_key(key_for_pad_bit(pad, bit),
                         (state & (1u << bit)) != 0,
                         static_cast<Uint8>(bit + 1));
        }
        old_pad[pad] = state;
        if(!(state&8)) start_consumed[pad]=false;
    }
}

SDL_Surface *load_r565(const char *path)
{
    std::printf("[asset %lu ms] %s, PSRAM free=%lu\n",
                static_cast<unsigned long>(to_ms_since_boot(get_absolute_time())), path,
                static_cast<unsigned long>(rp_psram_available()));
    rp_platform_service();
    char converted[600];
    std::snprintf(converted, sizeof(converted), "%s.r565", path);
    FILE *fp = std::fopen(converted, "rb");
    if (!fp) {
        SDL_SetError("cannot open %s", converted);
        return nullptr;
    }
    uint8_t hdr[8];
    if (std::fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) ||
        std::memcmp(hdr, "R565", 4) != 0) {
        std::fclose(fp);
        SDL_SetError("invalid R565 asset: %s", converted);
        return nullptr;
    }
    int width = hdr[4] | (hdr[5] << 8);
    int height = hdr[6] | (hdr[7] << 8);
    SDL_Surface *surface = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 16,
                                                 0xF800, 0x07E0, 0x001F, 0);
    if (!surface) {
        std::fclose(fp);
        return nullptr;
    }
    size_t bytes = static_cast<size_t>(surface->pitch) * surface->h;
    size_t loaded = 0;
    while (loaded < bytes) {
        size_t chunk = bytes - loaded;
        if (chunk > 4096) chunk = 4096;
        size_t got = std::fread(static_cast<uint8_t *>(surface->pixels) + loaded, 1, chunk, fp);
        loaded += got;
        rp_platform_service();
        if (got != chunk) break;
    }
    if (loaded != bytes) {
        SDL_FreeSurface(surface);
        std::fclose(fp);
        SDL_SetError("short R565 asset: %s", converted);
        return nullptr;
    }
    std::fclose(fp);
    return surface;
}
}

extern "C" {
int SDL_Init(Uint32 flags)
{
    (void)flags;
    set_format(&video_format);
    video_info.current_w = 640;
    video_info.current_h = 480;
    video_info.vfmt = &video_format;
    std::memset(event_states, SDL_ENABLE, sizeof(event_states));
    return 0;
}

void SDL_Quit(void) {}
const char *SDL_GetError(void) { return error_text; }

void SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(error_text, sizeof(error_text), fmt, ap);
    va_end(ap);
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    (void)depth; (void)rmask; (void)gmask; (void)bmask; (void)amask;
    if (width <= 0 || height <= 0) return nullptr;
    SDL_Surface *s = static_cast<SDL_Surface *>(std::calloc(1, sizeof(*s)));
    SDL_PixelFormat *f = static_cast<SDL_PixelFormat *>(std::calloc(1, sizeof(*f)));
    if (!s || !f) { std::free(s); std::free(f); SDL_SetError("surface metadata OOM"); return nullptr; }
    set_format(f);
    size_t pitch = static_cast<size_t>(width) * 2;
    s->pixels = rp_psram_calloc(static_cast<size_t>(height), pitch);
    if (!s->pixels) { std::free(f); std::free(s); SDL_SetError("surface pixel OOM"); return nullptr; }
    s->flags = flags & ~(SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
    s->format = f;
    s->w = width;
    s->h = height;
    s->pitch = static_cast<Uint16>(pitch);
    s->clip_rect = {0, 0, static_cast<Uint16>(width), static_cast<Uint16>(height)};
    s->refcount = 1;
    return s;
}

SDL_Surface *SDL_AllocSurface(Uint32 flags, int width, int height, int depth,
                              Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask)
{
    return SDL_CreateRGBSurface(flags, width, height, depth, rmask, gmask, bmask, amask);
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    if (surface == video_surface) video_surface = nullptr;
    rp_psram_free(surface->pixels);
    std::free(surface->format);
    std::free(surface);
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *surface, SDL_PixelFormat *format, Uint32 flags)
{
    ScopedPerf measured(&sdl_convert_us);
    (void)format;
    if (!surface) return nullptr;
    SDL_Surface *copy = SDL_CreateRGBSurface(flags, surface->w, surface->h, 16,
                                              0xF800, 0x07E0, 0x001F, 0);
    if (!copy) return nullptr;
    for (int y = 0; y < surface->h; ++y) {
        std::memcpy(static_cast<Uint8 *>(copy->pixels) + y * copy->pitch,
                    static_cast<const Uint8 *>(surface->pixels) + y * surface->pitch,
                    surface->pitch);
        if ((y & 15) == 0) service_if_due();
    }
    copy->flags |= surface->flags & (SDL_SRCCOLORKEY | SDL_SRCALPHA);
    copy->format->colorkey = surface->format->colorkey;
    copy->format->alpha = surface->format->alpha;
    return copy;
}

SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface)
{
    return SDL_ConvertSurface(surface, &video_format, SDL_SWSURFACE);
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    (void)bpp;
    if (video_surface) SDL_FreeSurface(video_surface);
    video_surface = SDL_CreateRGBSurface(flags, width, height, 16, 0xF800, 0x07E0, 0x001F, 0);
    return video_surface;
}

SDL_Surface *SDL_GetVideoSurface(void) { return video_surface; }
const SDL_VideoInfo *SDL_GetVideoInfo(void) { return &video_info; }
int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{ (void)flags; return (width <= 640 && height <= 480) ? (bpp ? bpp : 16) : 0; }
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{ (void)format; (void)flags; return reinterpret_cast<SDL_Rect **>(-1); }

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    (void)format;
    return static_cast<Uint16>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key)
{
    if (!surface) return -1;
    if (flag & SDL_SRCCOLORKEY) surface->flags |= SDL_SRCCOLORKEY;
    else surface->flags &= ~SDL_SRCCOLORKEY;
    surface->format->colorkey = key & 0xFFFFu;
    return 0;
}

int SDL_SetAlpha(SDL_Surface *surface, Uint32 flag, Uint8 alpha)
{
    if (!surface) return -1;
    if (flag & SDL_SRCALPHA) surface->flags |= SDL_SRCALPHA;
    else surface->flags &= ~SDL_SRCALPHA;
    surface->format->alpha = alpha;
    return 0;
}

int SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
    if (!surface) return 0;
    if (!rect) surface->clip_rect = {0, 0, static_cast<Uint16>(surface->w), static_cast<Uint16>(surface->h)};
    else {
        int x0 = rect->x < 0 ? 0 : rect->x;
        int y0 = rect->y < 0 ? 0 : rect->y;
        int x1 = rect->x + rect->w; if (x1 > surface->w) x1 = surface->w;
        int y1 = rect->y + rect->h; if (y1 > surface->h) y1 = surface->h;
        if (x1 < x0) x1 = x0; if (y1 < y0) y1 = y0;
        surface->clip_rect = {static_cast<Sint16>(x0), static_cast<Sint16>(y0),
                              static_cast<Uint16>(x1 - x0), static_cast<Uint16>(y1 - y0)};
    }
    return surface->clip_rect.w && surface->clip_rect.h;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
    if (!dst) return -1;
    ScopedPerf measured(&sdl_fill_us);
    service_if_due();
    SDL_Rect r = dstrect ? *dstrect : SDL_Rect{0, 0, static_cast<Uint16>(dst->w), static_cast<Uint16>(dst->h)};
    int x0 = r.x > dst->clip_rect.x ? r.x : dst->clip_rect.x;
    int y0 = r.y > dst->clip_rect.y ? r.y : dst->clip_rect.y;
    int x1 = r.x + r.w; int clipx1 = dst->clip_rect.x + dst->clip_rect.w; if (x1 > clipx1) x1 = clipx1;
    int y1 = r.y + r.h; int clipy1 = dst->clip_rect.y + dst->clip_rect.h; if (y1 > clipy1) y1 = clipy1;
    Uint16 c = static_cast<Uint16>(color);
    for (int y = y0; y < y1; ++y) {
        auto *row = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(dst->pixels) + y * dst->pitch);
        for (int x = x0; x < x1; ++x) row[x] = c;
        if ((y & 15) == 0) service_if_due();
    }
    return 0;
}

int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (!src || !dst) return -1;
    ScopedPerf measured(&sdl_blit_us);
    ++sdl_blit_calls;
    service_if_due();
    int sx = srcrect ? srcrect->x : 0;
    int sy = srcrect ? srcrect->y : 0;
    int w = srcrect ? srcrect->w : src->w;
    int h = srcrect ? srcrect->h : src->h;
    int dx = dstrect ? dstrect->x : 0;
    int dy = dstrect ? dstrect->y : 0;
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (dx < dst->clip_rect.x) { int d = dst->clip_rect.x - dx; sx += d; w -= d; dx += d; }
    if (dy < dst->clip_rect.y) { int d = dst->clip_rect.y - dy; sy += d; h -= d; dy += d; }
    int cx1 = dst->clip_rect.x + dst->clip_rect.w;
    int cy1 = dst->clip_rect.y + dst->clip_rect.h;
    if (dx + w > cx1) w = cx1 - dx;
    if (dy + h > cy1) h = cy1 - dy;
    if (w <= 0 || h <= 0) { if (dstrect) { dstrect->w = dstrect->h = 0; } return 0; }
    bool keyed = (src->flags & SDL_SRCCOLORKEY) != 0;
    bool alpha = (src->flags & SDL_SRCALPHA) != 0 && src->format->alpha < 255;
    Uint16 key = static_cast<Uint16>(src->format->colorkey);
    unsigned a = src->format->alpha;
    for (int y = 0; y < h; ++y) {
        auto *srow = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(src->pixels) + (sy + y) * src->pitch) + sx;
        auto *drow = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(dst->pixels) + (dy + y) * dst->pitch) + dx;
        if (!keyed && !alpha) {
            std::memcpy(drow, srow, static_cast<size_t>(w) * 2);
            if ((y & 15) == 0) service_if_due();
            continue;
        }
        for (int x = 0; x < w; ++x) {
            Uint16 sp = srow[x];
            if (keyed && sp == key) continue;
            if (!alpha) { drow[x] = sp; continue; }
            Uint16 dp = drow[x];
            unsigned sr = (sp >> 11) & 31, sg = (sp >> 5) & 63, sb = sp & 31;
            unsigned dr = (dp >> 11) & 31, dg = (dp >> 5) & 63, db = dp & 31;
            unsigned ia = 255 - a;
            drow[x] = static_cast<Uint16>((((sr * a + dr * ia) / 255) << 11) |
                                          (((sg * a + dg * ia) / 255) << 5) |
                                          ((sb * a + db * ia) / 255));
        }
        if ((y & 15) == 0) service_if_due();
    }
    if (dstrect) { dstrect->x = dx; dstrect->y = dy; dstrect->w = w; dstrect->h = h; }
    return 0;
}

#if defined(RP_GAME_LTRIS)
void rp_sdl_blit_background(SDL_Surface *src,SDL_Surface *dst)
{
    if(!src || !dst) return;
    const uintptr_t sp=reinterpret_cast<uintptr_t>(src->pixels);
    const uintptr_t dp=reinterpret_cast<uintptr_t>(dst->pixels);
    if(!sp || !dp || src->w<=0 || src->h<=0 || src->w!=dst->w || src->h!=dst->h ||
       src->format->BitsPerPixel!=16 || dst->format->BitsPerPixel!=16 ||
       src->pitch<src->w*2 || dst->pitch<dst->w*2 ||
       (src->flags&SDL_SRCCOLORKEY) ||
       ((src->flags&SDL_SRCALPHA) && src->format->alpha<255) ||
       dst->clip_rect.x || dst->clip_rect.y || dst->clip_rect.w!=dst->w || dst->clip_rect.h!=dst->h ||
       (sp<dp+size_t(dst->pitch)*dst->h && dp<sp+size_t(src->pitch)*src->h)) {
        SDL_BlitSurface(src,nullptr,dst,nullptr);
        return;
    }
    ScopedPerf measured(&sdl_blit_us);
    ++sdl_blit_calls;
    uint32_t started=time_us_32(),service_started=started,service_us=0;
    service_if_due();
    service_us+=time_us_32()-service_started;
    for(int y=0;y<src->h;++y) {
        std::memcpy(static_cast<Uint8 *>(dst->pixels)+y*dst->pitch,
            static_cast<const Uint8 *>(src->pixels)+y*src->pitch,size_t(src->w)*2);
        if((y&15)==0) {
            service_started=time_us_32();
            service_if_due();
            service_us+=time_us_32()-service_started;
        }
    }
    uint32_t elapsed=time_us_32()-started;

    std::printf("[bg_copy] bytes=%lu work_us=%lu service_us=%lu elapsed_us=%lu\n",
        static_cast<unsigned long>(size_t(src->w)*src->h*2),
        static_cast<unsigned long>(elapsed-service_us),
        static_cast<unsigned long>(service_us),static_cast<unsigned long>(elapsed));
}

void rp_sdl_background_cache_reset(void)
{
    if(!background_cache) return;
    if(background_cache->file) std::fclose(background_cache->file);
    for(auto &entry:background_cache->entries) rp_psram_free(entry.pixels);
    rp_psram_free(background_cache);
    background_cache=nullptr;
}

int rp_sdl_background_cache_begin(const char *directory,int width,int height)
{
    rp_sdl_background_cache_reset();
    if(!directory || width<=0 || height<=0 || width>32767 || height>65535) return 0;
    size_t bytes=static_cast<size_t>(width)*height*2;
    if(bytes>640u*480u*2u ||
       rp_psram_available()<4*bytes+sizeof(BackgroundCache)+BACKGROUND_CACHE_RESERVE) return 0;
    background_cache=static_cast<BackgroundCache *>(rp_psram_calloc(1,sizeof(BackgroundCache)));
    if(!background_cache) return 0;
    for(int i=0;i<4;++i) {
        auto &entry=background_cache->entries[i];
        int n=std::snprintf(entry.path,sizeof(entry.path),
            "%s/back%d.bmp.r565",directory,i);
        if(n<0 || static_cast<size_t>(n)>=sizeof(entry.path)) {
            rp_sdl_background_cache_reset(); return 0;
        }
    }
    background_cache->bytes=bytes;
    background_cache->width=width; background_cache->height=height;
    return 1;
}

int rp_sdl_background_cache_step(void)
{
    if(!background_cache || background_cache->next>=4) return 0;
    auto &cache=*background_cache;
    auto &entry=cache.entries[cache.next];
    uint32_t started=time_us_32();
    bool failed=false;
    if(!cache.file) {

        if(rp_psram_available()<(entry.pixels?0:cache.bytes)+BACKGROUND_CACHE_RESERVE) {
            rp_sdl_background_cache_reset(); return 0;
        }
        cache.file=std::fopen(entry.path,"rb");
        Uint8 hdr[8];
        failed=!cache.file ||
            std::setvbuf(cache.file,background_file_buffer,_IOFBF,sizeof(background_file_buffer))!=0 ||
            std::fread(hdr,1,8,cache.file)!=8 || std::memcmp(hdr,"R565",4) ||
            (hdr[4]|(hdr[5]<<8))!=cache.width || (hdr[6]|(hdr[7]<<8))!=cache.height;
        if(!failed && entry.loaded)
            failed=std::fseek(cache.file,static_cast<long>(8+entry.loaded),SEEK_SET)!=0;
        if(!failed && !entry.pixels) {

            entry.pixels=static_cast<Uint8 *>(rp_psram_malloc(cache.bytes));
            failed=!entry.pixels;
        }
    } else {
        size_t count=cache.bytes-entry.loaded;
        if(count>4096) count=4096;
        size_t got=std::fread(entry.pixels+entry.loaded,1,count,cache.file);
        entry.loaded+=got;
        failed=got!=count;
        entry.ready=!failed && entry.loaded==cache.bytes;
    }
    service_if_due();
    uint32_t elapsed=time_us_32()-started;
    ++entry.steps;
    if(elapsed>entry.max_step_us) entry.max_step_us=elapsed;
    if(failed || entry.ready) {
        if(cache.file) std::fclose(cache.file);
        cache.file=nullptr;
        if(failed) {
            rp_psram_free(entry.pixels); entry.pixels=nullptr;
        }
        entry.failed=failed;
        std::printf("[bg_cache] id=%d ready=%d bytes=%lu steps=%lu max_step_us=%lu\n",
            cache.next,entry.ready,static_cast<unsigned long>(entry.loaded),
            static_cast<unsigned long>(entry.steps),static_cast<unsigned long>(entry.max_step_us));

        cache.next=0;
        while(cache.next<4 && (cache.entries[cache.next].ready || cache.entries[cache.next].failed))
            ++cache.next;
        if(cache.next==4) rp_sdl_sram_checkpoint(0);
    }
    return cache.next<4;
}

void rp_sdl_sram_checkpoint(unsigned fragments)
{
#if defined(PICO_ON_DEVICE)

    static unsigned peak_fragments;
    if(fragments && fragments<=peak_fragments) return;
    if(fragments) peak_fragments=fragments;
    auto info=mallinfo();
    size_t capacity=reinterpret_cast<uintptr_t>(&__HeapLimit)-reinterpret_cast<uintptr_t>(&__end__);
    size_t used=static_cast<size_t>(info.uordblks);
    std::printf("[sram] fragments=%u capacity=%lu used=%lu available=%lu arena_free=%lu\n",
        fragments,static_cast<unsigned long>(capacity),static_cast<unsigned long>(used),
        static_cast<unsigned long>(used<capacity?capacity-used:0),
        static_cast<unsigned long>(info.fordblks));
#else
    (void)fragments;
#endif
}

static bool full_background_surface(const SDL_Surface *dst)
{
    return dst && dst->pixels && dst->format && dst->w>0 && dst->h>0 &&
       dst->pitch==dst->w*2 && dst->format->BitsPerPixel==16 &&
       dst->format->Rmask==0xf800 && dst->format->Gmask==0x07e0 && dst->format->Bmask==31 &&
       !dst->clip_rect.x && !dst->clip_rect.y &&
       dst->clip_rect.w==dst->w && dst->clip_rect.h==dst->h;
}

int rp_sdl_take_background(const char *path, SDL_Surface *dst)
{

    if(!path || !background_cache || !full_background_surface(dst) ||
       dst==video_surface || dst->refcount!=1 ||
       dst->w!=background_cache->width || dst->h!=background_cache->height) return 0;
    char converted[600];
    int n=std::snprintf(converted,sizeof(converted),"%s.r565",path);
    if(n<0 || size_t(n)>=sizeof(converted)) return 0;
    auto &cache=*background_cache;
    for(const auto &entry:cache.entries) if(entry.pixels==dst->pixels) return 0;
    for(int i=0;i<4;++i) {
        auto &entry=cache.entries[i];
        if(!entry.ready || std::strcmp(converted,entry.path)) continue;
        void *old=dst->pixels;
        dst->pixels=entry.pixels;
        entry.pixels=static_cast<Uint8 *>(old);
        entry.loaded=0; entry.steps=0; entry.max_step_us=0;
        entry.ready=false; entry.failed=false;
        if(cache.next==4) cache.next=i;
        std::printf("[bg_cache_take] id=%d bytes=%lu copied_bytes=0 refill=queued\n",i,
            static_cast<unsigned long>(cache.bytes));
        return 1;
    }
    return 0;
}

int rp_sdl_load_background(const char *path, SDL_Surface *dst)
{

    if(!path || !full_background_surface(dst)) return 0;
    uint32_t started=time_us_32();
    char converted[600];
    int n=std::snprintf(converted,sizeof(converted),"%s.r565",path);
    if(n<0 || static_cast<size_t>(n)>=sizeof(converted)) return 0;
    if(background_cache && dst->w==background_cache->width && dst->h==background_cache->height) {
        for(int i=0;i<4;++i) {
            const auto &entry=background_cache->entries[i];
            if(!entry.ready || std::strcmp(converted,entry.path)) continue;
            for(int y=0;y<dst->h;++y) {
                std::memcpy(static_cast<Uint8 *>(dst->pixels)+y*dst->pitch,
                    entry.pixels+y*dst->pitch,dst->pitch);
                if((y&15)==0) service_if_due();
            }
            std::printf("[bg_cache_hit] id=%d bytes=%lu copy_us=%lu\n",i,
                static_cast<unsigned long>(background_cache->bytes),
                static_cast<unsigned long>(time_us_32()-started));
            return 1;
        }
    }

    if(background_cache && background_cache->file) {
        std::fclose(background_cache->file);
        background_cache->file=nullptr;
    }
    rp_platform_service();
    FILE *fp=std::fopen(converted,"rb");
    if(!fp) return 0;
    if(std::setvbuf(fp,background_file_buffer,_IOFBF,sizeof(background_file_buffer))!=0) {
        std::fclose(fp); return 0;
    }
    Uint8 hdr[8];
    if(std::fread(hdr,1,8,fp)!=8 || std::memcmp(hdr,"R565",4) ||
       (hdr[4]|(hdr[5]<<8))!=dst->w || (hdr[6]|(hdr[7]<<8))!=dst->h) {
        std::fclose(fp); return 0;
    }
    size_t bytes=static_cast<size_t>(dst->pitch)*dst->h,loaded=0;
    uint32_t fread_us=0,service_us=0,max_fread_us=0,read_calls=0;
    while(loaded<bytes) {
        size_t chunk=bytes-loaded;
        if(chunk>4096) chunk=4096;
        uint32_t chunk_started=time_us_32();
        size_t got=std::fread(static_cast<Uint8 *>(dst->pixels)+loaded,1,chunk,fp);
        uint32_t chunk_us=time_us_32()-chunk_started;
        fread_us+=chunk_us; ++read_calls;
        if(chunk_us>max_fread_us) max_fread_us=chunk_us;
        loaded+=got;
        uint32_t service_started=time_us_32();
        rp_platform_service();
        service_us+=time_us_32()-service_started;
        if(got!=chunk) break;
    }
    std::fclose(fp);
    std::printf("[bg_read] direct=%d bytes=%lu elapsed_us=%lu\n",loaded==bytes,
        static_cast<unsigned long>(loaded),static_cast<unsigned long>(time_us_32()-started));

    std::printf("[bg_read_detail] buffer_bytes=%lu payload_fread_us=%lu service_us=%lu calls=%lu max_fread_us=%lu\n",
        static_cast<unsigned long>(sizeof(background_file_buffer)),
        static_cast<unsigned long>(fread_us),static_cast<unsigned long>(service_us),
        static_cast<unsigned long>(read_calls),static_cast<unsigned long>(max_fread_us));
    return loaded==bytes;
}

void rp_sdl_shade_rect(SDL_Surface *dst, const SDL_Rect *rect, Uint16 color, Uint8 opacity)
{
    if(!dst || !rect) return;
    ScopedPerf measured(&sdl_blit_us);
    ++sdl_blit_calls;
    service_if_due();
    int x0=rect->x>dst->clip_rect.x?rect->x:dst->clip_rect.x;
    int y0=rect->y>dst->clip_rect.y?rect->y:dst->clip_rect.y;
    int x1=rect->x+rect->w, y1=rect->y+rect->h;
    if(x1>dst->clip_rect.x+dst->clip_rect.w) x1=dst->clip_rect.x+dst->clip_rect.w;
    if(y1>dst->clip_rect.y+dst->clip_rect.h) y1=dst->clip_rect.y+dst->clip_rect.h;
    unsigned a=opacity, ia=255-a;
    unsigned r=(color>>11)*a, g=((color>>5)&63)*a, b=(color&31)*a;
    for(int y=y0;y<y1;++y) {
        auto *row=reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(dst->pixels)+y*dst->pitch);
        for(int x=x0;x<x1;++x) {
            unsigned p=row[x];
            row[x]=static_cast<Uint16>((((r+(p>>11)*ia)/255)<<11) |
                (((g+((p>>5)&63)*ia)/255)<<5) | ((b+(p&31)*ia)/255));
        }
        if((y&15)==0) service_if_due();
    }
}
#endif

int SDL_LockSurface(SDL_Surface *surface) { (void)surface; return 0; }
void SDL_UnlockSurface(SDL_Surface *surface) { (void)surface; }
SDL_Surface *SDL_LoadBMP(const char *path) { return load_r565(path); }
SDL_Surface *SDL_LoadPNG(const char *path) { return load_r565(path); }
int SDL_SaveBMP(SDL_Surface *surface, const char *path)
{ (void)surface; SDL_SetError("screenshots unsupported: %s", path); return -1; }

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    if (!screen) return;
    if (w == 0 && h == 0) {
        x = 0;
        y = 0;
        w = static_cast<Uint32>(screen->w);
        h = static_cast<Uint32>(screen->h);
    }
    const rp_platform_rect rect = {
        static_cast<int16_t>(x), static_cast<int16_t>(y),
        static_cast<uint16_t>(w), static_cast<uint16_t>(h)
    };
    rp_platform_present_rects(static_cast<Uint16 *>(screen->pixels),
                              screen->w, screen->h, screen->pitch / 2,
                              1, &rect);
}
void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects)
{
    if (!screen || numrects <= 0 || !rects) return;
    static_assert(sizeof(SDL_Rect) == sizeof(rp_platform_rect),
                  "SDL and platform rectangles must have the same layout");
    static_assert(alignof(SDL_Rect) == alignof(rp_platform_rect),
                  "SDL and platform rectangles must have the same alignment");
    rp_platform_present_rects(static_cast<Uint16 *>(screen->pixels),
                              screen->w, screen->h, screen->pitch / 2,
                              numrects,
                              reinterpret_cast<const rp_platform_rect *>(rects));
}
void SDL_UpdateRectAtomic(SDL_Surface *screen, Sint32 x, Sint32 y,
                          Uint32 w, Uint32 h)
{
    if (!screen || !w || !h) return;
#if defined(RP_GAME_LTRIS)
    const rp_platform_rect rect = {
        static_cast<int16_t>(x), static_cast<int16_t>(y),
        static_cast<uint16_t>(w), static_cast<uint16_t>(h)
    };
    rp_platform_present_rect_atomic(static_cast<Uint16 *>(screen->pixels),
                                    screen->w, screen->h, screen->pitch / 2,
                                    &rect, atomic_hint_staging,
                                    sizeof(atomic_hint_staging));
#else
    SDL_UpdateRect(screen, x, y, w, h);
#endif
}
int SDL_Flip(SDL_Surface *screen) { SDL_UpdateRect(screen, 0, 0, 0, 0); return 0; }

void rp_sdl_take_perf(uint64_t *blit_us, uint64_t *fill_us,
                      uint64_t *convert_us, uint32_t *blit_calls)
{
    if (blit_us) *blit_us = sdl_blit_us;
    if (fill_us) *fill_us = sdl_fill_us;
    if (convert_us) *convert_us = sdl_convert_us;
    if (blit_calls) *blit_calls = sdl_blit_calls;
    sdl_blit_us = sdl_fill_us = sdl_convert_us = 0;
    sdl_blit_calls = 0;
}

Uint32 SDL_GetTicks(void)
{
    service_if_due();
    return to_ms_since_boot(get_absolute_time()) - system_paused_ms;
}
void SDL_Delay(Uint32 ms)
{
    uint32_t start = SDL_GetTicks();
    do { rp_platform_service(); sleep_ms(1); } while (SDL_GetTicks() - start < ms);
}
void SDL_PumpEvents(void) { pump_input(); }
int SDL_PollEvent(SDL_Event *event)
{
    pump_input();

    uint32_t paused=rp_system_menu_poll();
    if (paused) {
        system_paused_ms+=paused;
        event_head=event_tail=0;
        std::memset(key_state,0,sizeof(key_state));
        old_pad[0]=rp_platform_read_pad(0);
        old_pad[1]=rp_platform_read_pad(1);
        start_consumed[0]=start_consumed[1]=false;
        return 0;
    }
    if (queue_empty()) return 0;
    if (event) *event = event_queue[event_tail];
    event_tail = (event_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}
int SDL_WaitEvent(SDL_Event *event)
{ while (!SDL_PollEvent(event)) sleep_ms(1); return 1; }
Uint8 *SDL_GetKeyState(int *numkeys) { pump_input(); if (numkeys) *numkeys = SDLK_LAST; return key_state; }
SDL_EventFilter SDL_SetEventFilter(SDL_EventFilter filter) { SDL_EventFilter old = event_filter; event_filter = filter; return old; }
SDL_EventFilter SDL_GetEventFilter(void) { return event_filter; }
Uint8 SDL_EventState(Uint8 type, int state)
{ Uint8 old = type < sizeof(event_states) ? event_states[type] : SDL_ENABLE; if (state != SDL_QUERY && type < sizeof(event_states)) event_states[type] = state; return old; }
SDLMod SDL_GetModState(void) { return KMOD_NONE; }
int SDL_EnableUNICODE(int enable) { int old = unicode_enabled; unicode_enabled = enable; return old; }

const char *SDL_GetKeyName(SDLKey key)
{
    switch (key) {
        case SDLK_UP: return "up"; case SDLK_DOWN: return "down";
        case SDLK_LEFT: return "left"; case SDLK_RIGHT: return "right";
        case SDLK_RETURN: return "start"; case SDLK_ESCAPE: return "select";
        case SDLK_SPACE: return "A"; case SDLK_PAGEDOWN: return "B";
        default: break;
    }
    static char name[2]; name[0] = key > 0 && key < 128 ? static_cast<char>(key) : '?'; name[1] = 0; return name;
}

Uint8 SDL_GetMouseState(int *x, int *y) { if (x) *x = 320; if (y) *y = 240; return 0; }
Uint8 SDL_GetRelativeMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
void SDL_WarpMouse(Uint16 x, Uint16 y) { (void)x; (void)y; }
int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
int SDL_WM_GrabInput(int mode) { (void)mode; return SDL_GRAB_OFF; }
void SDL_WM_SetCaption(const char *title, const char *icon) { (void)title; (void)icon; }
void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask) { (void)icon; (void)mask; }
SDL_Cursor *SDL_CreateCursor(Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y)
{ (void)data; (void)mask; (void)w; (void)h; (void)hot_x; (void)hot_y; return static_cast<SDL_Cursor *>(std::calloc(1, sizeof(SDL_Cursor))); }
void SDL_FreeCursor(SDL_Cursor *cursor) { if (cursor != &default_cursor) std::free(cursor); }
SDL_Cursor *SDL_GetCursor(void) { return current_cursor; }
void SDL_SetCursor(SDL_Cursor *cursor) { current_cursor = cursor ? cursor : &default_cursor; }
int SDL_NumJoysticks(void) { return 0; }
SDL_Joystick *SDL_JoystickOpen(int index) { (void)index; return nullptr; }
void SDL_JoystickClose(SDL_Joystick *joy) { (void)joy; }
int SDL_JoystickEventState(int state) { return state; }
void SDL_JoystickUpdate(void) {}
int SDL_JoystickNumAxes(SDL_Joystick *joy) { (void)joy; return 0; }
int SDL_JoystickNumBalls(SDL_Joystick *joy) { (void)joy; return 0; }
int SDL_JoystickNumButtons(SDL_Joystick *joy) { (void)joy; return 0; }
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joy, int axis) { (void)joy; (void)axis; return 0; }
Uint8 SDL_JoystickGetButton(SDL_Joystick *joy, int button) { (void)joy; (void)button; return 0; }
}
