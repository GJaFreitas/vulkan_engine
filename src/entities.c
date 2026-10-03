#include "world.h"
#include "entity.h"
#include "console.h"

void	spawnCommand(int argc, String *argv)
{
	(void)argc;
	(void)argv;
	consoleAppend("To be implemented");
}

// - `__compar_fn_t __compar (aka int (*)(const void *, const void *))`
// Sort by pipeline first, then by model index
static int	compare_func(const void *a, const void *b)
{
	const EntityRenderData	*data_a = (const EntityRenderData *)a;
	const EntityRenderData	*data_b = (const EntityRenderData *)b;

	if (data_a->pipeline_state != data_b->pipeline_state) {
		return (data_a->pipeline_state < data_b->pipeline_state) ? -1 : 1;
	}
	
	if (data_a->model_idx != data_b->model_idx) {
		return (data_a->model_idx < data_b->model_idx) ? -1 : 1;
	}

	return 0;
}

EntityRenderInfo	buildEntityRenderInfo(EntityRegistry *reg, Allocator *a)
{
	EntityRenderInfo	entity_info = {0};

	entity_info.data = a->fp_allocation(a, sizeof(EntityRenderData) * reg->count, DEFAULT_ALIGN);
	u32			render_idx = 0;

	for (u32 i = 0; i < reg->count; i++) {
		
		if (!reg->hot_data[i].active) {
			continue;
		}

		EntityHotData		*hot = &reg->hot_data[i];
		EntityRenderData	*packet = &entity_info.data[render_idx];

		packet->pipeline_state = hot->pipeline_state;
		packet->model_idx = hot->model_handle; 

		// Pure copy - we let the Slang shader build the matrix
		packet->pos[0] = hot->pos[0];
		packet->pos[1] = hot->pos[1];
		packet->pos[2] = hot->pos[2];
		
		packet->rotation[0] = hot->rotation[0];
		packet->rotation[1] = hot->rotation[1];
		packet->rotation[2] = hot->rotation[2];
		packet->rotation[3] = hot->rotation[3];
		
		packet->scale = hot->scale;

		render_idx++;
	}

	entity_info.entity_count = render_idx;
	qsort(entity_info.data, render_idx, sizeof(EntityRenderData), compare_func);

	return entity_info;
}

static void	movePlayer(vec3 old_pos, vec3 new_pos)
{
	old_pos[X] = new_pos[X];
	old_pos[Y] = new_pos[Y];
	old_pos[Z] = new_pos[Z];
}

void	updatePlayer(Player *p, EntityRegistry *reg, double dt, SDL_Window *window)
{
	if (gameStateQuery(g_game_state, ShowConsole)) return;
	(void)window;

	EntityHotData	*p_ent = getEntityHot(reg, p->p_entity);
	Camera	*camera = &p->camera;
	bool	noclip = gameStateQuery(g_game_state, NoClip);
	bool	editor_mode = g_engine_mode == ENGINE_MODE_EDITOR;

	// 1. Process mouse look FIRST so movement is relative to the updated view
	float	xrel, yrel;
	SDL_GetRelativeMouseState(&xrel, &yrel);

	if (!editor_mode || (mouseHeld(SDL_BUTTON_RIGHT))) {
		camera->yaw	+= xrel * camera->mouseSensitivity;
		camera->pitch	-= yrel * camera->mouseSensitivity;
		camera->pitch	= glm_clamp(camera->pitch, -89.0f, 89.0f);
	}

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

	if (keyHeld(g_keybinds[ACTION_MOVE_FORWARD]))	{ glm_vec3_add(move_dir, move_front, move_dir); moved = true; }
	if (keyHeld(g_keybinds[ACTION_MOVE_BACKWARD]))	{ glm_vec3_sub(move_dir, move_front, move_dir); moved = true; }
	if (keyHeld(g_keybinds[ACTION_MOVE_LEFT]))	{ glm_vec3_sub(move_dir, move_right, move_dir); moved = true; }
	if (keyHeld(g_keybinds[ACTION_MOVE_RIGHT]))	{ glm_vec3_add(move_dir, move_right, move_dir); moved = true; }
	if (keyHeld(g_keybinds[ACTION_JUMP]))		{ glm_vec3_add(move_dir, move_up, move_dir); moved = true; }
	if (keyHeld(g_keybinds[ACTION_SHIFT]))		{ glm_vec3_sub(move_dir, move_up, move_dir); moved = true; }

	// 5. Apply movement
	if (moved) {
		glm_vec3_normalize(move_dir);

		vec3	displacement;
		glm_vec3_scale(move_dir, p->movSpeed * dt, displacement);

		if (noclip) {
			glm_vec3_add(camera->position, displacement, camera->position);
		} else {
			vec3	desired_pos;
			glm_vec3_add(p_ent->pos, displacement, desired_pos);
			movePlayer(p_ent->pos, desired_pos);
		}
	}

	// 6. Orbit Camera (Third Person View)
	if (!noclip) {
		vec3	target_pos;
		vec3	offset;

		// Focus on the upper body/head, not the feet
		glm_vec3_copy(p_ent->pos, target_pos);
		target_pos[1] += 1.5f; 

		// Push the camera backward along its own negative front vector using dynamic orbit distance
		glm_vec3_scale(camera->front, -p->orbit_dist, offset);
		glm_vec3_add(target_pos, offset, camera->position);
	}
}

EntityHotData	*getEntityHot(EntityRegistry *reg, EntityHandle handle)
{
	// 1. Verify the entity hasn't been deleted and replaced
	if (reg->generations[handle.index] != handle.generation) {
		return NULL; 
	}

	// 2. Look up exactly where it currently lives in the dense array
	u32		dense_idx = reg->sparse_to_dense[handle.index];

	return &reg->hot_data[dense_idx];
}

EntityColdData	*getEntityCold(EntityRegistry *reg, EntityHandle handle)
{
	if (reg->generations[handle.index] != handle.generation) {
		return NULL;
	}

	u32		dense_idx = reg->sparse_to_dense[handle.index];

	return &reg->cold_data[dense_idx];
}

EntityHandle	createEntity(EntityRegistry *reg, ModelCache *cache, GraphicsContext *ctx, String model_path)
{
	assert(reg->count < reg->capacity);
	assert(reg->free_count > 0);

	// 1. Grab a permanent sparse index (the entity's ID)
	reg->free_count--;
	u32		sparse_idx = reg->free_indices[reg->free_count];

	// 2. The data will live at the very end of the packed arrays
	u32		dense_idx = reg->count;
	reg->count++;

	// 3. Link them together
	reg->sparse_to_dense[sparse_idx] = dense_idx;
	reg->dense_to_sparse[dense_idx] = sparse_idx;

	// 4. Invalidate old handles that used this sparse index
	reg->generations[sparse_idx]++;

	// 5. Initialize Hot Data
	EntityHotData	*hot = &reg->hot_data[dense_idx];
	
	// Default transform
	hot->pos[0] = 0.0f; hot->pos[1] = 0.0f; hot->pos[2] = 0.0f;
	hot->rotation[0] = 0.0f; hot->rotation[1] = 0.0f; hot->rotation[2] = 0.0f; hot->rotation[3] = 1.0f;
	hot->scale = 1.0f;

	// Load the model from the cache!
	hot->model_handle = modelCacheAcquire(ctx, model_path);

	// Set default pipeline
	hot->pipeline_state = createPipelineState(
		ctx->pbr_shader_handle,
		CULL_BACK,
		BLEND_NONE,
		COMPARE_LESS,
		DEPTH_RW,
		TOPO_TRIANGLES,
		PIPELINE_TYPE_GRAPHICS,
		PASS_MAIN
	);

	// 6. Initialize Cold Data
	EntityColdData	*cold = &reg->cold_data[dense_idx];
	hot->active = true;
	memcpy(cold->str, "New entity", 11);

	// 7. Return the safe handle
	EntityHandle	handle;
	handle.index = sparse_idx;
	handle.generation = reg->generations[sparse_idx];

	return handle;
}

void	initEntityRegistry(EntityRegistry *reg, Allocator *a, u32 max_entities)
{
	reg->count		= 0;
	reg->capacity		= max_entities;

	// Dense arrays (packed tightly for the game loop / extraction)
	reg->hot_data		= a->fp_allocation(a, sizeof(EntityHotData) * max_entities, DEFAULT_ALIGN);
	reg->cold_data		= a->fp_allocation(a, sizeof(EntityColdData) * max_entities, DEFAULT_ALIGN);
	reg->dense_to_sparse	= a->fp_allocation(a, sizeof(u32) * max_entities, DEFAULT_ALIGN);

	// Sparse arrays (indexed by handle.index)
	reg->sparse_to_dense	= a->fp_allocation(a, sizeof(u32) * max_entities, DEFAULT_ALIGN);
	reg->generations	= a->fp_allocation(a, sizeof(u32) * max_entities, DEFAULT_ALIGN);

	// Freelist for recycling sparse indices
	reg->free_indices	= a->fp_allocation(a, sizeof(u32) * max_entities, DEFAULT_ALIGN);
	reg->free_count		= max_entities;

	// Populate freelist backwards so index 0 is popped first
	for (u32 i = 0; i < max_entities; i++) {
		reg->free_indices[i] = (max_entities - 1) - i;
		reg->generations[i] = 0;
	}
}

void	initPlayer(Player *p, World *world, String player_model)
{
	p->movSpeed = 0.1f;
	p->p_entity = createEntity(&world->entity_registry, g_model_cache, world->graphics_ctx, player_model);

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
