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

typedef struct Entity
{
	Model	*model;
	bool	active;
	
	vec3	pos;
	vec3	direction;
}	Entity;
void	unloadEntity(Entity *e, Allocator *a);
Entity	*loadEntity(GraphicsContext *ctx, String model_path, vec3 pos, vec3 dir, Allocator *a);




EntityRenderInfo	buildRenderInfo(Vector *entities, Allocator *a);

typedef struct World
{
	Allocator	frame_allocator;
	GraphicsContext	*graphics_ctx;


	// TODO: Implement sim regions?
	Allocator	entity_allocator;
	Vector		*entities;

	Player		*player;



	const bool		*key_states;

	double		dt_ms;

}	World;
