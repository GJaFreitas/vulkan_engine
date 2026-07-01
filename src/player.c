#include "world.h"

static inline void	cameraMovement(float movSpeed, Camera *camera, enum CameraMovement direction, double dt_ms)
{
	float	velocity = movSpeed * dt_ms;
	vec3	scaled_dir;
	switch (direction) {
		case FORWARD:
			glm_vec3_scale(camera->front, velocity, scaled_dir);
			glm_vec3_add(camera->position, scaled_dir, camera->position);
		break;
		case BACKWARD:
			glm_vec3_scale(camera->front, velocity, scaled_dir);
			glm_vec3_sub(camera->position, scaled_dir, camera->position);
		break;
		case LEFT:
			glm_vec3_scale(camera->right, velocity, scaled_dir);
			glm_vec3_sub(camera->position, scaled_dir, camera->position);
		break;
		case RIGHT:
			glm_vec3_scale(camera->right, velocity, scaled_dir);
			glm_vec3_add(camera->position, scaled_dir, camera->position);
		break;
		case UP:
			glm_vec3_scale(camera->up, velocity, scaled_dir);
			glm_vec3_add(camera->position, scaled_dir, camera->position);
		break;
		case DOWN:
			glm_vec3_scale(camera->up, velocity, scaled_dir);
			glm_vec3_sub(camera->position, scaled_dir, camera->position);
		break;
	}
}

void	updatePlayer(Player *p, const bool *key_states, double dt_ms)
{
	if (key_states[SDL_SCANCODE_W]) {
		cameraMovement(p->movSpeed, &p->camera, FORWARD, dt_ms);
	}
	if (key_states[SDL_SCANCODE_A]) {
		cameraMovement(p->movSpeed, &p->camera, LEFT, dt_ms);
	}
	if (key_states[SDL_SCANCODE_S]) {
		cameraMovement(p->movSpeed, &p->camera, BACKWARD, dt_ms);
	}
	if (key_states[SDL_SCANCODE_D]) {
		cameraMovement(p->movSpeed, &p->camera, RIGHT, dt_ms);
	}
}

void	initPlayer(Player *p)
{
	p->movSpeed = 0.1f;


	Camera	*camera = &p->camera;

	glm_vec3_copy((vec3){0.0f, 0.0f, 1.0f},  camera->position);
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
