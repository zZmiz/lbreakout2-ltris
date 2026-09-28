
#include "rp_platform.h"
#include "hstx.h"
#include "pico/stdlib.h"
#include "system_font_8x8.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
bool pending, chord_down, active;
int current_game = -1;
constexpr uint32_t bases[] = {0x10080000u, 0x10180000u, 0x10280000u};
constexpr uint16_t A=1, B=2, SELECT=4, START=8, UP=16, DOWN=32, LEFT=64, RIGHT=128;
struct Preferences { unsigned audio=1, fg=15, bg=48; int recent=-1; } prefs;
#ifndef RP_SYSTEM_SETTINGS_PATH
#define RP_SYSTEM_SETTINGS_PATH "/games/lgames.settings"
#endif
constexpr const char *settings_path=RP_SYSTEM_SETTINGS_PATH;
constexpr const char *games[]={"LTris", "LBreakout2"};
constexpr uint32_t palette[]={
    0x626262,0x001C95,0x1904AC,0x42009D,0x61006B,0x6E0025,0x650500,0x491E00,
    0x223700,0x004900,0x004F00,0x004816,0x00355E,0,0,0,
    0xABABAB,0x0C4EDB,0x3D2EFF,0x7115F3,0x9B0BB9,0xB01262,0xA92704,0x894600,
    0x576600,0x237F00,0x008900,0x008332,0x006D90,0,0,0,
    0xFFFFFF,0x57A5FF,0x8287FF,0xB46DFF,0xDF60FF,0xF863C6,0xF8746D,0xDE9020,
    0xB3AE00,0x81C800,0x56D522,0x3DD36F,0x3EC1C8,0x4E4E4E,0,0,
    0xFFFFFF,0xBEE0FF,0xCDD4FF,0xE0CAFF,0xF1C4FF,0xFCC4EF,0xFDCACE,0xF5D4AF,
    0xE6DF9C,0xD3E99A,0xC2EFA8,0xB7EFC4,0xB6EAE5,0xB8B8B8,0,0};
uint8_t color(unsigned i) {
    uint32_t c=palette[i&63];
    return ((c>>16)&0xe0)|((c>>11)&0x1c)|((c>>6)&3);
}
bool save_preferences(const Preferences &p) {
    FILE *f=std::fopen(settings_path,"w");
    if (!f) return false;
    bool ok=std::fprintf(f,"LGAMES1 %u %u %u %d\n",p.audio,p.fg,p.bg,p.recent)>0;
    if (std::fclose(f)) ok=false;
    return ok;
}

void text(int col, int row, const char *s, uint8_t fg=0, uint8_t bg=255)
{
    uint8_t *fb = hstx_getframebuffer();
    for (; *s && col < 40; ++s, ++col) {
        unsigned ch = static_cast<unsigned char>(*s);
        if (ch < 32 || ch > 126) ch = '?';
        for (int y=0; y<8; ++y) {
            uint8_t bits = font_8x8[ch-32+y*95];
            for (int x=0; x<8; ++x) {
                uint8_t c = (bits & (1u<<x)) ? fg : bg;
                int off=(row*16+y*2)*640+col*16+x*2;
                fb[off]=fb[off+1]=fb[off+640]=fb[off+641]=c;
            }
        }
    }
}

void row(int y,const char *s,bool selected,uint8_t fg=0,uint8_t bg=255)
{
    if (selected) {uint8_t t=fg; fg=bg; bg=t;}
    std::memset(hstx_getframebuffer()+y*16*640,bg,16*640);
    text(0,y,s,fg,bg);
}

void header(const char *title,uint8_t fg=0,uint8_t bg=255)
{
    hstx_waitForVSync();
    std::memset(hstx_getframebuffer(),bg,640*480);
    text((40-static_cast<int>(std::strlen(title)))/2,0,title,fg,bg);
    text(36,0,"HDMI",fg,bg);
}

void draw_list(int selected)
{
    uint8_t fg=color(prefs.fg),bg=color(prefs.bg);
    header("- LTris / LBreakout2 -",fg,bg);
    text(0,0,"252MHz",fg,bg);
    text(10,1,"Choose a rom to play:",fg,bg);
    text(0,2,"----------------------------------------",fg,bg);
    row(3,"R LTris",selected==0,fg,bg);
    row(4,"R LBreakout2",selected==1,fg,bg);
    text(0,25,"----------------------------------------",fg,bg);
    text(0,26,"A:Open B:Back",fg,bg);
    text(0,27,"SELECT+START:Settings",fg,bg);
    text(0,29,"P8192K L",fg,bg);
    uint32_t id=rp_usb_identity();
    if(id) {
        char s[12]; std::snprintf(s,sizeof(s),"%04lx:%04lx",static_cast<unsigned long>(id>>16),
                                 static_cast<unsigned long>(id&0xffff));
        text(16,29,s,color(1),bg);
    }
    text(29,29,__DATE__,fg,bg);
}

uint16_t pad()
{
    rp_platform_service();
    return rp_platform_read_pad(0) | rp_platform_read_pad(1);
}

void released()
{
    while (pad()) sleep_ms(1);
    pending=false;
}

void controller_test()
{
    header("-- Controller Test --");
    text(0,27,"Press B to go back.");
    released();
    uint16_t previous=0xffff;
    for (;;) {
        uint16_t now=pad();
        if (now&B) {released(); return;}
        if (now!=previous) {
            constexpr const char *labels[]={"A","B","Select","Start","Up","Down","Left","Right","X","Y","L","R"};
            for(int i=0;i<12;++i) row(3+i,labels[i],now&(1u<<i));
            previous=now;
        }
        sleep_ms(1);
    }
}

int settings(bool in_game)
{
    Preferences working=prefs;
    enum Action {RESUME,RECENT,QUIT,RESET,LAUNCHER,SCREEN,AUDIO,DISPLAY,FG,BG,BOOTSEL,CONTROLLER};
    Action actions[12]; int count=0;
    if(in_game) {actions[count++]=RESUME; actions[count++]=QUIT; actions[count++]=RESET;}
    else actions[count++]=RECENT;
    actions[count++]=LAUNCHER;
    actions[count++]=SCREEN; actions[count++]=AUDIO; actions[count++]=DISPLAY;
    actions[count++]=FG; actions[count++]=BG;
    actions[count++]=BOOTSEL; actions[count++]=CONTROLLER;
    int selected=0,button=0;
    const char *message=nullptr;
    auto draw=[&]() {
        header("-- Settings --");
        for(int i=0;i<count;++i) {
            char s[41];
            switch(actions[i]) {
            case RESUME: std::snprintf(s,sizeof(s),"Return to game"); break;
            case RECENT: std::snprintf(s,sizeof(s),"Recently played"); break;
            case QUIT: std::snprintf(s,sizeof(s),"Quit game"); break;
            case RESET: std::snprintf(s,sizeof(s),"Reset game"); break;
            case LAUNCHER: std::snprintf(s,sizeof(s),"Return to emulator selection"); break;
            case AUDIO: std::snprintf(s,sizeof(s),"Audio enabled: %s",working.audio?"ON":"OFF"); break;
            case SCREEN: std::snprintf(s,sizeof(s),"Screen Mode: Native 640x480 (fixed)"); break;
            case DISPLAY: std::snprintf(s,sizeof(s),"Display Mode: HDMI (fixed)"); break;
            case FG: std::snprintf(s,sizeof(s),"Menu Font Color: %u",working.fg); break;
            case BG: std::snprintf(s,sizeof(s),"Menu Font Back Color: %u",working.bg); break;
            case BOOTSEL: std::snprintf(s,sizeof(s),"Enter BOOTSEL Mode"); break;
            case CONTROLLER: std::snprintf(s,sizeof(s),"Controller Test"); break;
            }
            row(3+i,s,selected==i);
        }
        const char *buttons[]={"SAVE","CANCEL","DEFAULT"};
        for(int i=0;i<3;++i) text(i*9,17,buttons[i],selected==count&&button==i?255:0,
                                                          selected==count&&button==i?0:255);
        for(int y=0;y<4;++y) {
            char s[4]; std::snprintf(s,sizeof(s),"%02d",y*16); text(0,19+y,s);
            for(int x=0;x<16;++x) text(3+x,19+y," ",0,color(y*16+x));
        }
        char sample[20]; std::snprintf(sample,sizeof(sample),"FG=%u BG=%u",working.fg,working.bg);
        text(22,20,sample,color(working.fg),color(working.bg));
        const char *help="Save or cancel changes";
        if(selected<count) switch(actions[selected]) {
        case RESUME: help="Continue the paused game"; break;
        case RECENT: help="Restart the last played game"; break;
        case QUIT: help="Return to the ROM list"; break;
        case RESET: help="Restart the current game"; break;
        case LAUNCHER: help="Return to the emulator launcher"; break;
        case AUDIO: help="Enable HDMI and Audio sound"; break;
        case SCREEN: help="Native games use full-resolution output"; break;
        case DISPLAY: help="This game port currently supports HDMI"; break;
        case FG: case BG: help="LEFT/RIGHT: change menu color"; break;
        case BOOTSEL: help="Reboot into USB firmware update"; break;
        case CONTROLLER: help="Test controller buttons"; break;
        }
        text(0,24,message?message:help);
        text(0,27,"UP/DOWN: Move, A: select");
        text(0,29,"Press B to go back.");
    };
    std::printf("[system_menu] open from %s\n",in_game?"game":"ROM list");
    draw(); released();
    uint16_t previous=0;
    for (;;) {
        uint16_t now=pad(), edge=now & ~previous;
        previous=now;
        if (edge&B) {released(); return -1;}
        if(edge&UP) selected=(selected+count)%(count+1);
        if(edge&DOWN) selected=(selected+1)%(count+1);
        if(selected==count) {
            if(edge&LEFT) button=(button+2)%3;
            if(edge&RIGHT) button=(button+1)%3;
            if(edge&A) {
                if(button==1) {released(); return -1;}
                if(button==2) {working=Preferences{}; working.recent=prefs.recent;}
                if(button==0) {
                    if(color(working.fg)==color(working.bg)) message="Choose different text/background colors";
                    else if(save_preferences(working)) {
                        prefs=working; rp_audio_system_enable(prefs.audio); released(); return -1;
                    } else message="Settings save failed; B to cancel";
                }
            }
        } else if(edge&(A|LEFT|RIGHT)) {
            int delta=(edge&LEFT)?63:1;
            switch(actions[selected]) {
            case AUDIO: working.audio^=1; break;
            case FG: working.fg=(working.fg+delta)%64; message=nullptr; break;
            case BG: working.bg=(working.bg+delta)%64; message=nullptr; break;
            default:
                if(!(edge&A)) break;
                released(); previous=0;
                switch(actions[selected]) {
                case RESUME: return -1;
                case RECENT:
                    if(prefs.recent>=0) return prefs.recent;
                    message="No recently played game yet"; break;
                case QUIT: rp_system_launch(-1); break;
                case RESET: rp_system_launch(current_game); break;
                case LAUNCHER: rp_exit(0); break;
                case BOOTSEL: rp_platform_bootsel(); break;
                case CONTROLLER: controller_test(); break;
                default: break;
                }
            }
        }
        if(edge) draw();
        sleep_ms(1);
    }
}
}

extern "C" void rp_system_menu_load_preferences(void)
{
    prefs=Preferences{};
    FILE *f=std::fopen(settings_path,"r");
    if(f) {

        char line[96];
        if(std::fgets(line,sizeof(line),f) && !std::strncmp(line,"LGAMES1 ",8)) {
            char *cursor=line+8,*end;
            long v[4]; bool valid=true;
            for(int i=0;i<4;++i) {
                v[i]=std::strtol(cursor,&end,10);
                if(end==cursor) valid=false;
                cursor=end;
            }
            if(valid && v[0]>=0 && v[0]<=1 && v[1]>=0 && v[1]<64 &&
               v[2]>=0 && v[2]<64 && v[3]>=-1 && v[3]<=1 && color(v[1])!=color(v[2]))
                prefs={unsigned(v[0]),unsigned(v[1]),unsigned(v[2]),int(v[3])};
        }
        std::fclose(f);
    }
    rp_audio_system_enable(prefs.audio);
}

extern "C" void rp_system_menu_init(const char *program)
{
    current_game=!std::strcmp(program,"LTris") ? 0 :
                 (!std::strcmp(program,"LBreakout2") ? 1 : -1);
    pending=chord_down=active=false;
}

extern "C" void rp_system_menu_sample(uint16_t state)
{
    bool down=(state & (SELECT|START))==(SELECT|START);
    if (down && !chord_down && !active) pending=true;
    chord_down=down;
}

extern "C" void rp_system_launch(int game)
{
#ifdef RP_LGAMES_CONTAINER
    if (game < -1 || game > 1) return;
    uint32_t base=bases[game+1];
    const volatile uint32_t *vt=reinterpret_cast<const volatile uint32_t *>(base);
    uint32_t sp=vt[0], pc=vt[1];
    if (sp < 0x20000000u || sp > 0x20082000u || (sp & 7u) ||
        pc < base || pc >= base+0x100000u || !(pc&1u)) {
        std::printf("[lgames] invalid slot %d vectors %08lx %08lx\n",game,
                    static_cast<unsigned long>(sp),static_cast<unsigned long>(pc));
        return;
    }
    std::printf("[lgames] request loader resume to slot %d at %08lx\n",game,static_cast<unsigned long>(base));
    stdio_flush();
    rp_platform_launch_slot(game);
#else
    (void)game;
    rp_exit(0);
#endif
}

extern "C" int rp_system_game_list(void)
{
    active=false;
    int selected=0;
    draw_list(selected); released();
    uint16_t previous=0;
    uint32_t usb_id=rp_usb_identity();
    for (;;) {
        uint16_t now=pad(),edge=now&~previous;
        previous=now;
        if(usb_id!=rp_usb_identity()) {usb_id=rp_usb_identity(); draw_list(selected);}

        if(pending) {
            pending=false; active=true;
            int recent=settings(false);
            active=false;
            if(recent>=0) {active=false; return recent;}
            draw_list(selected); previous=0;
        } else if(edge&B) {released(); active=false; return -1;}
        else if(edge&A) {
            released(); active=false;
            prefs.recent=selected; save_preferences(prefs);
            std::printf("[lgames] selected %s\n",games[selected]);
            return selected;
        } else {
            if(edge&(UP|DOWN)) {selected^=1; draw_list(selected);}
        }
        sleep_ms(1);
    }
}

extern "C" uint32_t rp_system_menu_poll(void)
{
    if (!pending || active) return 0;
    pending=false;
    active=true;
    uint32_t started=time_us_32();
    uint8_t *saved=static_cast<uint8_t *>(rp_psram_malloc(640*480));
    if (!saved) {
        std::printf("[system_menu] snapshot allocation failed\n");
        active=false;
        return 0;
    }
    rp_audio_system_pause(1);
    std::memcpy(saved,hstx_getframebuffer(),640*480);
    settings(true);

    for (int y=0; y<480; ++y) {
        std::memcpy(hstx_getframebuffer()+y*640,saved+y*640,640);
        if ((y&15)==0) rp_platform_service();
    }
    rp_psram_free(saved);
    rp_audio_system_pause(0);
    pending=false;
    active=false;
    uint32_t elapsed=(time_us_32()-started)/1000;
    std::printf("[system_menu] resumed, paused_ms=%lu\n",static_cast<unsigned long>(elapsed));
    return elapsed ? elapsed : 1;
}

extern "C" void rp_system_game_finished(void)
{
    rp_system_launch(-1);
    rp_exit(0);
}
