#include "graphics_layer.h"
#include "base_layer.h"
#include "vars.h"

void gltf_debug(GLTFModel *gltf)
{
	tg3_model *model = &gltf->model;
	tg3_primitive *prim = &model->meshes[0].primitives[0];

	int pos_idx = -1, normal_idx = -1, uv_idx = -1;
	for (uint32_t i = 0; i < prim->attributes_count; i++) {
		if (strcmp(prim->attributes[i].key.data, "POSITION") == 0)
			pos_idx = prim->attributes[i].value;
		else if (strcmp(prim->attributes[i].key.data, "NORMAL") == 0)
			normal_idx = prim->attributes[i].value;
		else if (strcmp(prim->attributes[i].key.data, "TEXCOORD_0") == 0)
			uv_idx = prim->attributes[i].value;
	}

	fprintf(stderr, "pos_idx: %d normal_idx: %d uv_idx: %d\n", pos_idx, normal_idx, uv_idx);

	tg3_accessor *pos_acc = &model->accessors[pos_idx];
	tg3_buffer_view *pos_bv = &model->buffer_views[pos_acc->buffer_view];
	float *positions = (float *)(model->buffers[pos_bv->buffer].data.data + pos_bv->byte_offset + pos_acc->byte_offset);
	fprintf(stderr, "vertex_count: %lu\n", pos_acc->count);
	for (int i = 0; i < 5; i++)
		fprintf(stderr, "pos[%d]: %f %f %f\n", i, positions[i*3], positions[i*3+1], positions[i*3+2]);

	tg3_accessor *idx_acc = &model->accessors[prim->indices];
	fprintf(stderr, "index component_type: %d count: %lu\n", idx_acc->component_type, idx_acc->count);
	tg3_buffer_view *idx_bv = &model->buffer_views[idx_acc->buffer_view];
	void *idx_data = (void *)(model->buffers[idx_bv->buffer].data.data + idx_bv->byte_offset + idx_acc->byte_offset);
	if (idx_acc->component_type == 5123) {
		uint16_t *idx = (uint16_t *)idx_data;
		for (int i = 0; i < 9; i += 3)
			fprintf(stderr, "tri[%d]: %u %u %u\n", i/3, idx[i], idx[i+1], idx[i+2]);
	} else {
		uint32_t *idx = (uint32_t *)idx_data;
		for (int i = 0; i < 9; i += 3)
			fprintf(stderr, "tri[%d]: %u %u %u\n", i/3, idx[i], idx[i+1], idx[i+2]);
	}
}

int	loop()
{

	GraphicsContext	ctx = {0};


	startGraphics(&ctx);
	// TODO: Create destruction function
	gltf_load(STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"), &ctx.model, &ctx);
	gltf_debug(&ctx.model);


	bool	running = true;
	while (running)
	{
		SDL_Event	event = {0};
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
				break ;
			} else if (event.key.key == SDLK_ESCAPE) {
				running = false;
				break ;
			}
			else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				ctx.window_width = event.window.data1;
				ctx.window_height = event.window.data2;
				break ;
			}
		}
		render(&ctx);
		check_var_modify();
	}

	endGraphics(&ctx);
	return (0);
}

int	main(void)
{
	GLTFModel	model;
	start_logs();
	init_vars();
	loop();
	// gltf_load(STRING_LIT("data/models/GlassHurricaneCandleHolder.glb"), &model);
	printf("\n\n\n");
	return (0);
}
