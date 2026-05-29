#ifndef SDL_H_MINIMAL_12
#define SDL_H_MINIMAL_12

#include <stdint.h>

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

#define SDL_INIT_TIMER      0x00000001
#define SDL_INIT_AUDIO      0x00000010
#define SDL_INIT_VIDEO      0x00000020
#define SDL_INIT_JOYSTICK   0x00000200

#define SDL_SWSURFACE       0x00000000
#define SDL_DISABLE         0
#define SDL_ENABLE          1

#define SDL_BYTEORDER       1234
#define SDL_BIG_ENDIAN      4321

#define SDL_HAT_CENTERED    0x00
#define SDL_HAT_UP          0x01
#define SDL_HAT_RIGHT       0x02
#define SDL_HAT_DOWN        0x04
#define SDL_HAT_LEFT        0x08

typedef enum SDLKey {
    SDLK_BACKSPACE = 8,
    SDLK_RETURN = 13,
    SDLK_ESCAPE = 27,
    SDLK_SPACE = 32,
    SDLK_PLUS = 43,
    SDLK_MINUS = 45,
    SDLK_EQUALS = 61,
    SDLK_UP = 273,
    SDLK_DOWN = 274,
    SDLK_RIGHT = 275,
    SDLK_LEFT = 276,
    SDLK_q = 113,
    SDLK_p = 112
} SDLKey;

typedef struct SDL_PixelFormat {
    void *palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 Rloss;
    Uint8 Gloss;
    Uint8 Bloss;
    Uint8 Aloss;
    Uint8 Rshift;
    Uint8 Gshift;
    Uint8 Bshift;
    Uint8 Ashift;
    Uint32 Rmask;
    Uint32 Gmask;
    Uint32 Bmask;
    Uint32 Amask;
    Uint32 colorkey;
    Uint8 alpha;
} SDL_PixelFormat;

typedef struct SDL_Rect {
    Sint16 x;
    Sint16 y;
    Uint16 w;
    Uint16 h;
} SDL_Rect;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w;
    int h;
    Uint16 pitch;
    void *pixels;
    int offset;
    void *hwdata;
    SDL_Rect clip_rect;
    Uint32 unused1;
    Uint32 locked;
    void *map;
    unsigned int format_version;
    int refcount;
} SDL_Surface;

typedef struct SDL_Joystick SDL_Joystick;

typedef struct SDL_keysym {
    Uint8 scancode;
    SDLKey sym;
    Uint16 mod;
    Uint16 unicode;
} SDL_keysym;

typedef struct SDL_KeyboardEvent {
    Uint8 type;
    Uint8 which;
    Uint8 state;
    SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_JoyAxisEvent {
    Uint8 type;
    Uint8 which;
    Uint8 axis;
    Sint16 value;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyHatEvent {
    Uint8 type;
    Uint8 which;
    Uint8 hat;
    Uint8 value;
} SDL_JoyHatEvent;

typedef struct SDL_JoyButtonEvent {
    Uint8 type;
    Uint8 which;
    Uint8 button;
    Uint8 state;
} SDL_JoyButtonEvent;

typedef struct SDL_QuitEvent {
    Uint8 type;
} SDL_QuitEvent;

typedef union SDL_Event {
    Uint8 type;
    SDL_KeyboardEvent key;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyHatEvent jhat;
    SDL_JoyButtonEvent jbutton;
    SDL_QuitEvent quit;
} SDL_Event;

enum {
    SDL_KEYDOWN = 2,
    SDL_KEYUP = 3,
    SDL_QUIT = 12,
    SDL_JOYAXISMOTION = 7,
    SDL_JOYHATMOTION = 9,
    SDL_JOYBUTTONDOWN = 10,
    SDL_JOYBUTTONUP = 11
};

int SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
SDL_Surface *SDL_LoadBMP(const char *file);
void SDL_FreeSurface(SDL_Surface *surface);
void SDL_WM_SetCaption(const char *title, const char *icon);
int SDL_ShowCursor(int toggle);
int SDL_PollEvent(SDL_Event *event);
void SDL_Delay(Uint32 ms);
int SDL_Flip(SDL_Surface *screen);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);

int SDL_NumJoysticks(void);
const char *SDL_JoystickName(int device_index);
SDL_Joystick *SDL_JoystickOpen(int device_index);
void SDL_JoystickClose(SDL_Joystick *joystick);
int SDL_JoystickNumButtons(SDL_Joystick *joystick);
int SDL_JoystickNumAxes(SDL_Joystick *joystick);
int SDL_JoystickNumHats(SDL_Joystick *joystick);
int SDL_JoystickEventState(int state);
void SDL_JoystickUpdate(void);
Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button);
Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat);
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis);
Uint32 SDL_GetTicks(void);

#endif
