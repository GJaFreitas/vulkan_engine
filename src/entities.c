#include "world.h"
#include "console.h"

void	spawnCommand(int argc, String *argv)
{
	consoleAppend("To be implemented");
}

void	updateEntities(Vector *entities, double dt)
{
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

void	updatePlayer(Player *p, double dt, SDL_Window *window)
{
	Camera	*camera = &p->camera;
	vec3 move = {0};

	if (g_engine_mode != ENGINE_MODE_GAME) return ;

	if (key_held(g_keybinds[ACTION_MOVE_FORWARD])) {
		glm_vec3_muladds(camera->front, p->movSpeed * dt, move);
	} else if (key_held(g_keybinds[ACTION_MOVE_LEFT])) {
		glm_vec3_muladds(camera->right, -p->movSpeed * dt, move);
	} else if (key_held(g_keybinds[ACTION_MOVE_RIGHT])) {
		glm_vec3_muladds(camera->right, p->movSpeed * dt, move);
	} else if (key_held(g_keybinds[ACTION_MOVE_BACKWARD])) {
		glm_vec3_muladds(camera->front, -p->movSpeed * dt, move);
	} else if (key_held(g_keybinds[ACTION_JUMP])) {
		glm_vec3_muladds(camera->worldUp, p->movSpeed * dt, move);
	} else if (key_held(g_keybinds[ACTION_SHIFT])) {
		glm_vec3_muladds(camera->worldUp, -p->movSpeed * dt, move);
	}

	glm_vec3_add(camera->position, move, camera->position);
}

void	initPlayer(Player *p)
{
	p->movSpeed = 0.1f;


	Camera	*camera = &p->camera;
	glm_vec3_copy((vec3){0.0f,1.0f,4.0f},  camera->position);
	glm_vec3_copy((vec3){0.0f, 0.0f, -1.0f}, camera->front);
	glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f},  camera->up);
	glm_vec3_copy((vec3){1.0f, 0.0f, 0.0f},  camera->right);
	glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f},  camera->worldUp);
	camera->yaw = -90.0f;
	camera->pitch = 0;
	camera->zoom = 45.0f;

	camera->mouseSensitivity = 0.1f;
	camera->movementSpeed = 0.01f;
}
