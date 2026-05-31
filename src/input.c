#include "input.h"

#include <stdio.h>
#include <string.h>

static SDL_Joystick *joy = NULL;
static int hat_latched = 0;
static InputAction hat_hold_action = ACTION_NONE;
static Uint32 hat_next_repeat = 0;
static int input_debug = 0;
static int select_held = 0;
static int select_combo_used = 0;

#define HAT_REPEAT_DELAY_MS 360
#define HAT_REPEAT_RATE_MS 95

enum {
    SDL_BTN_A      = 0,
    SDL_BTN_B      = 1,
    SDL_BTN_X      = 2,
    SDL_BTN_Y      = 3,
    SDL_BTN_L      = 4,
    SDL_BTN_R      = 5,
    SDL_BTN_L2     = 6,
    SDL_BTN_SELECT = 7,
    SDL_BTN_START  = 8,
    SDL_BTN_MENU   = 9,
    SDL_BTN_VOL_UP = 10,
    SDL_BTN_VOL_DOWN = 11
};

void input_init(void)
{
    printf("Input mode: SDL joystick (rg35xx patched SDL)\n");
    fflush(stdout);

    if (SDL_NumJoysticks() > 0) {
        joy = SDL_JoystickOpen(0);
        if (joy) {
            printf("Joystick: %s buttons=%d axes=%d hats=%d\n",
                   SDL_JoystickName(0),
                   SDL_JoystickNumButtons(joy),
                   SDL_JoystickNumAxes(joy),
                   SDL_JoystickNumHats(joy));
            SDL_JoystickEventState(SDL_ENABLE);
            fflush(stdout);
        }
    } else {
        printf("No joysticks found\n");
        fflush(stdout);
    }
}

void input_shutdown(void)
{
    if (joy) {
        SDL_JoystickClose(joy);
        joy = NULL;
    }
}

void input_set_debug(int enabled)
{
    input_debug = enabled ? 1 : 0;
}

InputAction input_poll_joystick(void)
{
    Uint32 now;
    Uint8 hat;

    if (!joy || hat_hold_action == ACTION_NONE) {
        return ACTION_NONE;
    }

    SDL_JoystickUpdate();
    hat = SDL_JoystickGetHat(joy, 0);
    if (hat == SDL_HAT_CENTERED || ((hat & (SDL_HAT_UP | SDL_HAT_DOWN)) == 0)) {
        hat_hold_action = ACTION_NONE;
        hat_next_repeat = 0;
        return ACTION_NONE;
    }

    now = SDL_GetTicks();
    if (now >= hat_next_repeat) {
        hat_next_repeat = now + HAT_REPEAT_RATE_MS;
        return hat_hold_action;
    }

    return ACTION_NONE;
}

static InputAction button_action(int button, int pressed)
{
    if (button == SDL_BTN_SELECT) {
        if (pressed) {
            select_held = 1;
            select_combo_used = 0;
            return ACTION_NONE;
        }
        select_held = 0;
        return select_combo_used ? ACTION_NONE : ACTION_REPEAT_TOGGLE;
    }

    if (!pressed) return ACTION_NONE;

    switch (button) {
    case SDL_BTN_A:      return ACTION_PLAY;
    case SDL_BTN_B:      return ACTION_STOP;
    case SDL_BTN_X:      return ACTION_PAUSE;
    case SDL_BTN_Y:
        if (select_held) {
            select_combo_used = 1;
            return ACTION_FAVORITES_ONLY_TOGGLE;
        }
        return ACTION_FAVORITE_TOGGLE;
    case SDL_BTN_L:
    case SDL_BTN_L2:     return ACTION_VOL_DOWN;
    case SDL_BTN_R:      return ACTION_VOL_UP;
    case SDL_BTN_START:  return ACTION_SHUFFLE_PLAY;
    case SDL_BTN_MENU:   return ACTION_QUIT;
    case SDL_BTN_VOL_UP: return ACTION_VOL_UP;
    case SDL_BTN_VOL_DOWN: return ACTION_VOL_DOWN;
    default:
        if (input_debug) {
            printf("JOY unknown btn=%d\n", button);
            fflush(stdout);
        }
        return ACTION_NONE;
    }
}

InputAction input_event_to_action(const SDL_Event *event)
{
    if (event->type == SDL_JOYBUTTONDOWN) {
        if (input_debug) {
            printf("JOY btn=%d down\n", event->jbutton.button);
            fflush(stdout);
        }
        return button_action(event->jbutton.button, 1);
    }
    if (event->type == SDL_JOYBUTTONUP) {
        return button_action(event->jbutton.button, 0);
    }

    if (event->type == SDL_JOYHATMOTION) {
        Uint8 val = event->jhat.value;
        if (input_debug) {
            printf("JOY hat=%d\n", val);
            fflush(stdout);
        }

        if (val == SDL_HAT_CENTERED) {
            hat_latched = 0;
            hat_hold_action = ACTION_NONE;
            hat_next_repeat = 0;
            return ACTION_NONE;
        }
        if (hat_latched) return ACTION_NONE;

        hat_latched = 1;
        if (val & SDL_HAT_UP) {
            hat_hold_action = select_held ? ACTION_FOLDER_PREV : ACTION_UP;
            if (select_held) {
                select_combo_used = 1;
            }
            hat_next_repeat = SDL_GetTicks() + HAT_REPEAT_DELAY_MS;
            return hat_hold_action;
        }
        if (val & SDL_HAT_DOWN) {
            hat_hold_action = select_held ? ACTION_FOLDER_NEXT : ACTION_DOWN;
            if (select_held) {
                select_combo_used = 1;
            }
            hat_next_repeat = SDL_GetTicks() + HAT_REPEAT_DELAY_MS;
            return hat_hold_action;
        }
        if (val & SDL_HAT_LEFT) {
            if (select_held) {
                select_combo_used = 1;
                return ACTION_RECENT_PREV;
            }
            return ACTION_PREV;
        }
        if (val & SDL_HAT_RIGHT) {
            if (select_held) {
                select_combo_used = 1;
                return ACTION_RECENT_NEXT;
            }
            return ACTION_NEXT;
        }
        return ACTION_NONE;
    }

    if (event->type == SDL_KEYDOWN) {
        int scan = (int)event->key.keysym.scancode;
        int sym  = (int)event->key.keysym.sym;
        if (input_debug) {
            printf("KEY scan=%d sym=%d\n", scan, sym);
            fflush(stdout);
        }
        if (scan == 116 || sym == 117) return ACTION_QUIT; /* MENU/power */
        return ACTION_NONE;
    }

    if (event->type == SDL_QUIT) return ACTION_QUIT;
    return ACTION_NONE;
}
