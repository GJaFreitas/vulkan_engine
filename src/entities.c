#include "world.h"
#include "console.h"

void	spawnCommand(int argc, String *argv)
{
	(void)argc;
	(void)argv;
	consoleAppend("To be implemented");
}

void	updateEntities(Vector *entities, double dt)
{
	(void)entities;
	(void)dt;
}

static void modelMatFromPosDir(vec3 position, versor rotation, mat4 dest)
{
	glm_quat_mat4(rotation, dest);

	dest[3][0] = position[0];
	dest[3][1] = position[1];
	dest[3][2] = position[2];

}

// - `__compar_fn_t __compar (aka int (*)(const void *, const void *))`
static int	compare_func(const void *p1, const void * p2)
{
	const EntityRenderData	*data1 = p1;
	const EntityRenderData	*data2 = p2;

	if (data1->model_idx < data2->model_idx) return -1;
	if (data1->model_idx > data2->model_idx) return 1;
	return 0;
}

static i16	linearSearch(Model **models, Model *cur, i16 model_count)
{
	for (u16 i = 0; i < model_count; i++) {
		if (models[i] == cur) return i;
	}
	return (-1);
}

EntityRenderInfo	buildEntityRenderInfo(Vector *entity_vector, u32 model_count, Allocator *a)
{
	const u64	entity_count = entity_vector->used;

	EntityRenderInfo	entity_info = {};

	entity_info.models = a->fp_allocation(a, sizeof(Model *) * model_count, DEFAULT_ALIGN);
	entity_info.data = a->fp_allocation(a, sizeof(EntityRenderData) * entity_count, DEFAULT_ALIGN);

	i16	model_idx;
	u16	cur_model_count = 0;
	u32	render_idx = 0;

	for (u64 i = 0; i < entity_count; i++) {
		Entity	**ent_ptr = vectorGet(entity_vector, i);
		Entity	*ent = *ent_ptr;

		if (!ent->active) continue;
		// »speed
		// Return -1 if the model isnt in use already
		// If the model is already in use then we get its index
		model_idx = linearSearch(entity_info.models, ent->model, cur_model_count);

		if (model_idx == -1) {
			// New model so add it to the array and increment the model_idx
			model_idx = cur_model_count;
			entity_info.models[model_idx] = ent->model;
			cur_model_count++;
		}

		entity_info.data[render_idx].model_idx = model_idx;
		modelMatFromPosDir(ent->pos, ent->rotation, entity_info.data[render_idx].instance_data.model_mat);
		render_idx++;
	}

	entity_info.model_count = cur_model_count;
	entity_info.entity_count = render_idx;

	qsort(entity_info.data, render_idx, sizeof(EntityRenderData), compare_func);

	return entity_info;
}

void	unloadEntity(Entity *e, Allocator *a)
{
	modelCacheRelease(e->model);
	a->fp_free(a, e);
}

Entity	*loadEntity(GraphicsContext *ctx, String model_path, Allocator *a)
{
	Entity	*e = a->fp_allocation(a, sizeof(Entity), DEFAULT_ALIGN);

	e->model = modelCacheAcquire(ctx, model_path);
	e->active = true;
	e->rotation[X] = 0;
	e->rotation[Y] = 0;
	e->rotation[Z] = 0;
	e->rotation[W] = 1;
	return e;
}

static void	movePlayer(Player *p, vec3 new_pos)
{
	p->p_entity->pos[X] = new_pos[X];
	p->p_entity->pos[Y] = new_pos[Y];
	p->p_entity->pos[Z] = new_pos[Z];
}

void	updatePlayer(Player *p, double dt, SDL_Window *window)
{
	if (g_engine_mode != ENGINE_MODE_GAME || gameStateQuery(g_game_state, ShowConsole)) return;
	(void)window;

	Camera	*camera = &p->camera;
	bool	noclip = gameStateQuery(g_game_state, NoClip);

	// 1. Process mouse look FIRST so movement is relative to the updated view
	float	xrel, yrel;
	SDL_GetRelativeMouseState(&xrel, &yrel);

	camera->yaw	+= xrel * camera->mouseSensitivity;
	camera->pitch	-= yrel * camera->mouseSensitivity;
	camera->pitch	= glm_clamp(camera->pitch, -89.0f, 89.0f);

	// Recalculate camera vectors from yaw/pitch
	vec3	front;
	front[0] = cos(glm_rad(camera->yaw)) * cos(glm_rad(camera->pitch));
	front[1] = sin(glm_rad(camera->pitch));
	front[2] = sin(glm_rad(camera->yaw)) * cos(glm_rad(camera->pitch));
	glm_vec3_normalize_to(front, camera->front);
	glm_vec3_crossn(camera->front, camera->worldUp, camera->right);
	glm_vec3_crossn(camera->right, camera->front, camera->up);

	// 2. Handle Mouse Scroll (Zoom / Orbit Distance)
	if (g_input_state.mouse_wheel_y != 0.0f) {
		if (noclip) {
			// Change Field of View (Zoom)
			camera->fov -= g_input_state.mouse_wheel_y * 5.0f; 
			camera->fov = glm_clamp(camera->fov, 10.0f, 120.0f); // Keep FOV sane
		} else {
			// Change Orbit Distance
			p->orbit_dist -= g_input_state.mouse_wheel_y * 0.5f;
			p->orbit_dist = glm_clamp(p->orbit_dist, MIN_ORBIT_DIST, MAX_ORBIT_DIST); // Don't clip inside player or go too far
		}
	}

	// 3. Prepare movement vectors based on the current mode
	vec3	move_front;
	vec3	move_right;
	vec3	move_up;

	if (noclip) {
		glm_vec3_copy(camera->front, move_front);
		glm_vec3_copy(camera->right, move_right);
	} else {
		glm_vec3_copy(camera->front, move_front);
		move_front[1] = 0.0f;
		if (glm_vec3_norm2(move_front) > 0.001f) glm_vec3_normalize(move_front);

		glm_vec3_copy(camera->right, move_right);
		move_right[1] = 0.0f;
		if (glm_vec3_norm2(move_right) > 0.001f) glm_vec3_normalize(move_right);
	}
	glm_vec3_copy(camera->worldUp, move_up);

	// 4. Accumulate raw input directions using the prepared vectors
	vec3	move_dir = GLM_VEC3_ZERO_INIT;
	bool	moved = false;

	if (key_held(g_keybinds[ACTION_MOVE_FORWARD]))	{ glm_vec3_add(move_dir, move_front, move_dir); moved = true; }
	if (key_held(g_keybinds[ACTION_MOVE_BACKWARD]))	{ glm_vec3_sub(move_dir, move_front, move_dir); moved = true; }
	if (key_held(g_keybinds[ACTION_MOVE_LEFT]))	{ glm_vec3_sub(move_dir, move_right, move_dir); moved = true; }
	if (key_held(g_keybinds[ACTION_MOVE_RIGHT]))	{ glm_vec3_add(move_dir, move_right, move_dir); moved = true; }
	if (key_held(g_keybinds[ACTION_JUMP]))		{ glm_vec3_add(move_dir, move_up, move_dir); moved = true; }
	if (key_held(g_keybinds[ACTION_SHIFT]))		{ glm_vec3_sub(move_dir, move_up, move_dir); moved = true; }

	// 5. Apply movement
	if (moved) {
		glm_vec3_normalize(move_dir);

		vec3	displacement;
		glm_vec3_scale(move_dir, p->movSpeed * dt, displacement);

		if (noclip) {
			glm_vec3_add(camera->position, displacement, camera->position);
		} else {
			vec3	desired_pos;
			glm_vec3_add(p->p_entity->pos, displacement, desired_pos);
			movePlayer(p, desired_pos);
		}
	}

	// 6. Orbit Camera (Third Person View)
	if (!noclip) {
		vec3	target_pos;
		vec3	offset;

		// Focus on the upper body/head, not the feet
		glm_vec3_copy(p->p_entity->pos, target_pos);
		target_pos[1] += 1.5f; 

		// Push the camera backward along its own negative front vector using dynamic orbit distance
		glm_vec3_scale(camera->front, -p->orbit_dist, offset);
		glm_vec3_add(target_pos, offset, camera->position);
	}
}

void	initPlayer(Player *p, World *world)
{
	p->movSpeed = 0.1f;
	Entity	**e = vectorGet(world->entities, 0);
	p->p_entity = *e;

	Camera	*camera = &p->camera;
	glm_vec3_copy((vec3){0.0f,1.0f,4.0f},  camera->position);
	glm_vec3_copy((vec3){0.0f, 0.0f, -1.0f}, camera->front);
	glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f},  camera->up);
	glm_vec3_copy((vec3){1.0f, 0.0f, 0.0f},  camera->right);
	glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f},  camera->worldUp);
	camera->yaw = -90.0f;
	camera->pitch = 0;
	camera->fov = 45.0f;
	camera->mouseSensitivity = 0.1f;
	camera->movementSpeed = 0.01f;
	camera->far_z = 1000.0f;
	camera->near_z = 0.01f;
}
