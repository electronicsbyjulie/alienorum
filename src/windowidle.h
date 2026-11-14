#pragma once

#include <SDL.h>

// Wait without rendering; SDL events from other threads also wake the caller.
bool wait_for_window_event(SDL_Window* window, SDL_Event* event);
