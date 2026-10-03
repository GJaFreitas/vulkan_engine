#include "graphics_layer.h"
#include "base_layer.h"
#include "world.h"
#include "vars.h"
#include "fonts.h"
#include "console.h"
#include <dlfcn.h>

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

static inline void beginFrame(World *world) {
	inputBeginFrame();
	do_callbacks();

	processEvents(world);
}

static inline void	endFrame(World world) {
	(void)world;
	modelCacheTick(&world.model_cache, (u16)world.dt_ms);
	imguiResetFrame(world.ui->ctx, &world.ui->pool);
	inputEndFrame();
}

int	loop(World world)
{
	Font	*font = &world.fonts.fonts[0];

	u64	frames = 0;
	double	fps = 0;
	double	fps_avg = 0;

	EntityRenderInfo	entity_info = {};
	UiRenderInfo		ui_info = {};

	u64	last_time = SDL_GetPerformanceCounter();
	while (gameStateQuery(g_game_state, Running))
	{
		// --- Begining of frame ---
		getMsAndFps(&world.dt_ms, &fps, &fps_avg, &last_time, &frames);
		if (world.dt_ms <= 16.6) {
			usleep((useconds_t)(16.6 - world.dt_ms) * 1000);
		}
		beginFrame(&world);

		// --- RENDERING ---
		ui_info = runUi(world.graphics_ctx, world.ui, font, fps);
		if (modelCacheGetCount(&world.model_cache))
			entity_info = buildEntityRenderInfo(&world.entity_registry, &g_frame_arena);

		render(world.graphics_ctx, &world.model_cache, &world.player->camera, entity_info, ui_info);

		// --- UPDATES ---
		updatePlayer(world.player, &world.entity_registry, world.dt_ms, world.graphics_ctx->window);
		// updateEntities(&world.entities, world.dt_ms);

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

	World		world = {};
	GraphicsContext	gctx = {};
	Player		p = {};
	UiState		ui = {.editor_ctx = {
		.toolbar_h = 40.0f,
		.left_panel_w = 250.0f,
		.right_panel_w = 300.0f,
	}};

	gameStateToggle(&g_game_state, Running);
	world.player = &p;
	world.graphics_ctx = &gctx;
	world.ui = &ui;
	world.key_states = SDL_GetKeyboardState(NULL);
	g_frame_arena = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	g_perm_arena = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	updateGridProperties(&world);

	startGraphics(world.graphics_ctx);
	initUi(&ui, gctx.window_width, gctx.window_height, &g_perm_arena);
	consoleInit(&g_perm_arena);
	initEntityRegistry(&world.entity_registry, &g_perm_arena, 4096);
	initModelCache(&world.model_cache);

	register_callback(STRING_LIT("data/All.variables"), vars_callback, &world);
	initPlayer(world.player, &world, STRING_LIT("data/models/rabbit.glb"));
	initFonts(world.graphics_ctx, &world.fonts, &g_perm_arena, &g_frame_arena);

	loop(world);
	printf("\n\n\n");
	return (0);
}
