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
	versor	rotation;
	
	float	spin;
}	Entity;
void	unloadEntity(Entity *e, Allocator *a);
Entity	*loadEntity(GraphicsContext *ctx, String model_path, Allocator *a);
void	updateEntities(Vector *entities, double dt);




EntityRenderInfo	buildRenderInfo(Vector *entity_vector, u32 model_count, Allocator *a);

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
