
#ifndef LGAMES_PORT_MINI_SDL_H
#define LGAMES_PORT_MINI_SDL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef unsigned int uint;

typedef enum SDLKey {
    SDLK_UNKNOWN = 0,
    SDLK_BACKSPACE = 8, SDLK_TAB = 9, SDLK_RETURN = 13, SDLK_ESCAPE = 27,
    SDLK_SPACE = 32,
    SDLK_0 = '0', SDLK_1 = '1', SDLK_2 = '2', SDLK_3 = '3', SDLK_4 = '4',
    SDLK_5 = '5', SDLK_6 = '6', SDLK_7 = '7', SDLK_8 = '8', SDLK_9 = '9',
    SDLK_a = 'a', SDLK_b = 'b', SDLK_c = 'c', SDLK_d = 'd', SDLK_e = 'e',
    SDLK_f = 'f', SDLK_g = 'g', SDLK_h = 'h', SDLK_i = 'i', SDLK_j = 'j',
    SDLK_k = 'k', SDLK_l = 'l', SDLK_m = 'm', SDLK_n = 'n', SDLK_o = 'o',
    SDLK_p = 'p', SDLK_q = 'q', SDLK_r = 'r', SDLK_s = 's', SDLK_t = 't',
    SDLK_u = 'u', SDLK_v = 'v', SDLK_w = 'w', SDLK_x = 'x', SDLK_y = 'y',
    SDLK_z = 'z', SDLK_DELETE = 127,
    SDLK_KP0 = 256, SDLK_KP1 = 257, SDLK_KP2 = 258, SDLK_KP3 = 259,
    SDLK_KP4 = 260, SDLK_KP5 = 261, SDLK_KP6 = 262, SDLK_KP7 = 263,
    SDLK_KP8 = 264, SDLK_KP9 = 265,
    SDLK_UP = 273, SDLK_DOWN = 274, SDLK_RIGHT = 275, SDLK_LEFT = 276,
    SDLK_INSERT = 277, SDLK_HOME = 278, SDLK_END = 279,
    SDLK_PAGEUP = 280, SDLK_PAGEDOWN = 281,
    SDLK_F1 = 282, SDLK_F5 = 286,
    SDLK_RSHIFT = 303, SDLK_LSHIFT = 304, SDLK_RCTRL = 305,
    SDLK_LCTRL = 306, SDLK_RALT = 307, SDLK_LALT = 308,
    SDLK_LAST = 323
} SDLKey;

typedef Uint16 SDLMod;
#define KMOD_NONE 0x0000
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002

typedef struct SDL_Color { Uint8 r, g, b, unused; } SDL_Color;
typedef struct SDL_Palette { int ncolors; SDL_Color *colors; } SDL_Palette;

typedef struct SDL_PixelFormat {
    SDL_Palette *palette;
    Uint8 BitsPerPixel, BytesPerPixel, Rloss, Gloss, Bloss, Aloss;
    Uint8 Rshift, Gshift, Bshift, Ashift;
    Uint32 Rmask, Gmask, Bmask, Amask, colorkey;
    Uint8 alpha;
} SDL_PixelFormat;

typedef struct SDL_Rect { Sint16 x, y; Uint16 w, h; } SDL_Rect;
typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    Uint16 pitch;
    void *pixels;
    SDL_Rect clip_rect;
    int refcount;
} SDL_Surface;

typedef struct SDL_keysym { Uint8 scancode; SDLKey sym; SDLMod mod; Uint16 unicode; } SDL_keysym;
typedef struct SDL_ActiveEvent { Uint8 type, gain, state; } SDL_ActiveEvent;
typedef struct SDL_KeyboardEvent { Uint8 type, which, state; SDL_keysym keysym; } SDL_KeyboardEvent;
typedef struct SDL_MouseMotionEvent { Uint8 type, which, state; Uint16 x, y; Sint16 xrel, yrel; } SDL_MouseMotionEvent;
typedef struct SDL_MouseButtonEvent { Uint8 type, which, button, state; Uint16 x, y; } SDL_MouseButtonEvent;
typedef struct SDL_JoyButtonEvent { Uint8 type, which, button, state; } SDL_JoyButtonEvent;
typedef struct SDL_QuitEvent { Uint8 type; } SDL_QuitEvent;
typedef union SDL_Event {
    Uint8 type;
    SDL_ActiveEvent active;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_JoyButtonEvent jbutton;
    SDL_QuitEvent quit;
} SDL_Event;

typedef int (*SDL_EventFilter)(const SDL_Event *event);
typedef struct SDL_Cursor { int unused; } SDL_Cursor;
typedef struct SDL_Joystick { int unused; } SDL_Joystick;
typedef struct SDL_RWops { void *hidden; } SDL_RWops;
typedef struct SDL_VideoInfo {
    Uint32 hw_available, wm_available, blit_hw, blit_hw_CC, blit_hw_A;
    Uint32 blit_sw, blit_sw_CC, blit_sw_A, blit_fill;
    Uint32 video_mem;
    int current_w, current_h;
    SDL_PixelFormat *vfmt;
} SDL_VideoInfo;

#define SDL_NOEVENT 0
#define SDL_ACTIVEEVENT 1
#define SDL_KEYDOWN 2
#define SDL_KEYUP 3
#define SDL_MOUSEMOTION 4
#define SDL_MOUSEBUTTONDOWN 5
#define SDL_MOUSEBUTTONUP 6
#define SDL_JOYBUTTONDOWN 10
#define SDL_QUIT 12

#define SDL_RELEASED 0
#define SDL_PRESSED 1
#define SDL_APPACTIVE 0x04
#define SDL_APPINPUTFOCUS 0x02

#define SDL_INIT_TIMER 0x00000001u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_JOYSTICK 0x00000200u
#define SDL_INIT_EVERYTHING 0x0000FFFFu

#define SDL_SWSURFACE 0x00000000u
#define SDL_HWSURFACE 0x00000001u
#define SDL_DOUBLEBUF 0x40000000u
#define SDL_FULLSCREEN 0x80000000u
#define SDL_SRCCOLORKEY 0x00001000u
#define SDL_SRCALPHA 0x00010000u
#define SDL_ALPHA_TRANSPARENT 0
#define SDL_ALPHA_OPAQUE 255
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#define SDL_ENABLE 1
#define SDL_IGNORE 0
#define SDL_QUERY -1
#define SDL_GRAB_QUERY -1
#define SDL_GRAB_OFF 0
#define SDL_GRAB_ON 1
#define SDL_BUTTON(X) (1u << ((X)-1))
#define SDL_MUSTLOCK(S) 0

int SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
void SDL_SetError(const char *fmt, ...);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
SDL_Surface *SDL_GetVideoSurface(void);
const SDL_VideoInfo *SDL_GetVideoInfo(void);
int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
SDL_Surface *SDL_AllocSurface(Uint32 flags, int width, int height, int depth,
                              Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface);
SDL_Surface *SDL_ConvertSurface(SDL_Surface *surface, SDL_PixelFormat *format, Uint32 flags);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key);
int SDL_SetAlpha(SDL_Surface *surface, Uint32 flag, Uint8 alpha);
int SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
SDL_Surface *SDL_LoadBMP(const char *path);
SDL_Surface *SDL_LoadPNG(const char *path);
int SDL_SaveBMP(SDL_Surface *surface, const char *path);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);
void SDL_UpdateRectAtomic(SDL_Surface *screen, Sint32 x, Sint32 y,
                          Uint32 w, Uint32 h);
int SDL_Flip(SDL_Surface *screen);
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
void SDL_PumpEvents(void);
Uint8 *SDL_GetKeyState(int *numkeys);
SDL_EventFilter SDL_SetEventFilter(SDL_EventFilter filter);
SDL_EventFilter SDL_GetEventFilter(void);
Uint8 SDL_EventState(Uint8 type, int state);
SDLMod SDL_GetModState(void);
const char *SDL_GetKeyName(SDLKey key);
int SDL_EnableUNICODE(int enable);
Uint8 SDL_GetMouseState(int *x, int *y);
Uint8 SDL_GetRelativeMouseState(int *x, int *y);
void SDL_WarpMouse(Uint16 x, Uint16 y);
int SDL_ShowCursor(int toggle);
int SDL_WM_GrabInput(int mode);
void SDL_WM_SetCaption(const char *title, const char *icon);
void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask);
SDL_Cursor *SDL_CreateCursor(Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y);
void SDL_FreeCursor(SDL_Cursor *cursor);
SDL_Cursor *SDL_GetCursor(void);
void SDL_SetCursor(SDL_Cursor *cursor);
int SDL_NumJoysticks(void);
SDL_Joystick *SDL_JoystickOpen(int index);
void SDL_JoystickClose(SDL_Joystick *joy);
int SDL_JoystickEventState(int state);
void SDL_JoystickUpdate(void);
int SDL_JoystickNumAxes(SDL_Joystick *joy);
int SDL_JoystickNumBalls(SDL_Joystick *joy);
int SDL_JoystickNumButtons(SDL_Joystick *joy);
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joy, int axis);
Uint8 SDL_JoystickGetButton(SDL_Joystick *joy, int button);

void rp_sdl_shade_rect(SDL_Surface *dst, const SDL_Rect *rect, Uint16 color, Uint8 opacity);

int rp_sdl_load_background(const char *path, SDL_Surface *dst);

int rp_sdl_take_background(const char *path, SDL_Surface *dst);
void rp_sdl_blit_background(SDL_Surface *src, SDL_Surface *dst);

int rp_sdl_background_cache_begin(const char *directory, int width, int height);
int rp_sdl_background_cache_step(void);
void rp_sdl_background_cache_reset(void);
void rp_sdl_sram_checkpoint(unsigned fragments);

#ifdef __cplusplus
}
#endif
#endif
