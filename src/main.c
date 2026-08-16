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

	// WASD movement
	const bool *keys = SDL_GetKeyboardState(NULL);
	vec3 move = {0};

	if (keys[SDL_SCANCODE_W]) glm_vec3_muladds(camera->front, camera->movementSpeed * dt, move);
	if (keys[SDL_SCANCODE_S]) glm_vec3_muladds(camera->front, -camera->movementSpeed * dt, move);
	if (keys[SDL_SCANCODE_A]) glm_vec3_muladds(camera->right, -camera->movementSpeed * dt, move);
	if (keys[SDL_SCANCODE_D]) glm_vec3_muladds(camera->right, camera->movementSpeed * dt, move);
	if (keys[SDL_SCANCODE_SPACE])    glm_vec3_muladds(camera->worldUp, camera->movementSpeed * dt, move);
	if (keys[SDL_SCANCODE_LSHIFT])   glm_vec3_muladds(camera->worldUp, -camera->movementSpeed * dt, move);

	glm_vec3_add(camera->position, move, camera->position);
}

const String	models[] = {
	// STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"),
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

int	loop(World world)
{
	u64	frames = 0;
	double	fps = 0;
	double	fps_avg = 0;

	EntityRenderInfo	entity_info;

	bool	running = true;
	u64	last_time = SDL_GetPerformanceCounter();
	while (running)
	{
		getMsAndFps(&world.dt_ms, &fps, &fps_avg, &last_time, &frames);
		SDL_Event	event = {0};
		do_callbacks();
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
				break ;
			} else if (event.key.key == SDLK_ESCAPE) {
				running = false;
				break ;
			} else if (event.key.key == SDLK_M) {
				createRandomEntity(world);
				break ;
			} else if (event.key.key == SDLK_C) {
				print_fps(fps);
				break ;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				world.graphics_ctx->window_width = event.window.data1;
				world.graphics_ctx->window_height = event.window.data2;
				break ;
			}
		}
		entity_info = buildRenderInfo(world.entities, getModelCountFromCache(), &world.frame_allocator);

		render(world.graphics_ctx, &world.player->camera, entity_info);

		updateCamera(&world.player->camera, world.graphics_ctx->window, world.dt_ms);

		updateEntities(world.entities, world.dt_ms);

		modelCacheSweep();

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

#define FONT_PATH "/usr/share/fonts/Adwaita/AdwaitaMono-Bold.ttf"

void	createSomeText(GraphicsContext *ctx, World *world)
{
	FT_Library	ft_lib;

	// This is just a handle
	FT_Face		ft_face;

	FT_Init_FreeType(&ft_lib);
	FT_New_Face(ft_lib, FONT_PATH, 0, &ft_face);
	FT_Set_Char_Size(ft_face, 0, 16 * 64, 96, 96);

	hb_font_t	*hb_font = hb_ft_font_create(ft_face, NULL);
	world->font.face = ft_face;		// This is a handle so this assignment is fine
	world->font.hb_font = hb_font;		// Same here since its a pointer
	world->font.font_size = 12.0f;
	createTextAtlas(ctx, &world->font, STRING_LIT("1234567890ABCDEFGHIJKLMNOPQRSTUVWYZabcdefghijklmnopqrstuvwyz!"));

	hb_buffer_t	*buf = hb_buffer_create();
	const char 	*text = "Hello world!";
	hb_buffer_add_utf8(buf, text, -1, 0, -1);

	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buf, hb_language_from_string("en", -1));

	hb_shape(hb_font, buf, NULL, 0);

	hb_buffer_destroy(buf);
	hb_font_destroy(hb_font);
	FT_Done_Face(ft_face);
	FT_Done_FreeType(ft_lib);
}

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
	world.frame_allocator = newArenaAllocator(MB(1), NULL, DEFAULT_ALIGN);
	world.entity_allocator = newHeapAllocator(MB(2), NULL, DEFAULT_ALIGN);
	world.entities = vectorCreate(16, sizeof(Entity *), &world.entity_allocator);
	updateGridProperties(&world);

	startGraphics(world.graphics_ctx);

	register_callback(STRING_LIT("data/All.variables"), vars_callback, &world);
	initPlayer(world.player);

	startGraphics(world.graphics_ctx);
	createSomeText(world.graphics_ctx, &world);

	loop(world);
	printf("\n\n\n");
	return (0);
}
