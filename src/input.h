#ifndef INPUT_H
#define INPUT_H

#include <SDL/SDL.h>

typedef enum InputAction {
    ACTION_NONE = 0,
    ACTION_UP,
    ACTION_DOWN,
    ACTION_PLAY,
    ACTION_STOP,
    ACTION_PAUSE,
    ACTION_SHUFFLE_PLAY,
    ACTION_REPEAT_TOGGLE,
    ACTION_PREV,
    ACTION_NEXT,
    ACTION_VOL_DOWN,
    ACTION_VOL_UP,
    ACTION_QUIT
} InputAction;

void input_init(void);
void input_shutdown(void);
InputAction input_poll_joystick(void);
InputAction input_event_to_action(const SDL_Event *event);

#endif
