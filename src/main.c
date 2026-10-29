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

typedef	UiRenderInfo	(*ModuleRunUi)(GraphicsContext *graphics_ctx, UiState *ui, Font *font, double fps);
typedef	void		(*ModuleUiResetFrame)(UiContext *uictx, ComponentPool *pool);
typedef	void		(*ModuleInitUi)(UiState *ui, u32 screen_w, u32 screen_h, Allocator *perm);
typedef struct ModuleAPI
{
	void			*handle;
	ModuleRunUi		run_ui;
	ModuleUiResetFrame	reset_frame;
	ModuleInitUi		init_ui;
}	ModuleAPI;

static ModuleAPI	g_module_api;

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
	g_module_api.reset_frame(&world.ui->ctx, &world.ui->pool);
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
		ui_info = g_module_api.run_ui(world.graphics_ctx, world.ui, font, fps);
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

#define MODULE_PATH "build/libgame_module.so"
void	hotreload_func(void *data)
{
	(void)data;
	const char* dlsym_err;
	ModuleAPI	*api = &g_module_api;

	system("meson compile -C build game_module");
	api->handle = dlopen(MODULE_PATH, RTLD_NOW | RTLD_LOCAL);
	if (!api->handle) {
		engine_error(LOG_FILE, "dlopen() failed.");
		printf("ERROR opening file %s\n", dlerror());
		return ;
	}

	if ((api->run_ui = dlsym(api->handle, "runUi")) == NULL) {
		engine_error(LOG_FILE, "Failed to find symbol runUi in module.");
		goto symbol_error;
	}
	if ((api->init_ui = dlsym(api->handle, "initUi")) == NULL) {
		engine_error(LOG_FILE, "Failed to find symbol initUi in module.");
		goto symbol_error;
	}
	if ((api->reset_frame = dlsym(api->handle, "imguiResetFrame")) == NULL) {
		engine_error(LOG_FILE, "Failed to find symbol imguiResetFrame in module.");
		goto symbol_error;
	}

	return;

symbol_error:
	dlsym_err = dlerror();
	if (dlsym_err) {
		printf("ERROR loading symbol: %s\n", dlsym_err);
	}
	dlclose(api->handle);
	api->handle = NULL;
}

int	main(void)
{
	start_logs();
	set_log_severity(LOG_LOG);
	init_vars();
	start_hotload_callbacks();

	register_callback(STRING_LIT("build/libgame_module.so"), hotreload_func, NULL);
	hotreload_func(NULL);

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
	g_module_api.init_ui(&ui, gctx.window_width, gctx.window_height, &g_perm_arena);
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
