#include "world.h"

void	createPBRPipeline(GraphicsContext *ctx);

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

	// --- I DONT KNOW WHAT TO CALL IT --- //
	[ ACTION_DEBUG_TOGGLE ] = SDL_SCANCODE_F3,
	[ ACTION_EDITOR_TOGGLE ] = SDL_SCANCODE_F2,
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

	// Reset scroll every frame so it doesn't spin forever
	g_input_state.mouse_wheel_y = 0.0f;
}

void	inputEndFrame(void)
{
	g_input_state.mod = SDL_GetModState();
}

static void resolveMouseState(SDL_Window *window) {
	bool	should_lock = false;

	// 1. Highest Priority: Overlays
	if (gameStateQuery(g_game_state, ShowConsole)) {
		should_lock = false; // Free mouse for text selection / UI
	} 
	// 2. Base Modes
	else if (g_engine_mode == ENGINE_MODE_GAME) {
		should_lock = true;  // Always locked in gameplay
	} 
	else if (g_engine_mode == ENGINE_MODE_EDITOR) {
		// Hybrid Editor Mouse: Lock only while Right Mouse Button is held
		u32	mouse_state = SDL_GetMouseState(NULL, NULL);
		if (mouse_state & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) {
			should_lock = true;
		} else {
			should_lock = false;
		}
	} 
	else if (g_engine_mode == ENGINE_MODE_MAIN_MENU) {
		should_lock = false; // Always free in menus
	}

	// SDL internally checks if the state is already set, so this is safe to call per-frame
	SDL_SetWindowRelativeMouseMode(window, should_lock);
}

void	processEvents(World *world) {
	SDL_Event	event = {0};
	bool		consumed;

	while (SDL_PollEvent(&event))
	{
		consumed = false;

		// ---------------------------------------------------------
		// 1. GLOBAL / SYSTEM EVENTS (Always process these)
		// ---------------------------------------------------------
		if (event.type == SDL_EVENT_QUIT) {
			gameStateToggle(&g_game_state, Running);
			continue; 
		}
		if (event.type == SDL_EVENT_WINDOW_RESIZED) {
			world->graphics_ctx->window_width = event.window.data1;
			world->graphics_ctx->window_height = event.window.data2;
			world->graphics_ctx->swapchain_require_recreate = true;
			continue;
		}
		if (event.type == SDL_EVENT_MOUSE_WHEEL) {
			g_input_state.mouse_wheel_y = event.wheel.y;
			continue;
		}

		// ---------------------------------------------------------
		// 2. CONTEXT TOGGLES (Highest priority input)
		// ---------------------------------------------------------
		if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
			// --- TOGGLE CONSOLE ---
			if (event.key.scancode == g_keybinds[ACTION_CONSOLE_TOGGLE]) {
				gameStateToggle(&g_game_state, ShowConsole);
				
				if (gameStateQuery(g_game_state, ShowConsole)) {
					SDL_StartTextInput(world->graphics_ctx->window);
				} else {
					SDL_StopTextInput(world->graphics_ctx->window);
				}
				continue; // CONSUME!
			}
			
			// --- TOGGLE EDITOR ---
			if (event.key.scancode == g_keybinds[ACTION_EDITOR_TOGGLE]) {
				if (g_engine_mode == ENGINE_MODE_GAME) {
					g_engine_mode = ENGINE_MODE_EDITOR;
				} else if (g_engine_mode == ENGINE_MODE_EDITOR) {
					g_engine_mode = ENGINE_MODE_GAME;
				}
				continue; // CONSUME!
			}
		}

		// ---------------------------------------------------------
		// 3. ROUTE BY ACTIVE OVERLAY (Console)
		// ---------------------------------------------------------
		if (gameStateQuery(g_game_state, ShowConsole)) {
			if (event.type == SDL_EVENT_TEXT_INPUT) {
				consoleInputInsert(event.text.text, strlen(event.text.text));
				consumed = true;
			} 
			else if (event.type == SDL_EVENT_KEY_DOWN) {
				switch (event.key.scancode) {
					case SDL_SCANCODE_ESCAPE: 
						gameStateToggle(&g_game_state, ShowConsole);
						SDL_StopTextInput(world->graphics_ctx->window);
						consumed = true; 
						break;
					case SDL_SCANCODE_BACKSPACE:		consoleBackspace(); consumed = true; break;
					case SDL_SCANCODE_RETURN:
					case SDL_SCANCODE_KP_ENTER:		consoleEnter(&g_frame_arena); consumed = true; break;
					case SDL_SCANCODE_LEFT:			consoleLeftArrow(); consumed = true; break;
					case SDL_SCANCODE_RIGHT:		consoleRightArrow(); consumed = true; break;
					case SDL_SCANCODE_UP:			consoleUpArrow(); consumed = true; break;
					case SDL_SCANCODE_DOWN:			consoleDownArrow(); consumed = true; break;
					case SDL_SCANCODE_TAB:			consoleTab(); consumed = true; break;
					default: break;
				}
			}
		}

		if (consumed) {
			continue;
		}

		// ---------------------------------------------------------
		// 4. ROUTE BY BASE ENGINE MODE
		// ---------------------------------------------------------
		switch (g_engine_mode) {
			case ENGINE_MODE_GAME:
				if (event.type == SDL_EVENT_KEY_DOWN) {
					if (event.key.key == SDLK_ESCAPE) {
						gameStateToggle(&g_game_state, Running);
					} else if (event.key.key == SDLK_R) {
						vkDeviceWaitIdle(world->graphics_ctx->device);
						if (system("make")) {
							consoleAppend("Error detected recompiling shaders.");
						} else {
							createPBRPipeline(world->graphics_ctx);
						}
					}
				}
				break;

			case ENGINE_MODE_EDITOR:
				if (event.type == SDL_EVENT_KEY_DOWN) {
					if (event.key.key == SDLK_ESCAPE) {
						gameStateToggle(&g_game_state, Running);
					}
				}
				break;

			case ENGINE_MODE_MAIN_MENU:
				break;
		}
	}

	// ---------------------------------------------------------
	// 5. RESOLVE MOUSE STATE
	// ---------------------------------------------------------
	resolveMouseState(world->graphics_ctx->window);
}
