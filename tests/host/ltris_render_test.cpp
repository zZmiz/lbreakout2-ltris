// Pixel-exact regression tests: real LT drawing code and embedded SDL shim.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include "rp_platform.h"
#ifdef RP_HOST_SRAM_BUDGET
#include "sram_budget.h"
#endif
extern "C" {
#include "ltris.h"
#include "cpu.h"
#include "bowl.h"
#include "config.h"
#include "list.h"
#include "shrapnells.h"
extern Sdl sdl;
extern List *shrapnells;
size_t rp_psram_debug_mark();
Config config{};
SDL_Surface *offscreen, *bkgnd, *previewpieces;
Bowl *bowls[BOWL_COUNT];
}

static SDL_Surface *surface(int w, int h) {
    auto *s=SDL_CreateRGBSurface(0,w,h,16,0xf800,0x7e0,31,0);
    assert(s); return s;
}
static Uint16 &pixel(SDL_Surface *s,int x,int y) {
    return reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(s->pixels)+y*s->pitch)[x];
}
static void seed(SDL_Surface *s) {
    for(int y=0;y<s->h;++y) for(int x=0;x<s->w;++x)
        pixel(s,x,y)=((x+y)%7==0)?0:Uint16(x*197+y*331+19);
}
static void equal(SDL_Surface *a,SDL_Surface *b) {
    assert(a->w==b->w && a->h==b->h);
    for(int y=0;y<a->h;++y) for(int x=0;x<a->w;++x) {
        if(pixel(a,x,y)!=pixel(b,x,y)) {
            std::fprintf(stderr,"pixel mismatch at %d,%d: %04x != %04x\n",x,y,pixel(a,x,y),pixel(b,x,y));
            assert(false);
        }
    }
}
static void blits() {
    unsigned cases=0;
    for(int w: {1,20,127,640,701}) for(int a: {0,1,64,128,160,254,255})
    for(bool keyed: {false,true}) for(int offset: {-9,0,11}) {
        auto *src=surface(w,23), *dst=surface(w+20,35), *ref=surface(w+20,35);
        seed(src); seed(dst); seed(ref);
        SDL_SetColorKey(src,keyed?SDL_SRCCOLORKEY:0,0);
        SDL_SetAlpha(src,SDL_SRCALPHA,a);
        SDL_Rect clip={3,2,Uint16(w+8),30}; SDL_SetClipRect(dst,&clip);
        SDL_Rect from={Sint16(offset),-2,Uint16(w+7),28},to={-4,4,0,0};

        for(int yy=0;yy<from.h;++yy) for(int xx=0;xx<from.w;++xx) {
            int sx=from.x+xx,sy=from.y+yy,dx=to.x+xx,dy=to.y+yy;
            if(sx<0||sy<0||sx>=src->w||sy>=src->h||dx<clip.x||dy<clip.y||dx>=clip.x+clip.w||dy>=clip.y+clip.h) continue;
            unsigned sp=pixel(src,sx,sy),dp=pixel(ref,dx,dy),ia=255-a;
            if(keyed && sp==0) continue;
            pixel(ref,dx,dy)=((((sp>>11)*a+(dp>>11)*ia)/255)<<11)
                | (((((sp>>5)&63)*a+((dp>>5)&63)*ia)/255)<<5)
                | (((sp&31)*a+(dp&31)*ia)/255);
        }
        SDL_BlitSurface(src,&from,dst,&to); equal(dst,ref);
        SDL_FreeSurface(src); SDL_FreeSurface(dst); SDL_FreeSurface(ref); ++cases;
    }
    for(int w:{20,640,701}) {
        auto *dst=surface(w,35),*ref=surface(w,35); seed(dst); seed(ref);
        SDL_Rect clip={2,3,Uint16(w-3),27},rect={-4,7,Uint16(w+5),33};
        SDL_SetClipRect(dst,&clip);
        for(int y=7;y<30;++y) for(int x=2;x<w-1;++x) pixel(ref,x,y)=0xe73c;
        SDL_FillRect(dst,&rect,0xe73c); equal(dst,ref);
        SDL_FreeSurface(dst); SDL_FreeSurface(ref);
    }
    std::printf("PASS: %u alpha/keyed/clipped blits and fills are pixel-exact\n",cases);
}

static void background_copies() {
    unsigned cases=0;
    for(int w:{1,127,128,129,255,256,257,639,640,641})
    for(int mode=0;mode<8;++mode) {
        auto *src=surface(w,23),*dst=surface(w,23),*ref=surface(w,23);
        seed(src); SDL_FillRect(dst,nullptr,0x7654); SDL_FillRect(ref,nullptr,0x7654);
        if(mode==1) { SDL_Rect r={0,2,Uint16(w),18}; SDL_SetClipRect(dst,&r); SDL_SetClipRect(ref,&r); }
        if(mode==2) SDL_SetColorKey(src,SDL_SRCCOLORKEY,0);
        if(mode==3) SDL_SetAlpha(src,SDL_SRCALPHA,128);
        if(mode==4) SDL_SetAlpha(src,SDL_SRCALPHA,255);
        if(mode==5) {

            SDL_FreeSurface(src); src=surface(w+3,23); seed(src); src->w=w;
            SDL_FreeSurface(dst); dst=surface(w+3,23); SDL_FillRect(dst,nullptr,0x7654); dst->w=w;
            SDL_FreeSurface(ref); ref=surface(w+3,23); SDL_FillRect(ref,nullptr,0x7654); ref->w=w;
            SDL_SetClipRect(src,nullptr); SDL_SetClipRect(dst,nullptr); SDL_SetClipRect(ref,nullptr);
        }
        if(mode==6) { SDL_FreeSurface(src); src=surface(w+1,25); seed(src); }
        if(mode==7) { SDL_FreeSurface(src); src=surface(w,19); seed(src); }
        size_t mark=rp_psram_debug_mark();
        rp_sdl_blit_background(src,dst); SDL_BlitSurface(src,nullptr,ref,nullptr);
        assert(mark==rp_psram_debug_mark()); equal(dst,ref);
        if(mode==5) assert(!std::memcmp(dst->pixels,ref->pixels,size_t(dst->pitch)*dst->h));

        if(mode==0) { rp_sdl_blit_background(dst,dst); equal(dst,ref); }
        SDL_FreeSurface(src); SDL_FreeSurface(dst); SDL_FreeSurface(ref); ++cases;
    }
    std::printf("PASS: %u background copies match SDL, including tails, padded rows, clipping, alpha/key, mismatch and self fallback; zero allocations\n",cases);
}

static void background_ownership() {
    char dir[]="/tmp/ltris-bg-ownership.XXXXXX"; assert(mkdtemp(dir));
    char names[4][180],raw[4][188];
    constexpr int w=1024,h=3; constexpr size_t bytes=w*h*2;
    size_t initial=rp_psram_available();
    auto *dst=surface(w,h),*ref=surface(w,h);
    auto seed_ref=[&](int id) {
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) pixel(ref,x,y)=Uint16(id*3011+x*197+y*331);
    };
    auto write_entry=[&](int id) {
        seed_ref(id); FILE *f=std::fopen(raw[id],"wb"); assert(f);
        unsigned char hdr[8]={'R','5','6','5',0,4,3,0};
        assert(std::fwrite(hdr,1,8,f)==8 && std::fwrite(ref->pixels,1,bytes,f)==bytes);
        assert(std::fclose(f)==0);
    };
    for(int id=0;id<4;++id) {
        std::snprintf(names[id],sizeof(names[id]),"%s/back%d.bmp",dir,id);
        std::snprintf(raw[id],sizeof(raw[id]),"%s.r565",names[id]); write_entry(id);
    }
    auto drain=[] { unsigned steps=0; while(rp_sdl_background_cache_step()) assert(++steps<20); };
    auto take=[&](int id) {
        void *old=dst->pixels; size_t mark=rp_psram_debug_mark(),free=rp_psram_available();
        assert(rp_sdl_take_background(names[id],dst));
        assert(dst->pixels!=old && mark==rp_psram_debug_mark() && free==rp_psram_available());
        seed_ref(id); equal(dst,ref);

        void *current=dst->pixels;
        assert(!rp_sdl_take_background(names[id],dst)); assert(dst->pixels==current); equal(dst,ref);
    };
    assert(!rp_sdl_take_background(names[0],dst));
    assert(rp_sdl_background_cache_begin(dir,w,h));
    for(int n=0;n<5;++n) assert(rp_sdl_background_cache_step());
    take(0);

    void *active=dst->pixels;
    assert(rp_sdl_load_background(names[0],dst)); assert(dst->pixels==active);
    seed_ref(0); equal(dst,ref); SDL_FillRect(dst,nullptr,0x1234);
    drain();
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) assert(pixel(dst,x,y)==0x1234);

    SDL_Rect clip={1,0,1023,3}; SDL_SetClipRect(dst,&clip);
    assert(!rp_sdl_take_background(names[0],dst)); SDL_SetClipRect(dst,nullptr);
    dst->refcount=2; assert(!rp_sdl_take_background(names[0],dst)); dst->refcount=1;
    auto *wrong=surface(2,2); assert(!rp_sdl_take_background(names[0],wrong)); SDL_FreeSurface(wrong);
    unsigned takes=1;
    for(int cycle=0;cycle<25;++cycle) {

        for(int id:{3,1,0,2}) { take(id); ++takes; SDL_FillRect(dst,nullptr,0x9000+id); }
        drain();
    }
    take(0); ++takes;
    assert(unlink(raw[0])==0); drain();
    seed_ref(0); equal(dst,ref); assert(!rp_sdl_take_background(names[0],dst));
    assert(!rp_sdl_load_background(names[0],dst)); equal(dst,ref);

    rp_sdl_background_cache_reset(); equal(dst,ref);
    write_entry(0);
    assert(rp_sdl_background_cache_begin(dir,w,h)); drain(); take(1); ++takes;

    void *ballast=rp_psram_malloc(rp_psram_available()-2u*1024u*1024u+1); assert(ballast);
    assert(!rp_sdl_background_cache_step()); seed_ref(1); equal(dst,ref);
    rp_psram_free(ballast);
    assert(rp_sdl_background_cache_begin(dir,w,h)); drain(); take(2); ++takes;

    SDL_FreeSurface(dst); dst=nullptr;
    drain(); rp_sdl_background_cache_reset();
    SDL_FreeSurface(ref); assert(rp_psram_available()==initial);
    for(int id=0;id<4;++id) assert(unlink(raw[id])==0);
    assert(rmdir(dir)==0);
    std::printf("PASS: %u zero-copy background ownership transfers; partial-stream rescheduling, refill, rejected surfaces, failure/reset ownership and exact pixels\n",takes);
}

static void reference_stats(Bowl &b,SDL_Surface *dest) {
    SDL_BlitSurface(bkgnd,nullptr,dest,nullptr);
    auto &s=b.stats;
    int n=s.cleared[0]+2*s.cleared[1]+3*s.cleared[2]+4*s.cleared[3];
    int values[]={s.pieces,s.i_pieces,s.cleared[0],s.cleared[1],s.cleared[2],s.cleared[3],
        n?((1000*s.cleared[3]*4/n)+5)/10:-1,s.droughts,s.max_drought,
        s.droughts?((10*s.sum_droughts/s.droughts)+5)/10:-1,
        b.lines<b.firstlevelup_lines?b.firstlevelup_lines-b.lines:-1,
        ((1000*b.das_charge/b.das_maxcharge)+5)/10,b.drought>12?b.drought:-1};
    const char *names[]={"Pieces Placed:","I-Pieces:     ","Singles: ","Doubles: ","Triples: ","Tetrises:",
        "Tetris Rate:","Droughts:   ","Drought Max:","Drought Avg:","Transition:","DAS Charge:","Drought:   "};
    int y=b.stats_y+10;
    for(int i=0;i<13;++i) {
        char text[32];
        if(values[i]<0) std::snprintf(text,sizeof(text),"%s -",names[i]);
        else std::snprintf(text,sizeof(text),"%s %d",names[i],values[i]);
        write_text(b.font,dest,b.stats_x+5,y,text,OPAQUE);
        y+=b.font->height+2;
        if(i==1||i==5||i==6||i==9) y+=b.font->height+2;
    }
}
static void stats() {
    Font font{}; font.pic=surface(95*6,8); seed(font.pic);
    font.offset=32; font.length=95; font.height=8;
    for(int i=0;i<95;++i) {font.char_width[i]=6; font.char_offset[i]=6*i; font.keys[i+32]=1;}
    SDL_SetColorKey(font.pic,SDL_SRCCOLORKEY,0);
    bkgnd=surface(320,240); offscreen=surface(320,240); sdl.screen=surface(320,240);
    auto *reference=surface(320,240); seed(bkgnd);
    SDL_BlitSurface(bkgnd,nullptr,offscreen,nullptr); SDL_BlitSurface(bkgnd,nullptr,sdl.screen,nullptr);
    Bowl b{}; b.font=&font; b.show_stats=1; b.stats_x=30; b.stats_y=20; b.stats_w=180; b.stats_h=190;
    b.das_maxcharge=100; b.firstlevelup_lines=10;
    for(int pass=0;pass<4;++pass) {
        if(pass==1) {b.stats.pieces=123; b.stats.i_pieces=22; b.stats.cleared[3]=12; b.das_charge=100; b.drought=23;}
        if(pass==2) {b.stats.pieces=1; b.stats.i_pieces=2; b.das_charge=0; b.drought=0; b.stats.droughts=2; b.stats.sum_droughts=31; b.lines=15;}
        if(pass==3) {SDL_FillRect(bkgnd,nullptr,0x1357); SDL_BlitSurface(bkgnd,nullptr,offscreen,nullptr); SDL_BlitSurface(bkgnd,nullptr,sdl.screen,nullptr); b.rp_stats_valid=0;}
        bowl_draw_stats(&b); reference_stats(b,reference); equal(sdl.screen,reference); equal(offscreen,reference);
        uint64_t bu,fu,cu; uint32_t calls;
        rp_sdl_take_perf(&bu,&fu,&cu,&calls); sdl.rect_count=0;
        bowl_draw_stats(&b); rp_sdl_take_perf(&bu,&fu,&cu,&calls);
        assert(calls==0 && sdl.rect_count==0); equal(sdl.screen,reference);

        SDL_Rect damaged={30,45,180,60}; SDL_FillRect(sdl.screen,&damaged,0xffff);
        SDL_Rect dst=damaged; SDL_BlitSurface(offscreen,&damaged,sdl.screen,&dst);
        bowl_draw_stats(&b); equal(sdl.screen,reference);
    }
    SDL_FreeSurface(font.pic); SDL_FreeSurface(bkgnd); SDL_FreeSurface(offscreen);
    SDL_FreeSurface(sdl.screen); SDL_FreeSurface(reference);
    std::puts("PASS: stats match original full redraw, unchanged frames do zero blits; shorter values/background invalidation/fragment restoration match");
}
static void assets() {
    for(int i=0;i<4;++i) {
        char name[128],raw[136];
        std::snprintf(name,sizeof(name),"sdcard/games/ltris/gfx/back%d.bmp",i);
        std::snprintf(raw,sizeof(raw),"%s.r565",name);
        FILE *f=std::fopen(raw,"rb"); assert(f);
        unsigned char hdr[8]; assert(std::fread(hdr,1,8,f)==8);
        int w=hdr[4]+256*hdr[5],h=hdr[6]+256*hdr[7];
        std::vector<unsigned char> expected(w*h*2);
        assert(std::fread(expected.data(),1,expected.size(),f)==expected.size()); std::fclose(f);
        auto *s=SDL_LoadBMP(name); assert(s && s->w==w && s->h==h);
        assert(std::memcmp(s->pixels,expected.data(),expected.size())==0);
        auto *direct=surface(w,h),*old=surface(w,h); seed(direct);
        SDL_FillRect(old,nullptr,0); SDL_SetColorKey(s,SDL_SRCCOLORKEY,0);
        SDL_BlitSurface(s,nullptr,old,nullptr);
        size_t mark=rp_psram_debug_mark();
        assert(rp_sdl_load_background(name,direct)==1);
        assert(rp_psram_debug_mark()==mark); equal(direct,old);
        SDL_FreeSurface(direct); SDL_FreeSurface(old); SDL_FreeSurface(s);
    }
    std::puts("PASS: all four direct backgrounds match original clear+keyed wallpaper, with zero PSRAM allocations");
}

static void background_fallback() {
    char dir[]="/tmp/ltris-bg-read-test.XXXXXX"; assert(mkdtemp(dir));
    char name[160],raw[168]; std::snprintf(name,sizeof(name),"%s/test.bmp",dir);
    std::snprintf(raw,sizeof(raw),"%s.r565",name);
    auto *dst=surface(4,3),*ref=surface(4,3); seed(dst); seed(ref);
    assert(!rp_sdl_load_background(name,dst)); equal(dst,ref);
    auto write_fixture=[&](const unsigned char *hdr,size_t count) {
        FILE *f=std::fopen(raw,"wb"); assert(f);
        assert(std::fwrite(hdr,1,8,f)==8);
        unsigned char pixels[24]={0,0,0xff,0xff,0x34,0x12,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        assert(std::fwrite(pixels,1,count,f)==count); assert(std::fclose(f)==0);
    };
    const unsigned char valid[8]={'R','5','6','5',4,0,3,0};
    const unsigned char bad[8]={'N','O','P','E',4,0,3,0};
    const unsigned char tiled[8]={'R','5','6','5',2,0,3,0};
    write_fixture(bad,24); assert(!rp_sdl_load_background(name,dst)); equal(dst,ref);
    write_fixture(tiled,12); assert(!rp_sdl_load_background(name,dst)); equal(dst,ref);
    write_fixture(valid,24);
    SDL_Rect clip={1,0,3,3}; SDL_SetClipRect(dst,&clip);
    assert(!rp_sdl_load_background(name,dst)); equal(dst,ref); SDL_SetClipRect(dst,nullptr);
    assert(rp_sdl_load_background(name,dst));
    auto *loaded=SDL_LoadBMP(name); assert(loaded);
    SDL_SetColorKey(loaded,SDL_SRCCOLORKEY,0); SDL_FillRect(ref,nullptr,0);
    SDL_BlitSurface(loaded,nullptr,ref,nullptr); equal(dst,ref); SDL_FreeSurface(loaded);

    write_fixture(valid,8); assert(!rp_sdl_load_background(name,dst));
    SDL_FillRect(dst,nullptr,0); assert(SDL_LoadBMP(name)==nullptr);
    SDL_FillRect(ref,nullptr,0); equal(dst,ref);
    SDL_FreeSurface(dst); SDL_FreeSurface(ref); assert(unlink(raw)==0); assert(rmdir(dir)==0);
    std::puts("PASS: direct background rejects missing/bad/tiled/clipped inputs; black-key equivalence and short-read fallback");
}

static void background_cache() {
    char dir[]="/tmp/ltris-bg-cache-test.XXXXXX"; assert(mkdtemp(dir));
    char names[4][180],raw[4][188];
    constexpr int w=2048,h=3;
    constexpr size_t bytes=w*h*2;
    auto *dst=surface(w,h),*ref=surface(w,h);
    size_t initial=rp_psram_available();
    for(int id=0;id<4;++id) {
        std::snprintf(names[id],sizeof(names[id]),"%s/back%d.bmp",dir,id);
        std::snprintf(raw[id],sizeof(raw[id]),"%s.r565",names[id]);
    }
    auto seed_ref=[&](int id) {
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) pixel(ref,x,y)=Uint16(id*3011+x*197+y*331);
    };
    auto write_entry=[&](int id,size_t count,bool bad=false) {
        seed_ref(id); FILE *f=std::fopen(raw[id],"wb"); assert(f);
        unsigned char hdr[8]={'R','5','6','5',0,8,3,0}; if(bad) hdr[0]='X';
        assert(std::fwrite(hdr,1,8,f)==8);
        assert(std::fwrite(ref->pixels,1,count,f)==count); assert(std::fclose(f)==0);
    };
    std::srand(913); int expected_random=std::rand(); std::srand(913);
    size_t mark=rp_psram_debug_mark();

    assert(rp_sdl_background_cache_begin(dir,w,h));
    size_t configured=rp_psram_available(),metadata=initial-configured;
    assert(rp_psram_debug_mark()==mark+1 && metadata>0 && metadata<4096);
    for(int id=0;id<4;++id) write_entry(id,bytes);
    assert(rp_sdl_background_cache_step());
    assert(rp_psram_available()==configured-bytes);
    assert(rp_sdl_background_cache_step());

    assert(rp_sdl_load_background(names[0],dst)); seed_ref(0); equal(dst,ref);
    unsigned steps=2;
    int more;
    do {more=rp_sdl_background_cache_step(); assert(++steps<=17);} while(more);
    assert(steps==17 && rp_psram_available()==configured-4*bytes);
    assert(std::rand()==expected_random);

    for(int id=0;id<4;++id) assert(unlink(raw[id])==0);
    for(int id=0;id<4;++id) {
        seed_ref(id); mark=rp_psram_debug_mark();
        SDL_FillRect(dst,nullptr,0x9876); assert(rp_sdl_load_background(names[id],dst)); equal(dst,ref);
        SDL_FillRect(dst,nullptr,0x4321); assert(rp_sdl_load_background(names[id],dst)); equal(dst,ref);
        assert(rp_psram_debug_mark()==mark);
    }
    SDL_Rect clip={1,0,2047,3}; SDL_SetClipRect(dst,&clip);
    assert(!rp_sdl_load_background(names[0],dst)); SDL_SetClipRect(dst,nullptr);
    auto *wrong=surface(2,2); assert(!rp_sdl_load_background(names[0],wrong)); SDL_FreeSurface(wrong);
    rp_sdl_background_cache_reset(); assert(rp_psram_available()==initial);

    write_entry(0,bytes);
    for(int n=0;n<100;++n) {
        assert(rp_sdl_background_cache_begin(dir,w,h));
        assert(rp_sdl_background_cache_step()); assert(rp_sdl_background_cache_step());
        rp_sdl_background_cache_reset(); assert(rp_psram_available()==initial);
    }
    for(int payload_steps=0;payload_steps<3;++payload_steps) {
        assert(rp_sdl_background_cache_begin(dir,w,h));
        assert(rp_sdl_background_cache_step());
        for(int n=0;n<payload_steps;++n) assert(rp_sdl_background_cache_step());

        assert(!rp_sdl_load_background(names[1],dst));
        assert(rp_sdl_load_background(names[0],dst)); seed_ref(0); equal(dst,ref);
        steps=0; do {more=rp_sdl_background_cache_step(); assert(++steps<20);} while(more);
        assert(unlink(raw[0])==0);
        assert(rp_sdl_load_background(names[0],dst)); equal(dst,ref);
        rp_sdl_background_cache_reset(); assert(rp_psram_available()==initial);
        write_entry(0,bytes);
    }
    assert(unlink(raw[0])==0);
    write_entry(1,bytes,true); write_entry(2,3072); write_entry(3,bytes);
    assert(rp_sdl_background_cache_begin(dir,w,h));
    steps=0; do {more=rp_sdl_background_cache_step(); assert(++steps<20);} while(more);
    assert(rp_psram_available()==initial-metadata-bytes);
    for(int id=1;id<4;++id) assert(unlink(raw[id])==0);
    for(int id=0;id<3;++id) assert(!rp_sdl_load_background(names[id],dst));
    assert(rp_sdl_load_background(names[3],dst)); seed_ref(3); equal(dst,ref);
    rp_sdl_background_cache_reset(); assert(rp_psram_available()==initial);

    size_t reserve=2u*1024u*1024u;
    void *ballast=rp_psram_malloc(initial-(4*bytes+reserve)+1); assert(ballast);
    assert(!rp_sdl_background_cache_begin(dir,w,h)); rp_psram_free(ballast);
    assert(rp_sdl_background_cache_begin(dir,w,h));
    ballast=rp_psram_malloc(initial-(bytes+reserve)+1); assert(ballast);
    assert(!rp_sdl_background_cache_step()); rp_psram_free(ballast);
    assert(rp_psram_available()==initial);
    SDL_FreeSurface(dst); SDL_FreeSurface(ref); assert(rmdir(dir)==0);

    initial=rp_psram_available();
    assert(rp_sdl_background_cache_begin("sdcard/games/ltris/gfx",640,480));
    steps=0; do {more=rp_sdl_background_cache_step(); assert(++steps<=604);} while(more);
    assert(steps==604 && rp_psram_available()==initial-metadata-4u*640u*480u*2u);
    assets(); rp_sdl_background_cache_reset(); assert(rp_psram_available()==initial);
    std::puts("PASS: incremental cache hits/misses/interleaved streams/RNG/4 real assets/100 partial resets/failures/memory reserve; no partial-image exposure or leaks");
}

static void background_buffer_boundaries() {
    char dir[]="/tmp/ltris-bg-buffer-test.XXXXXX"; assert(mkdtemp(dir));
    char name[160],raw[168]; std::snprintf(name,sizeof(name),"%s/test.bmp",dir);
    std::snprintf(raw,sizeof(raw),"%s.r565",name);
    unsigned cases=0;
    for(int w:{2043,2044,2045,2048,4092,4096}) {
        auto *dst=surface(w,1),*ref=surface(w,1); seed(ref);
        size_t bytes=size_t(w)*2;
        unsigned char hdr[8]={'R','5','6','5',static_cast<unsigned char>(w&255),
            static_cast<unsigned char>(w>>8),1,0};
        auto write_fixture=[&](size_t payload) {
            FILE *f=std::fopen(raw,"wb"); assert(f);
            assert(std::fwrite(hdr,1,8,f)==8);
            assert(std::fwrite(ref->pixels,1,payload,f)==payload);
            assert(std::fclose(f)==0);
        };

        for(size_t cut:{size_t(0),size_t(4087),size_t(4088),size_t(4095),size_t(4096),size_t(4097)}) {
            if(cut>=bytes) continue;
            write_fixture(cut); SDL_FillRect(dst,nullptr,0xffff);
            assert(!rp_sdl_load_background(name,dst));
            assert(std::memcmp(dst->pixels,ref->pixels,cut)==0);
            write_fixture(bytes);
            size_t mark=rp_psram_debug_mark();
            assert(rp_sdl_load_background(name,dst)); equal(dst,ref);
            assert(rp_psram_debug_mark()==mark); ++cases;
        }
        SDL_FreeSurface(dst); SDL_FreeSurface(ref);
    }
    assert(unlink(raw)==0); assert(rmdir(dir)==0);
    std::printf("PASS: %u 4-KiB background buffer boundary/short-read/reopen cases; exact pixels, no PSRAM allocations\n",cases);
}

static void original_frame(SDL_Surface *dst,int cx,int cy,int w,int h,int b) {
    auto *mask=surface(w+2*b,h+2*b),*black=surface(w,h);
    SDL_SetColorKey(mask,SDL_SRCCOLORKEY,0xf800);
    DEST(dst,cx,cy,w,h); SOURCE(dst,cx-b+1,cy-b+1); blit_surf();
    for(int pass=0;pass<2;++pass) {
        SDL_FillRect(mask,nullptr,0xf800);
        if(pass==0) {
            SDL_Rect top={0,0,Uint16(w+b),Uint16(b)},left={0,0,Uint16(b),Uint16(h+b)};
            SDL_FillRect(mask,&top,0); SDL_FillRect(mask,&left,0);
        } else {
            SDL_Rect right={Sint16(w+b),Sint16(b),Uint16(b),Uint16(h+b)};
            SDL_Rect bottom={Sint16(b),Sint16(h+b),Uint16(w+b),Uint16(b)};
            SDL_FillRect(mask,&right,0xffff); SDL_FillRect(mask,&bottom,0xffff);
        }
        for(int i=0;i<b;++i) for(int j=0;j<b;++j) {
            if(pass==0 ? i<j : i>=j) pixel(mask,b+w+i,b-j-1)=pass?0xffff:0;
            if(pass==0 ? i>j : i<=j) pixel(mask,b-i-1,b+h+j)=pass?0xffff:0;
        }
        DEST(dst,cx-b,cy-b,w+2*b,h+2*b); SOURCE(mask,0,0); alpha_blit_surf(pass?128:64);
    }
    SDL_FillRect(black,nullptr,0);
    DEST(dst,cx,cy,w,h); SOURCE(black,0,0); alpha_blit_surf(48);
    SDL_FreeSurface(mask); SDL_FreeSurface(black);
}
static void frames() {
    for(int b:{1,4,6}) for(int size:{10,80,200}) for(int edge:{-3,0,20}) {
        auto *actual=surface(640,480),*ref=surface(640,480); seed(actual); seed(ref);

        SDL_SetColorKey(actual,SDL_SRCCOLORKEY,0xffff); SDL_SetColorKey(ref,SDL_SRCCOLORKEY,0xffff);
        SDL_Rect clip={2,3,630,471}; SDL_SetClipRect(actual,&clip); SDL_SetClipRect(ref,&clip);
        int height=size==200?400:size+13;
        draw_3dframe(actual,edge+8,edge+7,size,height,b);
        original_frame(ref,edge+8,edge+7,size,height,b); equal(actual,ref);
        SDL_FreeSurface(actual); SDL_FreeSurface(ref);
    }
    std::puts("PASS: direct frame shading matches original temporary-surface masks (27 geometries/clips)");
}
static void original_contents(Bowl &b,SDL_Surface *back,SDL_Surface *screen) {
    for(int j=0;j<b.h;++j) for(int i=0;i<b.w;++i) {
        int x=b.sx+i*b.block_size,y=b.sy+j*b.block_size,v=b.contents[i][j];
        SDL_Rect to={Sint16(x),Sint16(y),Uint16(b.block_size),Uint16(b.block_size)};
        SDL_Rect src=to;
        if(v>=0) {src.x=v*b.block_size; src.y=0;}
        SDL_BlitSurface(v>=0?b.blocks:bkgnd,&src,back,&to);
        SDL_Rect out=to; SDL_BlitSurface(back,&to,screen,&out);
    }
}
static void contents() {
    bkgnd=surface(280,460); offscreen=surface(280,460); sdl.screen=surface(280,460);
    auto *old_back=surface(280,460),*old_screen=surface(280,460);
    seed(bkgnd); seed(offscreen); seed(sdl.screen); seed(old_back); seed(old_screen);
    Bowl b{}; b.sx=30; b.sy=20; b.w=10; b.h=20; b.sw=200; b.sh=400; b.block_size=20;
    b.blocks=surface(140,20); seed(b.blocks); SDL_SetColorKey(b.blocks,SDL_SRCCOLORKEY,0);
    for(auto &col:b.contents) for(auto &v:col) v=-1;
    for(int pass=0;pass<8;++pass) {
        if(pass==1) for(int x=3;x<7;++x) b.contents[x][19]=2;
        if(pass==2) for(int x=0;x<10;++x) b.contents[x][19]=x%7;
        if(pass==3) for(int x=0;x<10;++x) b.contents[x][19]=-1;
        if(pass==4) for(int x=0;x<10;++x) for(int y=15;y<19;++y) b.contents[x][y]=(x+y)%3?((x+y)%7):-1;
        if(pass==5) for(int x=0;x<10;++x) for(int y=19;y>0;--y) b.contents[x][y]=b.contents[x][y-1];
        if(pass==6) {SDL_FillRect(bkgnd,nullptr,0x26a7); b.rp_contents_valid=0;}
        if(pass==7) {for(auto &col:b.contents) for(auto &v:col) v=-1; SDL_Rect msg={75,190,60,15}; SDL_FillRect(bkgnd,&msg,0xffff); b.rp_contents_valid=0;}
        uint64_t bu,fu,cu; uint32_t calls;
        sdl.rect_count=0; rp_sdl_take_perf(&bu,&fu,&cu,&calls);
        bowl_draw_contents(&b); rp_sdl_take_perf(&bu,&fu,&cu,&calls);
        if(pass==1) {assert(calls==8); assert(sdl.rect_count==1);}
        if(pass==3) {assert(calls==20); assert(sdl.rect_count==1);}
        original_contents(b,old_back,old_screen);
        equal(offscreen,old_back); equal(sdl.screen,old_screen);
        rp_sdl_take_perf(&bu,&fu,&cu,&calls); sdl.rect_count=0;
        bowl_draw_contents(&b); rp_sdl_take_perf(&bu,&fu,&cu,&calls);
        assert(calls==0 && sdl.rect_count==0);
    }

    Font font{}; font.pic=surface(95*6,8); seed(font.pic); font.offset=32; font.length=95; font.height=8;
    for(int i=0;i<95;++i) {font.char_width[i]=6; font.char_offset[i]=6*i; font.keys[i+32]=1;}
    b.font=&font; b.contents[3][10]=2;
    bowl_draw_contents(&b); original_contents(b,old_back,old_screen);
    bowl_toggle_pause(&b); assert(b.paused && !b.rp_contents_valid);
    bowl_toggle_pause(&b); assert(!b.paused && b.rp_contents_valid);

    SDL_Rect bowl_rect={Sint16(b.sx),Sint16(b.sy),Uint16(b.sw),Uint16(b.sh)},bowl_dst=bowl_rect;
    SDL_BlitSurface(bkgnd,&bowl_rect,old_back,&bowl_dst); original_contents(b,old_back,old_screen);
    equal(offscreen,old_back); equal(sdl.screen,old_screen);
    SDL_FreeSurface(font.pic); SDL_FreeSurface(b.blocks); SDL_FreeSurface(bkgnd);
    SDL_FreeSurface(offscreen); SDL_FreeSurface(sdl.screen); SDL_FreeSurface(old_back); SDL_FreeSurface(old_screen);
    std::puts("PASS: dirty bowl matches full redraw for insert/clear/collapse/background/game-over text/pause; unchanged=0, insert=8, clear-row=20 blits (original=400)");
}
static void fragments() {

    bool seen[8]={}; int seeds[8]={};
    for(int n=0;n<1000;++n) {std::srand(n); int type=std::rand()%8; seen[type]=true; seeds[type]=n;}
    unsigned cases=0;
    for(int type=0;type<8;++type) {assert(seen[type]);
    for(int dt:{16,75,160,366}) for(int swap:{-1,0,1}) {
        sdl.screen=SDL_SetVideoMode(640,480,16,0); offscreen=surface(640,480);
        auto *shown=surface(640,480); seed(offscreen);
        SDL_BlitSurface(offscreen,nullptr,sdl.screen,nullptr); SDL_BlitSurface(offscreen,nullptr,shown,nullptr);
        shrapnells_init(); config.anim=1; std::srand(seeds[type]);
        assert(!shrapnells_active());
        shrapnells_create(220,390,200,20);
        assert(shrapnells_active());
        SDL_Rect cleared={220,390,200,20}; SDL_FillRect(offscreen,&cleared,0x0863);
        SDL_Rect to=cleared; SDL_BlitSurface(offscreen,&cleared,sdl.screen,&to);
        sdl.rect_count=0; add_refresh_rect(220,390,200,20);
        for(int step=0;step<30;++step) {
            if(step) shrapnells_hide();
            if(step==swap) {
                SDL_FillRect(offscreen,nullptr,0x52b7);
                SDL_BlitSurface(offscreen,nullptr,sdl.screen,nullptr);
                add_refresh_rect(0,0,640,480);
            }
            shrapnells_update(dt); shrapnells_show();
            if(sdl.rect_count==RECT_LIMIT) SDL_BlitSurface(sdl.screen,nullptr,shown,nullptr);
            else for(int i=0;i<sdl.rect_count;++i) {
                SDL_Rect r=sdl.rect[i],d=r; SDL_BlitSurface(sdl.screen,&r,shown,&d);
            }
            equal(shown,sdl.screen); sdl.rect_count=0;
            if(shrapnells->count==0) break;
        }
        assert(shrapnells->count==0 && !shrapnells_active()); equal(sdl.screen,offscreen); equal(shown,offscreen);
        shrapnells_delete(); SDL_FreeSurface(shown); SDL_FreeSurface(offscreen); SDL_FreeSurface(sdl.screen);
        ++cases;
    }}
    std::printf("PASS: %u actual fragment cleanup/damage simulations (8 effects, 4 frame steps, background changes); no persistent software residue\n",cases);
}

static void background_contents() {
    unsigned cases=0;

    for(int bg=0;bg<4;++bg) for(int population:{0,1,37,200})
    for(int layout:{0,1,2}) for(bool clipped:{false,true}) {
        char name[128]; std::snprintf(name,sizeof(name),"sdcard/games/ltris/gfx/back%d.bmp",bg);
        bkgnd=SDL_LoadBMP(name); assert(bkgnd); SDL_SetColorKey(bkgnd,0,0);
        offscreen=surface(640,480); sdl.screen=surface(640,480);
        auto *old_back=surface(640,480),*old_screen=surface(640,480);
        seed(offscreen); seed(sdl.screen); seed(old_back); seed(old_screen);
        if(clipped) {
            SDL_Rect clip={18,12,598,447};
            SDL_SetClipRect(offscreen,&clip); SDL_SetClipRect(old_back,&clip);
            SDL_SetClipRect(sdl.screen,&clip); SDL_SetClipRect(old_screen,&clip);
        }
        Bowl b[2]{}; int count=layout==1?2:1, occupied=0;
        for(int n=0;n<count;++n) {
            auto &v=b[n];
            v.sx=layout==0?220:(layout==1?35+n*310:-5); v.sy=25;
            v.w=10; v.h=20; v.sw=200; v.sh=400; v.block_size=20;
            v.blind=layout==2; v.rp_contents_valid=0;
            v.blocks=surface(140,20); seed(v.blocks); SDL_SetColorKey(v.blocks,SDL_SRCCOLORKEY,0);
            for(int j=0;j<20;++j) for(int i=0;i<10;++i) {

                int index=((j*10+i)*73)%200;
                v.contents[i][j]=index<population?(i+j+n)%7:-1;
                v.rp_drawn_contents[i][j]=5;
                if(!v.blind && v.contents[i][j]!=-1) ++occupied;
            }
        }
        rp_sdl_blit_background(bkgnd,offscreen);
        SDL_BlitSurface(bkgnd,nullptr,old_back,nullptr);
        uint64_t bu,fu,cu; uint32_t calls;
        rp_sdl_take_perf(&bu,&fu,&cu,&calls); sdl.rect_count=0;
        size_t mark=rp_psram_debug_mark();
        for(int n=0;n<count;++n) bowl_draw_contents_after_background(&b[n]);
        rp_sdl_take_perf(&bu,&fu,&cu,&calls);
        assert(calls==unsigned(occupied) && sdl.rect_count==0);
        assert(rp_psram_debug_mark()==mark);
        for(int n=0;n<count;++n) if(!b[n].blind) original_contents(b[n],old_back,old_screen);
        rp_sdl_blit_background(offscreen,sdl.screen);
        SDL_BlitSurface(old_back,nullptr,old_screen,nullptr);
        equal(offscreen,old_back); equal(sdl.screen,old_screen);
        for(int n=0;n<count;++n) {
            assert(b[n].rp_contents_valid==!b[n].blind);
            if(!b[n].blind) for(int j=0;j<20;++j) for(int i=0;i<10;++i)
                assert(b[n].rp_drawn_contents[i][j]==b[n].contents[i][j]);
        }

        rp_sdl_take_perf(&bu,&fu,&cu,&calls); sdl.rect_count=0;
        for(int n=0;n<count;++n) bowl_draw_contents(&b[n]);
        rp_sdl_take_perf(&bu,&fu,&cu,&calls);
        assert(calls==0 && sdl.rect_count==0);
        for(int n=0;n<count;++n) {
            auto &v=b[n];
            v.contents[2][18]=v.contents[2][18]==-1?3:-1;
            v.contents[5][19]=v.contents[5][19]==-1?4:-1;
            bowl_draw_contents(&v);
            if(!v.blind) original_contents(v,old_back,old_screen);
        }
        equal(offscreen,old_back); equal(sdl.screen,old_screen);
        for(int n=0;n<count;++n) SDL_FreeSurface(b[n].blocks);
        SDL_FreeSurface(bkgnd); SDL_FreeSurface(offscreen); SDL_FreeSurface(sdl.screen);
        SDL_FreeSurface(old_back); SDL_FreeSurface(old_screen); ++cases;
    }
    std::printf("PASS: %u background-only compositions match old renderer; occupied-only blits, no allocations, cache/next-clear/dual-bowl/blind/clip preserved\n",cases);
}
#ifdef RP_HOST_SRAM_BUDGET
static int constrained_clears() {
    size_t capacity=47616;
    if(const char *value=std::getenv("RP_TEST_SRAM_HEAP_CAPACITY")) capacity=std::strtoul(value,nullptr,10);
    size_t initial=rp_psram_available();

    sram_budget_begin(capacity,36864+4096);
    sdl.screen=SDL_SetVideoMode(640,480,16,0);
    if(!sdl.screen) {
        assert(sram_budget_failures()>0); sram_budget_end();
        std::puts("SRAM MODEL REJECTED: insufficient heap even for screen metadata");
        return 2;
    }
    offscreen=surface(640,480);

    void *game_assets=rp_psram_malloc(1701216); assert(game_assets);
    assert(rp_sdl_background_cache_begin("sdcard/games/ltris/gfx",640,480));
    unsigned steps=0; int more;
    do {more=rp_sdl_background_cache_step(); assert(++steps<=604);} while(more);
    assert(steps==604);
    int seeds[8]; bool seen[8]={};
    for(int seed_value=0;seed_value<1000;++seed_value) {
        std::srand(seed_value); int type=std::rand()%8; seeds[type]=seed_value; seen[type]=true;
    }
    unsigned cases=0;
    config.anim=1;
    for(int repeat=0;repeat<10;++repeat) for(int type=0;type<8;++type) for(int lines=1;lines<=4;++lines) {
        assert(seen[type]);
        char path[128]; std::snprintf(path,sizeof(path),"sdcard/games/ltris/gfx/back%d.bmp",cases%4);
        assert(rp_sdl_take_background(path,offscreen));
        SDL_BlitSurface(offscreen,nullptr,sdl.screen,nullptr);
        shrapnells_init();
        for(int row=0;row<lines;++row) {
            std::srand(seeds[type]);
            shrapnells_create(220,400-lines*20+row*20,200,20);
        }
        assert(shrapnells->count==10*lines);

        void *snapshot=rp_psram_malloc(640*480); assert(snapshot); rp_psram_free(snapshot);
        for(int frame=0;frame<8 && shrapnells_active();++frame) {
            shrapnells_hide(); shrapnells_update(75); shrapnells_show();
            sdl.rect_count=0;
        }
        assert(!shrapnells_active()); equal(sdl.screen,offscreen);
        shrapnells_delete(); assert(sram_budget_failures()==0); ++cases;
        steps=0;
        do {more=rp_sdl_background_cache_step(); assert(++steps<=151);} while(more);
        assert(steps==151);
    }
    rp_sdl_background_cache_reset(); rp_psram_free(game_assets);
    SDL_FreeSurface(offscreen); SDL_FreeSurface(sdl.screen);
    assert(rp_psram_available()==initial && sram_budget_failures()==0);
    sram_budget_end();
    std::printf("PASS: %u constrained-SRAM clears (8 effects x 1-4 lines x 10), background ownership exchange/refill, 40 live fragment surfaces, menu snapshot reserve, no leaks\n",cases);
    return 0;
}
#endif
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
#ifdef RP_HOST_SRAM_BUDGET
    return constrained_clears();
#else
    blits(); background_copies(); background_ownership(); stats(); assets(); background_fallback(); background_buffer_boundaries(); background_cache(); frames(); contents(); fragments(); background_contents();
#endif
}
