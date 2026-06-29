#include "graphics_layer.h"
#include "base_layer.h"
#include "world.h"
#include "vars.h"

int	loop(World world)
{

	GLTFModel	model;


	startGraphics(world.graphics_ctx);
	// TODO: Create destruction function
	gltf_load(STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"), &model, world.graphics_ctx);


	bool	running = true;
	while (running)
	{
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
		// fprintf(stderr, "vertex_buffer handle before render: %p\n", (void*)ctx.vertex_buffer);
		render(world.graphics_ctx, &world.player->camera);
		updatePlayer(world.player, world.key_states, world.dt_ms);
		
		check_var_modify();
		world.dt_ms = getFrameDelta();
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
	world.dt_ms = getFrameDelta();

	initPlayer(world.player);



	set_log_severity(LOG_DEBUG);
	start_logs();
	init_vars();
	loop(world);
	// gltf_load(STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"), &model);
	printf("\n\n\n");
	return (0);
}
