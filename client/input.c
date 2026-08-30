#include "input.h"

Direction input_handle_keydown(SDL_Keycode key) {
    switch (key) {
        case SDLK_UP:    case SDLK_w: return DIR_UP;
        case SDLK_DOWN:  case SDLK_s: return DIR_DOWN;
        case SDLK_LEFT:  case SDLK_a: return DIR_LEFT;
        case SDLK_RIGHT: case SDLK_d: return DIR_RIGHT;
        default: return DIR_NONE;
    }
}
