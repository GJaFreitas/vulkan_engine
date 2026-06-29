#include "graphics_layer.h"
#include "base_layer.h"
#include "vars.h"

int	loop()
{

	GraphicsContext	ctx = {0};
	GLTFModel	model;


	startGraphics(&ctx);
	// TODO: Create destruction function
	gltf_load(STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"), &model, &ctx);


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
				ctx.window_width = event.window.data1;
				ctx.window_height = event.window.data2;
				break ;
			}
		}
		// fprintf(stderr, "vertex_buffer handle before render: %p\n", (void*)ctx.vertex_buffer);
		render(&ctx);
		
		check_var_modify();
	}

	endGraphics(&ctx);
	return (0);
}

int	main(void)
{
	start_logs();
	init_vars();
	loop();
	// gltf_load(STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"), &model);
	printf("\n\n\n");
	return (0);
}
