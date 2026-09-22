#include "graphics_layer.h"
#include "base_layer.h"
#include "world.h"
#include "vars.h"
#include "fonts.h"
#include "console.h"

Allocator	g_frame_arena = {0};
Allocator	g_perm_arena = {0};

GameState	g_game_state = 0;

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

static inline void resolveMouseState(SDL_Window *window) {
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

static inline void beginFrame(World *world) {
	inputBeginFrame();
	do_callbacks();

	processEvents(world);
}

static inline void	endFrame(World world) {
	(void)world;
	modelCacheSweep();
	imguiResetFrame();
	inputEndFrame();
}

int	loop(World world)
{
	const u32	screen_w = world.graphics_ctx->window_width;
	const u32	screen_h = world.graphics_ctx->window_height;

	Font	*font = &world.fonts.fonts[0];
	Allocator	*frame_arena = &g_frame_arena;

	u64	frames = 0;
	double	fps = 0;
	double	fps_avg = 0;

	EntityRenderInfo	entity_info = {};
	UiRenderInfo		ui_info = {};

	// Entity	*e = loadEntity(world.graphics_ctx, STRING_LIT("data/models/test_scene.glb"), &world.entity_allocator);
	// vectorAppend(world.entities, &e);

	u64	last_time = SDL_GetPerformanceCounter();
	while (gameStateQuery(g_game_state, Running))
	{
		// --- Begining of frame ---
		getMsAndFps(&world.dt_ms, &fps, &fps_avg, &last_time, &frames);
		if (world.dt_ms <= 16.6) {
			usleep((useconds_t)(16.6 - world.dt_ms) * 1000);
		}
		beginFrame(&world);

		if (gameStateQuery(g_game_state, ShowConsole)) {
			openConsole(screen_w, screen_h, font, 12, frame_arena);
		}
		if (gameStateQuery(g_game_state, ShowFps)) {
			showFps(world.ui->imgui_root, font, fps, frame_arena);
		}

		// --- RENDERING ---
		if (getModelCountFromCache())
			entity_info = buildEntityRenderInfo(world.entities, getModelCountFromCache(), &g_frame_arena);
		uiCalculateLayout(world.graphics_ctx->window_width, world.graphics_ctx->window_height, &g_frame_arena);
		ui_info = buildUiRenderInfo(world.ui->rmgui_root, &g_frame_arena);

		render(world.graphics_ctx, &world.player->camera, entity_info, ui_info);

		// --- UPDATES ---
		updatePlayer(world.player, world.dt_ms, world.graphics_ctx->window);
		updateEntities(world.entities, world.dt_ms);

		// --- End of frame ---
		endFrame(world);
		g_frame_arena.fp_reset(&g_frame_arena);

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

	gameStateToggle(&g_game_state, Running);
	world.player = &p;
	world.graphics_ctx = &gctx;
	world.ui = &ui;
	world.key_states = SDL_GetKeyboardState(NULL);
	g_frame_arena = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	g_perm_arena = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	world.entity_allocator = newHeapAllocator(MB(2), NULL, DEFAULT_ALIGN);
	world.entities = vectorCreate(16, sizeof(Entity *), &world.entity_allocator);
	updateGridProperties(&world);

	startGraphics(world.graphics_ctx);
	initUi(&ui, gctx.window_width, gctx.window_height, &g_perm_arena);
	consoleInit(&g_perm_arena);

	Entity	*player = loadEntity(world.graphics_ctx, STRING_LIT("data/models/rabbit.glb"), &world.entity_allocator);
	vectorAppend(world.entities, &player);

	register_callback(STRING_LIT("data/All.variables"), vars_callback, &world);
	initPlayer(world.player, &world);
	initFonts(world.graphics_ctx, &world.fonts, &g_perm_arena, &g_frame_arena);

	loop(world);
	printf("\n\n\n");
	return (0);
}
