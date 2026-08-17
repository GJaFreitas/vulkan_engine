#pragma once

#include "base_layer.h"
#include "graphics_layer.h"
#include "fonts.h"

typedef struct Player
{
	Camera	camera;
	float	movSpeed;
	vec3	pos;
	versor	rotation;
}	Player;
void	initPlayer(Player *p);
void	updatePlayer(Player *p, double dt, SDL_Window *window);

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

EntityRenderInfo	buildEntityRenderInfo(Vector *entity_vector, u32 model_count, Allocator *a);

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

	Font		font;

}	World;

#define NUM_KEYS SDL_SCANCODE_COUNT
typedef struct InputState
{
	bool		current[NUM_KEYS];
	bool		last[NUM_KEYS];
	SDL_Keymod	mod;
}	InputState;

typedef enum {
	ACTION_MOVE_FORWARD,
	ACTION_MOVE_LEFT,
	ACTION_MOVE_RIGHT,
	ACTION_MOVE_BACKWARD,
	ACTION_JUMP,
	ACTION_SHIFT,



	ACTION_MAX_ENUM,
}	Action;

void	inputBeginFrame(void);
void	inputEndFrame(void);
void	inputProccessEvent(const SDL_Event *e);

extern InputState	g_input_state;
extern SDL_Scancode	g_keybinds[ACTION_MAX_ENUM];

static inline bool key_held(SDL_Scancode sc)       { return g_input_state.current[sc]; }
static inline bool key_pressed(SDL_Scancode sc)     { return g_input_state.current[sc] && !g_input_state.last[sc]; }
static inline bool key_released(SDL_Scancode sc)    { return !g_input_state.current[sc] && g_input_state.last[sc]; }
static inline bool mod_held(SDL_Keymod m)           { return (g_input_state.mod & m) != 0; }
