#include "input.h"

#include <stdio.h>
#include <string.h>

static SDL_Joystick *joy = NULL;
static int quit_combo_select = 0;
static int quit_combo_start  = 0;
static int hat_latched = 0;

enum {
    SDL_BTN_A      = 0,
    SDL_BTN_B      = 1,
    SDL_BTN_X      = 2,
    SDL_BTN_Y      = 3,
    SDL_BTN_L      = 4,
    SDL_BTN_R      = 5,
    SDL_BTN_SELECT = 8,
    SDL_BTN_START  = 9,
    SDL_BTN_MENU   = 10
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

InputAction input_poll_joystick(void)
{
    return ACTION_NONE;
}

static InputAction button_action(int button, int pressed)
{
    if (button == SDL_BTN_SELECT) quit_combo_select = pressed;
    if (button == SDL_BTN_START)  quit_combo_start  = pressed;

    if (!pressed) return ACTION_NONE;

    if (quit_combo_select && quit_combo_start) return ACTION_QUIT;

    switch (button) {
    case SDL_BTN_A:      return ACTION_PLAY;
    case SDL_BTN_B:      return ACTION_STOP;
    case SDL_BTN_X:      return ACTION_VOL_DOWN;
    case SDL_BTN_Y:      return ACTION_VOL_UP;
    case SDL_BTN_L:      return ACTION_PREV;
    case SDL_BTN_R:      return ACTION_NEXT;
    case SDL_BTN_START:  return ACTION_PAUSE;
    case SDL_BTN_MENU:   return ACTION_QUIT;
    default:
        printf("JOY unknown btn=%d\n", button);
        fflush(stdout);
        return ACTION_NONE;
    }
}

InputAction input_event_to_action(const SDL_Event *event)
{
    if (event->type == SDL_JOYBUTTONDOWN) {
        printf("JOY btn=%d down\n", event->jbutton.button);
        fflush(stdout);
        return button_action(event->jbutton.button, 1);
    }
    if (event->type == SDL_JOYBUTTONUP) {
        return button_action(event->jbutton.button, 0);
    }

    if (event->type == SDL_JOYHATMOTION) {
        Uint8 val = event->jhat.value;
        printf("JOY hat=%d\n", val);
        fflush(stdout);

        if (val == SDL_HAT_CENTERED) {
            hat_latched = 0;
            return ACTION_NONE;
        }
        if (hat_latched) return ACTION_NONE;

        hat_latched = 1;
        if (val & SDL_HAT_UP)    return ACTION_UP;
        if (val & SDL_HAT_DOWN)  return ACTION_DOWN;
        if (val & SDL_HAT_LEFT)  return ACTION_PREV;
        if (val & SDL_HAT_RIGHT) return ACTION_NEXT;
        return ACTION_NONE;
    }

    if (event->type == SDL_KEYDOWN) {
        int scan = (int)event->key.keysym.scancode;
        int sym  = (int)event->key.keysym.sym;
        printf("KEY scan=%d sym=%d\n", scan, sym);
        fflush(stdout);
        if (scan == 116 || sym == 117) return ACTION_QUIT; /* MENU/power */
        return ACTION_NONE;
    }

    if (event->type == SDL_QUIT) return ACTION_QUIT;
    return ACTION_NONE;
}
