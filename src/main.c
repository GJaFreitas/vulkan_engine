#include "graphics_layer.h"

int	main(void)
{
	GraphicsContext	ctx = {0};

	startGraphics(&ctx);

	bool	running = true;
	while (running)
	{
		SDL_Event	event = {0};
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
				break ;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				ctx.window_width = event.window.data1;
				ctx.window_height = event.window.data2;
				break ;
			}
		}
		render(&ctx);
	}

	endGraphics(&ctx);
	return (0);
}
