#pragma once

#include "base_layer.h"
#include "graphics_layer.h"

typedef struct Player
{
	Camera	camera;
	float	movSpeed;
}	Player;
void	updatePlayer(Player *p, const bool *key_states, double dt_ms);
void	initPlayer(Player *p);

typedef struct World
{
	GraphicsContext	*graphics_ctx;
	Player		*player;
	const bool		*key_states;
	double		dt_ms;
}	World;
