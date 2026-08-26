#include "graphics_layer.h"
#include "base_layer.h"
#include "world.h"
#include "vars.h"
#include "fonts.h"

// static void getCameraPos(Camera *camera)
// {
// 	print("Pos: %.2f,%.2f,%.2f\n", camera->position[0], camera->position[1], camera->position[2]);
// }

static void	print_fps(double fps)
{
	print("Current FPS: %.2f\n", fps);
}

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

static void updateCamera(Camera *camera, SDL_Window *window, double dt)
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

static inline void	beginFrame(World world, bool *running) {
	SDL_Event	event = {0};
	inputBeginFrame();
	do_callbacks();
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_EVENT_QUIT) {
			*running = false;
			break ;
		} else if (event.key.key == SDLK_ESCAPE) {
			*running = false;
			break ;
		} else if (event.key.key == SDLK_M) {
			createRandomEntity(world);
			break ;
		} else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
			world.graphics_ctx->window_width = event.window.data1;
			world.graphics_ctx->window_height = event.window.data2;
			break ;
		}
		inputProccessEvent(&event);
	}
}

static void	displayFps(World world, Allocator *frame_allocator, double fps)
{
	u8	strdata[16];
	String	fps_string = {.data = strdata};

	fps_string.count = stbsp_snprintf(strdata, 16, "Fps: %.2f", fps);
	textDraw(fps_string, &world.fonts.fonts[0], 16, (vec2){world.graphics_ctx->window_width - 200, 200}, (vec4){255, 255, 255, 255});
}

static inline void	endFrame(World world) {
	modelCacheSweep();
	inputEndFrame();
}

int	loop(World world)
{
	u64	frames = 0;
	double	fps = 0;
	double	fps_avg = 0;

	EntityRenderInfo	entity_info;
	TextRenderInfo		text_info;

	bool	running = true;
	u64	last_time = SDL_GetPerformanceCounter();
	while (running)
	{
		getMsAndFps(&world.dt_ms, &fps, &fps_avg, &last_time, &frames);
		beginFrame(world, &running);
		displayFps(world, &world.frame_allocator, fps);

		textDraw(STRING_LIT("12892Yippe:.?!!!"), &world.fonts.fonts[0], 64, (vec2){100, 100}, (vec4){255, 0, 255, 255});

		entity_info = buildEntityRenderInfo(world.entities, getModelCountFromCache(), &world.frame_allocator);
		text_info = buildTextRenderInfo(&world.fonts.fonts[0]);
		render(world.graphics_ctx, &world.player->camera, entity_info, text_info);

		updatePlayer(world.player, world.dt_ms, world.graphics_ctx->window);
		updateCamera(&world.player->camera, world.graphics_ctx->window, world.dt_ms);
		updateEntities(world.entities, world.dt_ms);

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

	world.player = &p;
	world.graphics_ctx = &gctx;
	world.key_states = SDL_GetKeyboardState(NULL);
	world.frame_allocator = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	world.perm_allocator = newArenaAllocator(MB(32), NULL, DEFAULT_ALIGN);
	world.entity_allocator = newHeapAllocator(MB(2), NULL, DEFAULT_ALIGN);
	world.entities = vectorCreate(16, sizeof(Entity *), &world.entity_allocator);
	updateGridProperties(&world);

	startGraphics(world.graphics_ctx);


	register_callback(STRING_LIT("data/All.variables"), vars_callback, &world);
	initPlayer(world.player);
	initFonts(world.graphics_ctx, &world.fonts, &world.perm_allocator, &world.frame_allocator);

	loop(world);
	printf("\n\n\n");
	return (0);
}
