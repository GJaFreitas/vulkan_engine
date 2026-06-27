#include "graphics_layer.h"

void gltf_upload(GraphicsContext *ctx, GLTFModel *gltf);

i32	file_exists_fn(const char *path, uint32_t path_len, void *user_data)
{
	(void)user_data;
	(void)path_len;
	return access(path, F_OK);
}

i32	read_file_fn(uint8_t **out_data, uint64_t *out_size,
		 const char *path, uint32_t path_len, void *user_data)
{
	(void)user_data;
	(void)path_len;
	int	fd = open(path, O_RDWR);
	if (fd == -1) {
		engine_error("gltf_loading.c", "Failed to open file: %s\n", path);
		return 0;
	}

	struct stat	stats;
	fstat(fd, &stats);

	u8	*data = mmap(NULL, stats.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		engine_error("gltf_loading.c", "Failed to map file: %s\n", path);
		return 0;
	}
	close(fd);

	*out_data = data;
	*out_size = stats.st_size;

	return 1;
}

void	free_file_fn(uint8_t *data, uint64_t size,
                                  void *user_data)
{
	(void)user_data;
	munmap(data, size);
}

i32	write_file_fn(const char *path, uint32_t path_len,
		  const uint8_t *data, uint64_t size, void *user_data)
{
	(void)path_len;
	(void)user_data;
	int	fd = open(path, O_RDWR);
	if (fd == -1) {
		engine_error("gltf_loading.c", "Failed to open file for writing: %s\n", path);
		return 0;
	}

	write(fd, data, size);
	close(fd);
	return (1);
}

i32	resolve_path_fn(char *out_path, uint32_t out_cap,
		    uint32_t *out_len, const char *path,
		    uint32_t path_len, void *user_data)
{
	(void)path_len;
	(void)out_path;
	(void)out_len;
	(void)path;
	(void)user_data;
	(void)out_cap;
	engine_log("gltf_loading.c", "The resolve_path_fn was called and i dont know what it does\n");
	return 0;
}

i32	get_file_size_fn(uint64_t *out_size, const char *path,
		     uint32_t path_len, void *user_data)
{
	(void)path_len;
	(void)user_data;
	int	fd = open(path, O_RDWR);
	if (fd == -1) {
		engine_error("gltf_loading.c", "Failed to open file: %s\n", path);
		return 0;
	}

	struct stat	stats;
	fstat(fd, &stats);
	*out_size = stats.st_size;
	return (1);
}

static tg3_fs_callbacks	callbacks = {
	.file_exists = file_exists_fn,
	.read_file = read_file_fn,
	.free_file = free_file_fn,
	.write_file = write_file_fn,
	.resolve_path = resolve_path_fn,
	.get_file_size = get_file_size_fn,
	.user_data = NULL
};

void	gltf_load(String filename, GLTFModel *model, GraphicsContext *ctx)
{
	tg3_parse_options opts;

	tg3_parse_options_init(&opts);
	tg3_error_stack_init(&model->errors);

	opts.fs = callbacks;

	tg3_error_code err = tg3_parse_file(&model->model, &model->errors, (char *)filename.data, filename.count, &opts);
	if (err != TG3_OK) {
		for (uint32_t i = 0; i < model->errors.count; i++) {
			fprintf(stderr, "[%d] %s\n", (int)model->errors.entries[i].severity,
	   model->errors.entries[i].message ? model->errors.entries[i].message : "(null)");
		}
	}

}

// void	createDescriptorSetsForMaterial(GLTFModel gltf_model)
// {
// 	VkDescriptorSetLayoutBinding	bindings[] = {
// 		// Base Color Texture binding
// 		{
// 			.binding = 0,
// 			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
// 			.descriptorCount = 1,
// 			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
// 		},
// 		// Metalic roughness binding
// 		{
// 			.binding = 1,
// 			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
// 			.descriptorCount = 1,
// 			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
// 		},
// 		// Normal map binding
// 		{
// 			.binding = 2,
// 			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
// 			.descriptorCount = 1,
// 			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
// 		},
// 		// Occlusion map binding
// 		{
// 			.binding = 3,
// 			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
// 			.descriptorCount = 1,
// 			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
// 		},
// 		// Emissive map binding
// 		{
// 			.binding = 4,
// 			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
// 			.descriptorCount = 1,
// 			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
// 		},
// 	};
//
// 	VkDescriptorSetLayoutCreateInfo	descriptor_set_info = {
// 		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
// 		.pNext = NULL,
// 		.flags = 0,
// 		.bindingCount = sizeofarray(bindings),
// 		.pBindings = bindings
// 	};
//
//
// 	tg3_model	model = gltf_model.model;
// 	u32	material_count = model.materials_count;
//
// 	for (u32 i = 0; i < material_count; i++) {
// 		const tg3_material material = model.materials[i];
//
// 		VkDescriptorSetAllocateInfo alloc_info = {
// 			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
// 			.descriptorPool
// 		}
// 	}
// }

void	gltf_destroy(GLTFModel model)
{
	tg3_model_free(&model.model);
	tg3_error_stack_free(&model.errors);
}
