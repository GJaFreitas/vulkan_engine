#include "world.h"

// TODO: When a main menu is added remember to change this
EngineMode	g_engine_mode = ENGINE_MODE_GAME;
InputState	g_input_state;

// Default keybinds
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
	memcpy(g_input_state.current, g_input_state.last, NUM_KEYS);
}

void	inputEndFrame(void)
{
	g_input_state.mod = SDL_GetModState();
}

void	inputProccessEvent(const SDL_Event *e)
{
	switch (e->type)
	{
		case SDL_EVENT_KEY_DOWN:
			g_input_state.current[e->key.scancode] = true;
		break;
		case SDL_EVENT_KEY_UP:
			g_input_state.current[e->key.scancode] = false;
		break;
		default: break;
	}
}
