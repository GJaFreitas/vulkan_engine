#include "world.h"

// TODO: When a main menu is added remember to change this
EngineMode	g_engine_mode = ENGINE_MODE_GAME;
InputState	g_input_state;

// Default keybinds
// TODO: Chnage this to reading from a file
SDL_Scancode	g_keybinds[ACTION_MAX_ENUM] = {
	[ ACTION_MOVE_FORWARD ] = SDL_SCANCODE_W,
	[ ACTION_MOVE_LEFT ] = SDL_SCANCODE_A,
	[ ACTION_MOVE_RIGHT ] = SDL_SCANCODE_D,
	[ ACTION_MOVE_BACKWARD ] = SDL_SCANCODE_S,
	[ ACTION_JUMP ] = SDL_SCANCODE_SPACE,
	[ ACTION_SHIFT ] = SDL_SCANCODE_LSHIFT,
	[ ACTION_DEBUG_TOGGLE ] = SDL_SCANCODE_F3,
	[ ACTION_CONSOLE_TOGGLE ] = SDL_SCANCODE_GRAVE,
};

void	inputBeginFrame(void)
{
	int		numkeys;
	const bool	*state = SDL_GetKeyboardState(&numkeys);

	// 1. Backup the last frame for key_pressed / key_released logic
	memcpy(g_input_state.last, g_input_state.current, NUM_KEYS);
	// 2. Overwrite the current frame with the true OS physical state
	memcpy(g_input_state.current, state, (numkeys < NUM_KEYS) ? numkeys : NUM_KEYS);
}

void	inputEndFrame(void)
{
	g_input_state.mod = SDL_GetModState();
}
