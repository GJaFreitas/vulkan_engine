#include "graphics_layer.h"
#include "stb_sprintf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

static void	loadFromPNG(GraphicsContext *ctx, tg3_model model, tg3_image image, Texture *tex)
{
	const tg3_buffer_view	buffer_view = model.buffer_views[image.buffer_view];
	const tg3_buffer	buffer = model.buffers[buffer_view.buffer];
	const tg3_span_u8	buf_data = buffer.data;


	i32	width, height, channels;
	u8	*pixels;

	pixels = stbi_load_from_memory(buf_data.data + buffer_view.byte_offset, buffer_view.byte_length, &width, &height, &channels, STBI_rgb_alpha);

	// PNGs come in this format
	VkFormat	format = VK_FORMAT_R8G8B8A8_SRGB;
	VkExtent3D	extent = {
		.width = width,
		.height = height,
		// 2D image
		.depth = 1
	};
	// Mip maps are basically scaled down images that are precalculated, each level is half the size
	// of the previous one so the total levels is how many times you can halve a texture +1 for the
	// first full sized map
	u32	mip_levels = (u32)floor(log2(fmax(width, height))) + 1;

	VkImageCreateInfo	image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = extent,
		.mipLevels = mip_levels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	void	*image_allocation;
	VkImage	vk_image;
	// Create, allocate and bind in one call, vma is awesome
	if (wrapperVMAcreateImage(ctx->vma_allocator, &image_info, &vk_image, &image_allocation) != VK_SUCCESS) {
		engine_log("Gltf loading", "Failed to create image: %S", tex->name);
	}

	tex->gpu_image = vk_image;
	tex->gpu_image_alloc = image_allocation;


	// Creating a staging buffer for upload
	// rgba format means 4 bytes per pixel
	VkDeviceSize	image_size = width * height * 4;
	VkBuffer	staging_buffer;

	VkBufferCreateInfo	staging_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = image_size,
		.usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	void	*staging_allocation;
	wrapperVMAcreateBuffer(ctx->vma_allocator, &staging_info, &staging_buffer, &staging_allocation, 1);
	void	*mapped;
	wrapperVMAmapMemory(ctx->vma_allocator, staging_allocation, &mapped);
	memcpy(mapped, pixels, image_size);
	wrapperVMAunmapMemory(ctx->vma_allocator, staging_allocation);
	stbi_image_free(pixels);

	// Submit the image to the GPU
	// Creating the command buffer
	// TODO: This part can be optimized A LOT
	VkCommandBuffer	cmd;
	VkCommandPool	cmd_pool;

	VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = ctx->queue_family_index
	};

	if (vkCreateCommandPool(ctx->device, &pool_info, NULL, &cmd_pool) != VK_SUCCESS) {
		engine_error("Gltf loading", "Failed to create command pool");
	}

	VkCommandBufferAllocateInfo buffer_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = cmd_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1
	};

	if (vkAllocateCommandBuffers(ctx->device, &buffer_alloc_info, &cmd) != VK_SUCCESS) {
		engine_error("Gltf loading", "Failed to allocate command buffer");
	}

	VkCommandBufferBeginInfo	cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(cmd, &cmd_begin_info);

	VkImageMemoryBarrier2 undefined_transfer = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = vk_image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT
	};

	VkDependencyInfo	dep_info = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &undefined_transfer
	};

	vkCmdPipelineBarrier2(cmd, &dep_info);

	VkBufferImageCopy2	region = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
		.bufferOffset = 0,
		.bufferRowLength = 0,	// Image is packed
		.bufferImageHeight = 0,	// Image is packed
		.imageExtent = extent,
		.imageOffset = {0, 0, 0},
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VkCopyBufferToImageInfo2	copy_info = {
		.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
		.srcBuffer = staging_buffer,
		.dstImage = vk_image,
		.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.regionCount = 1,
		.pRegions = &region
	};
	vkCmdCopyBufferToImage2(cmd, &copy_info);

	VkImageMemoryBarrier2 transfer_shaderronly = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = vk_image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
		.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	};

	VkDependencyInfo	dep_info_2 = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &transfer_shaderronly
	};
	vkCmdPipelineBarrier2(cmd, &dep_info_2);

	vkEndCommandBuffer(cmd);

	VkCommandBufferSubmitInfo	cmd_submit_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = cmd,
	};
	VkSubmitInfo2 submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmd_submit_info
	};
	// TODO: Make these uploads record everything first before submiting
	vkQueueSubmit2(ctx->queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(ctx->queue);
	vkDestroyCommandPool(ctx->device, cmd_pool, NULL);
	wrapperVMAdestroyBuffer(ctx->vma_allocator, staging_buffer, staging_allocation);

	VkImageViewCreateInfo	image_view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.image = vk_image,
		.format = format,
		.components = {
			VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY,
			VK_COMPONENT_SWIZZLE_IDENTITY,
		},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
	};
	if (vkCreateImageView(ctx->device, &image_view_info, NULL, &tex->image_view) != VK_SUCCESS) {
		engine_error("Gltf loading", "Failed to create image view");
	}

	VkSamplerCreateInfo	sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		// How to sample when texture is magnified or minified
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.minLod = 0,
		.maxLod = mip_levels,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		// What happens when axes go outside 0 - 1 range
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		// Need to check feature for this
		.anisotropyEnable = VK_FALSE
	};
	vkCreateSampler(ctx->device, &sampler_info, NULL, &tex->sampler);
}

void	gltf_load(String filename, GLTFModel *model, GraphicsContext *ctx)
{
	tg3_parse_options	opts;
	tg3_model		gltf_model;

	tg3_parse_options_init(&opts);
	tg3_error_stack_init(&model->errors);

	opts.fs = callbacks;

	tg3_error_code err = tg3_parse_file(&gltf_model, &model->errors, (char *)filename.data, filename.count, &opts);
	if (err != TG3_OK) {
		for (uint32_t i = 0; i < model->errors.count; i++) {
			fprintf(stderr, "[%d] %s\n", (int)model->errors.entries[i].severity,
	   model->errors.entries[i].message ? model->errors.entries[i].message : "(null)");
		}
	}

	// TODO: This is bad memory allocation
	Texture	*textures = calloc(gltf_model.textures_count, sizeof(Texture));
	for (u32 i = 0; i < gltf_model.textures_count; i++) {
		const tg3_texture	gltf_texture = gltf_model.textures[i];
		const tg3_image		gltf_image = gltf_model.images[gltf_texture.source];


		Texture	tex = {};

		if (gltf_image.name.len) {
			tex.name = stringCopy((String){(u8 *)gltf_image.name.data, gltf_image.name.len});
		} else {
			String tex_string = STRING_LIT("texture_");
			char	buf[32];
			stbsp_snprintf(buf, 32, "%S%i", tex_string, i);
			tex.name = createString(buf);
		}
		char	printbuffer[128];
		stbsp_snprintf(printbuffer, 128, "%S", tex.name);
		printf("%s\n", printbuffer);
		memset(printbuffer, 0, 128);

		StringView	mime_type;
		mime_type.data = (u8 *)gltf_image.mime_type.data;
		mime_type.count = gltf_image.mime_type.len;
		// mime_type is at '/'
		stringViewJumpToChar(&mime_type, '/');
		// mime_type is after '/'
		stringViewAdvance(&mime_type, 1);

		if (stringIsEqual(mime_type, STRING_LIT("png"))) {
			loadFromPNG(ctx, gltf_model, gltf_image, &tex);
		} else {
			engine_error("Gltf loading", "Unrecognized mime type, please come implement it");
		}

		textures[i] = tex;
	}
	model->textures = textures;


	Material	*materials = calloc(gltf_model.materials_count, sizeof(Texture));
	for (u32 i = 0; i < gltf_model.materials_count; i++) {
		const tg3_material	gltf_material = gltf_model.materials[i];

		Material	mat = {};

		// Base color
		mat.base_color_factor[0] = gltf_material.pbr_metallic_roughness.base_color_factor[0];
		mat.base_color_factor[1] = gltf_material.pbr_metallic_roughness.base_color_factor[1];
		mat.base_color_factor[2] = gltf_material.pbr_metallic_roughness.base_color_factor[2];
		mat.base_color_factor[3] = gltf_material.pbr_metallic_roughness.base_color_factor[3];

		// Metalic and roughness factors
		mat.roughness_factor = gltf_material.pbr_metallic_roughness.roughness_factor;
		mat.metallic_factor = gltf_material.pbr_metallic_roughness.metallic_factor;

		// Associate textures with the material

		if (gltf_material.pbr_metallic_roughness.base_color_texture.index >= 0) {
			const tg3_texture	gltf_texture =
				gltf_model.textures[gltf_material.pbr_metallic_roughness.base_color_texture.index];
			mat.base_color_texture = textures[gltf_texture.source];
		}

		if (gltf_material.normal_texture.index >= 0) {
			const tg3_texture	gltf_texture =
				gltf_model.textures[gltf_material.normal_texture.index];
			mat.normal_texture = textures[gltf_texture.source];
		}

		if (gltf_material.occlusion_texture.index >= 0) {
			const tg3_texture	gltf_texture =
				gltf_model.textures[gltf_material.occlusion_texture.index];
			mat.occlusion_texture = textures[gltf_texture.source];
		}

		if (gltf_material.emissive_texture.index >= 0) {
			const tg3_texture	gltf_texture =
				gltf_model.textures[gltf_material.emissive_texture.index];
			mat.emissive_texture = textures[gltf_texture.source];
		}


	}
	model->materials = materials;

	engine_log("Gltf loading", "Successfully loaded texture %S", filename);
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

// TODO: This function
void	gltf_destroy(GLTFModel model)
{
}
