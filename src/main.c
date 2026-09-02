#include "graphics_layer.h"
#include "base_layer.h"
#include "world.h"
#include "vars.h"
#include "fonts.h"
#include "console.h"


GameState	game_state = 0;

static inline void	getMsAndFps(double *ms, double *fps, double *fps_avg, u64 *last_time, u64 *frames) {
	static u32	frame_start = 2000;
	static u64	freq = 0;
	const u64	now = SDL_GetPerformanceCounter();
	if (!freq) freq = SDL_GetPerformanceFrequency();

	const double	dt = (double)(now - *last_time) / (double)freq;
	*ms = dt * 1000.0f;
	*last_time = now;
	if (frame_start) {
		frame_start--;
	} else {
		*fps = 1.0f / dt;
		*frames += 1;
		*fps_avg += (*fps - *fps_avg) / *frames;
	}
}

static void updateCamera(Camera *camera, SDL_Window *window)
{
	// Mouse look - only when right mouse button is held
	if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK)
	{
		SDL_SetWindowRelativeMouseMode(window, true);
		float xrel, yrel;
		SDL_GetRelativeMouseState(&xrel, &yrel);

		camera->yaw   += xrel * camera->mouseSensitivity;
		camera->pitch -= yrel * camera->mouseSensitivity;
		camera->pitch  = glm_clamp(camera->pitch, -89.0f, 89.0f);

		// Recalculate front vector from yaw/pitch
		vec3 front;
		front[0] = cos(glm_rad(camera->yaw)) * cos(glm_rad(camera->pitch));
		front[1] = sin(glm_rad(camera->pitch));
		front[2] = sin(glm_rad(camera->yaw)) * cos(glm_rad(camera->pitch));
		glm_vec3_normalize_to(front, camera->front);
		glm_vec3_crossn(camera->front, camera->worldUp, camera->right);
		glm_vec3_crossn(camera->right, camera->front, camera->up);
	} else {
		SDL_SetWindowRelativeMouseMode(window, false);
	}
}

const String	models[] = {
	STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"),
	STRING_LIT("data/models/DiffuseTransmissionTeacup.glb"),
};

static void	initializeRandomVec(float *v, u32 count, float min, float max) {
	for (u32 i = 0; i < count; i++) {
		v[i] = randomFloat(min, max);
	}
}

void	createRandomEntity(World world)
{
	u32	model = rand() % sizeofarray(models);
	Entity	*e = loadEntity(world.graphics_ctx, models[model], &world.entity_allocator);
	vectorAppend(world.entities, &e);

	e->spin = glm_rad(randomFloat(0, 90));
	initializeRandomVec(e->rotation, 4, 0, 1);
	glm_quat_normalize(e->rotation);
	initializeRandomVec(e->pos, 3, -1, 1);
}

static inline void beginFrame(World *world) {
	SDL_Event event = {0};
	inputBeginFrame();
	do_callbacks();

	while (SDL_PollEvent(&event))
	{
		// ---------------------------------------------------------
		// 1. GLOBAL / SYSTEM EVENTS (Always process these)
		// ---------------------------------------------------------
		if (event.type == SDL_EVENT_QUIT) {
			gameStateToggle(&game_state, Running);
			continue; 
		}
		if (event.type == SDL_EVENT_WINDOW_RESIZED) {
			world->graphics_ctx->window_width = event.window.data1;
			world->graphics_ctx->window_height = event.window.data2;
			continue;
		}

		// ---------------------------------------------------------
		// 2. CONTEXT TOGGLES (Highest priority input)
		// ---------------------------------------------------------
		if (event.type == SDL_EVENT_KEY_DOWN) {
			if (event.key.scancode == g_keybinds[ACTION_CONSOLE_TOGGLE]) {
				gameStateToggle(&game_state, ShowConsole);
				if (g_engine_mode == ENGINE_MODE_CONSOLE) {
					// TODO: When more modes are added i need a queue
					g_engine_mode = ENGINE_MODE_GAME;
					SDL_StopTextInput(world->graphics_ctx->window); // Stop OS text capture
				} else {
					g_engine_mode = ENGINE_MODE_CONSOLE;
					SDL_StartTextInput(world->graphics_ctx->window); // Start OS text capture
				}
				continue; // CONSUME the toggle event!
			}
		}

		// ---------------------------------------------------------
		// 3. ROUTE BY ACTIVE CONTEXT
		// ---------------------------------------------------------
		bool consumed = false;

		switch (g_engine_mode) {
			case ENGINE_MODE_CONSOLE:
				// Handle text input
				if (event.type == SDL_EVENT_TEXT_INPUT) {
					consoleInputInsert(event.text.text, strlen(event.text.text));
					consumed = true;
				} 
				else if (event.type == SDL_EVENT_KEY_DOWN) {
					switch (event.key.scancode) {
						case SDL_SCANCODE_ESCAPE: 
							// ESC in console closes console, doesn't quit game!
							g_engine_mode = ENGINE_MODE_GAME;
							gameStateToggle(&game_state, ShowConsole);
							SDL_StopTextInput(world->graphics_ctx->window);
							consumed = true; 
							break;
						case SDL_SCANCODE_BACKSPACE: consoleBackspace(); consumed = true; break;
						case SDL_SCANCODE_RETURN:
						case SDL_SCANCODE_KP_ENTER:  consoleEnter(&world->frame_allocator); consumed = true; break;
						case SDL_SCANCODE_LEFT:      consoleLeftArrow(); consumed = true; break;
						case SDL_SCANCODE_RIGHT:     consoleRightArrow(); consumed = true; break;
						default: break;
					}
				}
			break;

			case ENGINE_MODE_GAME:
				// Only process game-specific event presses here
				if (event.type == SDL_EVENT_KEY_DOWN) {
					if (event.key.key == SDLK_ESCAPE) {
						gameStateToggle(&game_state, Running);
						consumed = true;
					}
				}
			break;

			case ENGINE_MODE_MENU:
			break;
		}

		// If a context swallowed the event, do not pass it to the physical key tracker!
		if (consumed) {
			continue; 
		}

		// ---------------------------------------------------------
		// 4. FALLBACK: RECORD PHYSICAL KEY STATE
		// ---------------------------------------------------------
		// Only unconsumed events make it here to update the physical key arrays
		inputProccessEvent(&event);
	}
}

static inline void	endFrame(World world) {
	modelCacheSweep();
	imguiResetFrame();
	inputEndFrame();
}

int	loop(World world)
{
	const u32	screen_w = world.graphics_ctx->window_width;
	const u32	screen_h = world.graphics_ctx->window_height;

	Font	*font = &world.fonts.fonts[0];
	Allocator	*frame_arena = &world.frame_allocator;

	u64	frames = 0;
	double	fps = 0;
	double	fps_avg = 0;

	EntityRenderInfo	entity_info = {};
	UiRenderInfo		ui_info = {};

	Entity	*e = loadEntity(world.graphics_ctx, STRING_LIT("data/models/test_scene.glb"), &world.entity_allocator);
	vectorAppend(world.entities, &e);

	u64	last_time = SDL_GetPerformanceCounter();
	while (gameStateQuery(game_state, Running))
	{
		// --- Begining of frame ---
		getMsAndFps(&world.dt_ms, &fps, &fps_avg, &last_time, &frames);
		if (world.dt_ms <= 16.6) {
			usleep(16.6 - world.dt_ms);
		}
		beginFrame(&world);

		if (gameStateQuery(game_state, ShowConsole)) {
			openConsole(screen_w, screen_h, font, 12, frame_arena);
		}
		if (gameStateQuery(game_state, ShowFps)) {
			showFps(world.ui->imgui_root, font, fps, frame_arena);
		}

		// --- RENDERING ---
		if (getModelCountFromCache())
			entity_info = buildEntityRenderInfo(world.entities, getModelCountFromCache(), &world.frame_allocator);
		uiCalculateLayout(world.graphics_ctx->window_width, world.graphics_ctx->window_height, &world.frame_allocator);
		ui_info = buildUiRenderInfo(world.ui->rmgui_root, &world.frame_allocator);

		render(world.graphics_ctx, &world.player->camera, entity_info, ui_info);

		// --- UPDATES ---
		updatePlayer(world.player, world.dt_ms, world.graphics_ctx->window);
		updateCamera(&world.player->camera, world.graphics_ctx->window);
		updateEntities(world.entities, world.dt_ms);

		// --- End of frame ---
		endFrame(world);
		world.frame_allocator.fp_reset(&world.frame_allocator);

	}
	engine_debug(LOG_FILE, "Killing proccess");
	exit(0);
	endGraphics(world.graphics_ctx);
	return (0);
}

void	updateGridProperties(void *udata)
{
	World *world = (World *)udata;
	GraphicsContext	*ctx = world->graphics_ctx;

	ctx->grid_properties.grid_size = g_settings.dev.grid_size;
	ctx->grid_properties.fade_distance = g_settings.dev.fade_distance;
	ctx->grid_properties.line_width = g_settings.dev.line_width;
	ctx->grid_properties.major_line_every = g_settings.dev.major_line_every;
}

#define FONT_PATH "/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Bold.ttf"

int	main(void)
{
	start_logs();
	set_log_severity(LOG_WARN);
	init_vars();
	start_hotload_callbacks();
	initModelCache();

	World		world = {};
	GraphicsContext	gctx = {};
	Player		p = {};
	UiState		ui = {};

	gameStateToggle(&game_state, Running);
	world.player = &p;
	world.graphics_ctx = &gctx;
	world.ui = &ui;
	world.key_states = SDL_GetKeyboardState(NULL);
	world.frame_allocator = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	world.perm_allocator = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	world.entity_allocator = newHeapAllocator(MB(2), NULL, DEFAULT_ALIGN);
	world.entities = vectorCreate(16, sizeof(Entity *), &world.entity_allocator);
	updateGridProperties(&world);

	startGraphics(world.graphics_ctx);
	initUi(&ui, gctx.window_width, gctx.window_height, &world.perm_allocator);

	register_callback(STRING_LIT("data/All.variables"), vars_callback, &world);
	initPlayer(world.player);
	initFonts(world.graphics_ctx, &world.fonts, &world.perm_allocator, &world.frame_allocator);

	loop(world);
	printf("\n\n\n");
	return (0);
}
