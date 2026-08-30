#ifndef INPUT_H
#define INPUT_H

#include "../protocol/protocol.h"
#include <SDL2/SDL.h>

/* Process an SDL keyboard event. Returns new direction or DIR_NONE. */
Direction input_handle_keydown(SDL_Keycode key);

#endif /* INPUT_H */
