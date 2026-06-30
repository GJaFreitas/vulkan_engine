#include "graphics_layer.h"
#include "base_layer.h"
#include "world.h"
#include "vars.h"



static void updateCamera(Camera *camera, SDL_Window *window, double dt)
{
	// Mouse look - only when right mouse button is held
	if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_RMASK)
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




int	loop(World world)
{

	GLTFModel	model;


	startGraphics(world.graphics_ctx);
	// TODO: Create destruction function


	bool	running = true;
	u64	last_time = SDL_GetTicks();
	while (running)
	{
		u64	now = SDL_GetTicks();
		world.dt_ms = (double)(now - last_time) / 1000.0f;
		last_time = now;
		SDL_Event	event = {0};
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
				break ;
			} else if (event.key.key == SDLK_ESCAPE) {
				running = false;
				break ;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				world.graphics_ctx->window_width = event.window.data1;
				world.graphics_ctx->window_height = event.window.data2;
				break ;
			}
		}
		render(world.graphics_ctx, &world.player->camera);
		updateCamera(&world.player->camera, world.graphics_ctx->window, world.dt_ms);

		check_var_modify();
	}

	endGraphics(world.graphics_ctx);
	return (0);
}

int	main(void)
{
	World	world = {};
	GraphicsContext	gctx = {};
	Player		p = {};

	world.player = &p;
	world.graphics_ctx = &gctx;
	world.key_states = SDL_GetKeyboardState(NULL);

	initPlayer(world.player);



	set_log_severity(LOG_ERROR);
	start_logs();
	init_vars();
	loop(world);
	// gltf_load(STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"), &model);
	printf("\n\n\n");
	return (0);
}
