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

EntityRenderInfo	buildRenderInfo(Vector *entity_vector, Allocator *a)
{
	const u64	entity_count = entity_vector->used;

	EntityRenderInfo	entity_info = {};

	entity_info.render_data_arr = a->fp_allocation(a, sizeof(EntityRenderData) * entity_count, DEFAULT_ALIGN);
	entity_info.enabled_arr = a->fp_allocation(a, sizeof(bool) * entity_count, DEFAULT_ALIGN);

	entity_info.count = entity_count;

	for (u64 i = 0; i < entity_count; i++) {
		Entity	**ent_ptr = vectorGet(entity_vector, i);
		Entity	*ent = *ent_ptr;

		entity_info.enabled_arr[i] = ent->active;
		if (!ent->active) continue;

		entity_info.render_data_arr[i].model = ent->model;
		modelMatFromPosDir(ent->pos, ent->direction, entity_info.render_data_arr[i].instance_data.model_mat);
	}

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
