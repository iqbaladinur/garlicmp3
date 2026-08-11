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
static Uint32 axis_fire_tick[2] = {0, 0}; /* L2 = axis 2, R2 = axis 5 */

#define HAT_REPEAT_DELAY_MS 360
#define HAT_REPEAT_RATE_MS 95
/* L2/R2 are DIGITAL switches wired to axis slots: pressing bounces the value
 * +32767/-32768 and release never returns to neutral — so value-edge/latch
 * detection can't work. Debounce on TIME instead: each physical press emits a
 * burst of events; only the first event of a burst fires. */
#define AXIS_EVENT_THRESHOLD 10000
#define AXIS_DEBOUNCE_MS 300
/* The axis value persists at +/-32767 even after release, so SDL emits a stale
 * JOYAXISMOTION event right after open. Ignore axis events during a startup
 * grace window or the app would launch into the toggled view. */
#define AXIS_STARTUP_GRACE_MS 500

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
    SDL_BTN_VOL_DOWN = 11,
    SDL_BTN_R2     = 12 /* NOTE: dead — device exposes L2/R2 as AXES (2/5), not buttons */
};

void input_init(void)
{
    printf("Input mode: SDL joystick (rg35xx patched SDL)\n");
    fflush(stdout);

    if (SDL_NumJoysticks() > 0) {
        joy = SDL_JoystickOpen(0);
        if (joy) {
            Uint32 grace = SDL_GetTicks() + AXIS_STARTUP_GRACE_MS;
            axis_fire_tick[0] = grace;
            axis_fire_tick[1] = grace;
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

    if (!joy) {
        return ACTION_NONE;
    }

    /* Keep raw joystick state fresh every frame so SDL emits JOYAXISMOTION
     * events for L2/R2 (handled with time-debounce in input_event_to_action). */
    SDL_JoystickUpdate();

    if (hat_hold_action == ACTION_NONE) {
        return ACTION_NONE; /* initial hat press handled via SDL_JOYHATMOTION event */
    }

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
    case SDL_BTN_A:
        if (select_held) {
            select_combo_used = 1;
            return ACTION_SETTINGS_TOGGLE;
        }
        return ACTION_PLAY;
    case SDL_BTN_B:      return ACTION_STOP;
    case SDL_BTN_X:
        if (select_held) {
            select_combo_used = 1;
            return ACTION_VIEW_TOGGLE;
        }
        return ACTION_PAUSE;
    case SDL_BTN_Y:
        if (select_held) {
            select_combo_used = 1;
            return ACTION_FAVORITES_ONLY_TOGGLE;
        }
        return ACTION_FAVORITE_TOGGLE;
    case SDL_BTN_L:      return ACTION_VOL_DOWN;
    case SDL_BTN_R:      return ACTION_VOL_UP;
    case SDL_BTN_START:
        if (select_held) {
            select_combo_used = 1;
            return ACTION_HELP_TOGGLE;
        }
        return ACTION_SHUFFLE_PLAY;
    case SDL_BTN_MENU:   return ACTION_QUIT;
    case SDL_BTN_VOL_UP: return ACTION_VOL_UP;
    case SDL_BTN_VOL_DOWN: return ACTION_VOL_DOWN;
    case SDL_BTN_R2:     return ACTION_SETTINGS_TOGGLE;
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

    if (event->type == SDL_JOYAXISMOTION) {
        int axis = event->jaxis.axis;
        int value = event->jaxis.value;
        int idx = -1;
        Uint32 now;

        if (input_debug) {
            printf("JOY axis=%d value=%d\n", axis, value);
            fflush(stdout);
        }

        /* L2/R2 = digital switches on axis slots: press bounces +/-32767,
         * release returns NO neutral value. Debounce on TIME — a physical
         * press is a burst of events; fire only the first event of a burst. */
        if (axis == 2) {
            idx = 0; /* L2 */
        } else if (axis == 5) {
            idx = 1; /* R2 */
        }
        if (idx < 0) {
            return ACTION_NONE;
        }
        if (value < -AXIS_EVENT_THRESHOLD || value > AXIS_EVENT_THRESHOLD) {
            now = SDL_GetTicks();
            if ((int)(now - axis_fire_tick[idx]) >= AXIS_DEBOUNCE_MS) {
                axis_fire_tick[idx] = now;
                return (idx == 0) ? ACTION_VIEW_TOGGLE : ACTION_SETTINGS_TOGGLE;
            }
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
