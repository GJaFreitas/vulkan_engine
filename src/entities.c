#include "world.h"

void modelMatFromPosDir(vec3 position, vec3 direction, mat4 dest)
{
	vec3 forward, right, up;
	vec3 world_up = {0.0f, 1.0f, 0.0f};

	glm_vec3_normalize_to(direction, forward);

	// Handle the edge case where direction is parallel to world_up
	// (cross product would be zero/undefined)
	if (fabsf(glm_vec3_dot(forward, world_up)) > 0.999f) {
		world_up[0] = 1.0f; world_up[1] = 0.0f; world_up[2] = 0.0f;
	}

	glm_vec3_cross(world_up, forward, right);
	glm_vec3_normalize(right);
	glm_vec3_cross(forward, right, up);
	glm_vec3_normalize(up);

	glm_mat4_identity(dest);
	// Column-major basis vectors
	dest[0][0] = right[0];   dest[0][1] = right[1];   dest[0][2] = right[2];
	dest[1][0] = up[0];      dest[1][1] = up[1];      dest[1][2] = up[2];
	dest[2][0] = forward[0]; dest[2][1] = forward[1]; dest[2][2] = forward[2];
	dest[3][0] = position[0]; dest[3][1] = position[1]; dest[3][2] = position[2];
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

EntityRenderInfo	buildRenderInfo(Vector *entity_vector, u32 model_count, Allocator *a)
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

		render_idx++;
		entity_info.data[render_idx].model_idx = model_idx;
		modelMatFromPosDir(ent->pos, ent->direction, entity_info.data[render_idx].instance_data.model_mat);
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

Entity	*loadEntity(GraphicsContext *ctx, String model_path, vec3 pos, vec3 dir, Allocator *a)
{
	Entity	*e = a->fp_allocation(a, sizeof(Entity), DEFAULT_ALIGN);

	e->model = modelCacheAcquire(ctx, model_path);
	glm_vec3_copy(pos, e->pos);
	glm_vec3_copy(dir, e->direction);
	e->active = true;
	return e;
}

void	initPlayer(Player *p)
{
	p->movSpeed = 0.1f;


	Camera	*camera = &p->camera;

	glm_vec3_copy((vec3){0.0f, 0.4f, 0.0f},  camera->position);
	glm_vec3_copy((vec3){0.0f, 0.0f, -1.0f}, camera->front);
	glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f},  camera->up);
	glm_vec3_copy((vec3){1.0f, 0.0f, 0.0f},  camera->right);
	glm_vec3_copy((vec3){0.0f, 1.0f, 0.0f},  camera->worldUp);
	camera->yaw = -90.0f;
	camera->pitch = 0;
	camera->zoom = 45.0f;

	camera->mouseSensitivity = 0.1f;
	camera->movementSpeed = 0.7f;
}
