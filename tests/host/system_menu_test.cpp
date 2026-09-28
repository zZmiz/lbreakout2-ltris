
#include "rp_platform.h"
#include "SDL.h"
#include "launch_request.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <vector>

struct Event {unsigned ms; uint16_t pad;};
struct Reboot {int game;};
struct Exit {int code;};
static uint8_t framebuffer[640*480];
static unsigned now_ms;
static std::vector<Event> events;
static bool fail_alloc, paused;
static int allocations;
static bool enabled=true;
static unsigned screenshot_ms=~0u;
static const char *screenshot_path;
static uint16_t last_report=0xffff;
static bool inject_fast_chord;
extern "C" void tuh_hid_mount_cb(uint8_t,uint8_t,const uint8_t *,uint16_t);
extern "C" void tuh_hid_umount_cb(uint8_t,uint8_t);
extern "C" void tuh_hid_report_received_cb(uint8_t,uint8_t,const uint8_t *,uint16_t);

static void usb_report(uint16_t pad) {
    uint8_t r[7]={128,128,128,128,128,0,0};
    if(pad&64) r[0]=0;
    if(pad&128) r[0]=255;
    if(pad&16) r[1]=0;
    if(pad&32) r[1]=255;
    if(pad&1) r[5]|=0x20;
    if(pad&2) r[5]|=0x40;
    if(pad&4) r[6]|=0x10;
    if(pad&8) r[6]|=0x20;
    if(pad&256) r[5]|=0x10;
    if(pad&512) r[5]|=0x80;
    tuh_hid_report_received_cb(1,0,r,sizeof(r));
}

static void screenshot() {
    FILE *f=std::fopen(screenshot_path,"wb"); assert(f);
    std::fprintf(f,"P6\n640 480\n255\n");
    for(uint8_t p:framebuffer) {
        uint8_t rgb[]={uint8_t((p>>5)*255/7),uint8_t(((p>>2)&7)*255/7),uint8_t((p&3)*255/3)};
        std::fwrite(rgb,1,3,f);
    }
    std::fclose(f);
}

uint8_t *hstx_getframebuffer() {return framebuffer;}
void hstx_waitForVSync() {}
uint32_t time_us_32() {return now_ms*1000;}
uint64_t get_absolute_time() {return now_ms;}
uint32_t to_ms_since_boot(uint64_t time) {return time;}
void sleep_ms(uint32_t ms) {
    now_ms+=ms; assert(now_ms<30000);
    if(now_ms==screenshot_ms) screenshot();
}
void stdio_flush() {}
void tight_loop_contents() {throw std::runtime_error("unexpected spin");}
extern "C" void rp_platform_launch_slot(int game) {throw Reboot{game};}
extern "C" void rp_platform_bootsel() {throw Exit{99};}
extern "C" void rp_exit(int code) {throw Exit{code};}
extern "C" void rp_platform_service() {
    if(inject_fast_chord && now_ms>=10) {
        inject_fast_chord=false;
        usb_report(12); usb_report(0);
    }
}
extern "C" uint16_t rp_platform_read_pad(int n) {
    if(n) return 0;
    uint16_t pad=0;
    for(auto &e:events) if(now_ms>=e.ms) pad=e.pad;
    if(pad!=last_report) {last_report=pad; usb_report(pad);}
    return rp_usb_pad(0);
}
extern "C" void *rp_psram_malloc(size_t n) {
    if(fail_alloc) return nullptr;
    ++allocations; return std::malloc(n);
}
extern "C" void rp_psram_free(void *p) {--allocations; std::free(p);}
extern "C" void rp_audio_system_pause(int value) {paused=value;}
extern "C" void rp_audio_system_enable(int value) {enabled=value;}

static void setup() {
    now_ms=0; events.clear(); paused=false; fail_alloc=false;
    inject_fast_chord=false;
    screenshot_ms=~0u;
    last_report=0xffff;
    tuh_hid_umount_cb(1,0);
    tuh_hid_mount_cb(1,0,nullptr,0);
#ifdef RP_GAME_LBREAKOUT2
    rp_system_menu_init("LBreakout2");
#else
    rp_system_menu_init("LTris");
#endif
    rp_system_menu_load_preferences();
    for(unsigned i=0;i<sizeof(framebuffer);++i) framebuffer[i]=(i*23+i/640)%256;
}

static void choose_row(int row) {
    for(int n=0;n<row;++n) {
        events.push_back({10u+unsigned(n)*20,32});
        events.push_back({20u+unsigned(n)*20,0});
    }
    events.push_back({10u+unsigned(row)*20,1});
    events.push_back({20u+unsigned(row)*20,0});
}

static unsigned action_time;
static void press(uint16_t key) {
    events.push_back({action_time,key});
    events.push_back({action_time+10,0});
    action_time+=20;
}

int main() {
    std::remove("/tmp/lgames-menu-test.settings");
    void *mem=mmap(reinterpret_cast<void *>(0x10080000),0x300000,
                    PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(mem!=MAP_FAILED);
    for(unsigned i=0;i<3;++i) {
        auto *vt=reinterpret_cast<uint32_t *>(0x10080000u+i*0x100000u);
        vt[0]=0x20082000; vt[1]=0x10080101u+i*0x100000u;
    }
    for(int pass=0;pass<100;++pass) {
        setup();
        std::vector<uint8_t> expected(framebuffer,framebuffer+sizeof(framebuffer));

        events={{0,12},{5,0},{10,uint16_t(pass%2 ? 2:1)},{20,0}};
        rp_system_menu_sample(12);
        assert(rp_system_menu_poll()==20);
        assert(!paused && allocations==0);
        assert(std::equal(expected.begin(),expected.end(),framebuffer));
        assert(rp_system_menu_poll()==0);
    }
    setup(); fail_alloc=true; rp_system_menu_sample(12);
    assert(rp_system_menu_poll()==0 && allocations==0 && !paused);

    for(bool save:{false,true}) {
        setup(); action_time=10;
        for(int i=0;i<5;++i) press(32);
        press(1);
        if(save) {
            for(int i=0;i<6;++i) press(32);
            press(1);
        } else press(2);
        rp_system_menu_sample(12);
        assert(rp_system_menu_poll()>0);
        assert(enabled==!save && !paused && allocations==0);
        rp_system_menu_load_preferences();
        assert(enabled==!save);
    }
    setup(); action_time=10;
    for(int i=0;i<11;++i) press(32);
    press(128); press(128); press(1);
    press(64); press(64); press(1);
    rp_system_menu_sample(12);
    assert(rp_system_menu_poll()>0 && enabled && !paused && allocations==0);
    rp_system_menu_load_preferences(); assert(enabled);
    setup(); choose_row(1);
    screenshot_ms=1; screenshot_path="/tmp/lgames-rom-list.ppm";
    assert(rp_system_game_list()==1);

    for(int order=0;order<3;++order) {
        setup(); rp_system_menu_init("LGames");
        events={{10,uint16_t(order==0?12:order==1?4:8)},{15,12},{20,0},
                {30,2},{40,0},{50,32},{60,0},{70,1},{80,0}};
        screenshot_ms=25; screenshot_path="/tmp/lgames-settings.ppm";
        assert(rp_system_game_list()==1);
        assert(now_ms==80);
    }

    setup(); rp_system_menu_init("LGames");
    events={{10,4},{20,0},{30,8},{40,0},{50,32},{60,0},{70,1},{80,0}};
    assert(rp_system_game_list()==1 && now_ms==80);

    setup(); events={{10,2},{20,0}};
    assert(rp_system_game_list()==-1);

    setup(); rp_system_menu_init("LGames"); inject_fast_chord=true;
    events={{30,2},{40,0},{50,1},{60,0}};
    assert(rp_system_game_list()==0 && now_ms==60 && !inject_fast_chord);

    setup(); rp_system_menu_init("LGames");
    events={{10,12},{20,0},{30,1},{40,0}};
    assert(rp_system_game_list()==0);

    setup(); SDL_Init(0);
    events={{10,4},{20,0},{30,8},{40,0}};
    SDL_Event event{}; int keydowns=0,keyups=0;
    for(now_ms=1;now_ms<=45;++now_ms) {
        while(SDL_PollEvent(&event)) {
            assert(event.key.keysym.sym==SDLK_RETURN && event.key.keysym.scancode==4);
            assert(now_ms>=40);
            if(event.type==SDL_KEYDOWN) ++keydowns;
            if(event.type==SDL_KEYUP) ++keyups;
        }
    }
    assert(keydowns==1 && keyups==1);
    for(int order=0;order<3;++order) {
        setup();
        events={{10,uint16_t(order==0?12:order==1?4:8)},{15,12},{20,0},
                {30,2},{40,0}};
        std::vector<uint8_t> expected(framebuffer,framebuffer+sizeof(framebuffer));
        unsigned open_count=0;
        for(now_ms=1;now_ms<=45;++now_ms) {
            unsigned before=now_ms;
            assert(SDL_PollEvent(&event)==0);
            if(now_ms>before) ++open_count;
        }
        assert(open_count==1 && !paused && allocations==0);
        assert(std::equal(expected.begin(),expected.end(),framebuffer));
    }
    setup();
    events={{30,2},{40,0}};
    std::vector<uint8_t> expected(framebuffer,framebuffer+sizeof(framebuffer));
    usb_report(12); usb_report(0);
    assert(allocations==0 && !paused);
    assert(std::equal(expected.begin(),expected.end(),framebuffer));
    now_ms=1;
    assert(SDL_PollEvent(&event)==0 && now_ms==40);
    assert(allocations==0 && !paused);
    assert(std::equal(expected.begin(),expected.end(),framebuffer));
    for(now_ms=41;now_ms<45;++now_ms) assert(SDL_PollEvent(&event)==0);
    for(int slot=-1;slot<=1;++slot) {
        try {rp_system_launch(slot); assert(false);}
        catch(Reboot r) {assert(r.game==slot);}
        uint32_t token=rp_launch_token(slot);
        assert(rp_decode_launch(token,~token,true)==slot);
        assert(rp_decode_launch(token,~token,false)==-2);
        assert(rp_decode_launch(token,token,true)==-2);
    }
    assert(rp_decode_launch(0,0,true)==-2);
    assert(rp_decode_launch(0x4c474d33,~0x4c474d33u,true)==-2);
    auto *bad=reinterpret_cast<uint32_t *>(0x10180000u);
    bad[1]=0x10000101;
    rp_system_launch(0);
    munmap(mem,0x300000);
    std::puts("SYSTEM MENU PASS: real USB+SDL path, chord-only Settings in browser/game (all orders + fast reports), A-only launch, Select inert, Start game action retained, 100 restores, settings persistence, slot bounds");
}
