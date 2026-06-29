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

static inline String	tg3_to_String(tg3_str str)
{
	String	s;

	s.count = str.len;
	s.data = (u8 *)str.data;
	return s;
}

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
	// »speed
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

internal void	gltfLoadTextures(GraphicsContext *ctx, GLTFModel *model, tg3_model gltf_model)
{

	// TODO: This is bad memory allocation
	model->textures = malloc(gltf_model.textures_count * sizeof(Texture));
	model->texture_count = gltf_model.textures_count;
	for (u32 i = 0; i < gltf_model.textures_count; i++) {
		const tg3_texture	gltf_texture = gltf_model.textures[i];
		const tg3_image		gltf_image = gltf_model.images[gltf_texture.source];


		Texture	tex = {};

		if (gltf_image.name.len) {
			tex.name = stringCopy(tg3_to_String(gltf_image.name));
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

		StringView	mime_type = tg3_to_String(gltf_image.mime_type);
		// mime_type is at '/'
		stringViewJumpToChar(&mime_type, '/');
		// mime_type is after '/'
		stringViewAdvance(&mime_type, 1);

		if (stringIsEqual(mime_type, STRING_LIT("png"))) {
			loadFromPNG(ctx, gltf_model, gltf_image, &tex);
		} else {
			engine_error("Gltf loading", "Unrecognized mime type, please come implement it");
		}

		model->textures[i] = tex;
	}

}

internal void	gltfLoadMaterials(GLTFModel *model, tg3_model gltf_model)
{
	model->materials = malloc(gltf_model.materials_count * sizeof(Material));
	model->material_count = gltf_model.materials_count;
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
			mat.base_color_texture = model->textures[gltf_texture.source];
		}

		if (gltf_material.normal_texture.index >= 0) {
			const tg3_texture	gltf_texture =
				gltf_model.textures[gltf_material.normal_texture.index];
			mat.normal_texture = model->textures[gltf_texture.source];
		}

		if (gltf_material.occlusion_texture.index >= 0) {
			const tg3_texture	gltf_texture =
				gltf_model.textures[gltf_material.occlusion_texture.index];
			mat.occlusion_texture = model->textures[gltf_texture.source];
		}

		if (gltf_material.emissive_texture.index >= 0) {
			const tg3_texture	gltf_texture =
				gltf_model.textures[gltf_material.emissive_texture.index];
			mat.emissive_texture = model->textures[gltf_texture.source];
		}

		model->materials[i] = mat;
	}
}

internal void	gltfBuildSceneGraph(GLTFModel *model, tg3_model gltf_model)
{
	// TODO: Bad allocation
	model->linear_nodes = malloc(sizeof(Node) * gltf_model.nodes_count);
	model->node_count = gltf_model.nodes_count;
	for (u32 i = 0; i < gltf_model.nodes_count; i++) {
		const tg3_node	gltf_node = gltf_model.nodes[i];

		Node	node = {};

		node.name = tg3_to_String(gltf_model.nodes[i].name);
		node.index = i;
		node.parent = -1;

		node.children_count = gltf_node.children_count;
		if (gltf_node.children_count != 0)
			node.children = malloc(sizeof(u32) * gltf_node.children_count);

		node.rotation[0] = gltf_node.rotation[0];
		node.rotation[1] = gltf_node.rotation[1];
		node.rotation[2] = gltf_node.rotation[2];
		node.rotation[3] = gltf_node.rotation[3];
		node.scale[0] = gltf_node.scale[0];
		node.scale[1] = gltf_node.scale[1];
		node.scale[2] = gltf_node.scale[2];
		node.translation[0] = gltf_node.translation[0];
		node.translation[1] = gltf_node.translation[1];
		node.translation[2] = gltf_node.translation[2];

		model->linear_nodes[i] = node;
	}

	for (u32 n = 0; n < gltf_model.nodes_count; n++) {
		const tg3_node	gltf_node = gltf_model.nodes[n];

		for (u32 c = 0; c < gltf_node.children_count; c++) {
			// at the childs index node[c].index the parent is the current node
			model->linear_nodes[gltf_node.children[c]].parent = n;
			// The current node getting the childs index
			model->linear_nodes[n].children[c] = gltf_node.children[c];
		}
	}

}


internal void	gltfSetMeshData(GLTFModel *model, tg3_model gltf_model)
{
	for (u32 n = 0; n < gltf_model.nodes_count; n++) {
		const tg3_node	gltf_node = gltf_model.nodes[n];

		if (gltf_node.mesh >= 0) {
			const tg3_mesh	gltf_mesh = gltf_model.meshes[gltf_node.mesh];

			for (u32 p = 0; p < gltf_mesh.primitives_count; p++) {
				const tg3_primitive	primitive = gltf_mesh.primitives[p];

				Mesh	mesh = {};

				mesh.material_index = primitive.material;

				// The index for the attributes
				i32	pos_idx = -1, normal_idx = -1, uv_idx = -1;
				for (u32 a = 0; a < primitive.attributes_count; a++) {
					if (stringIsEqual(tg3_to_String(primitive.attributes[a].key), STRING_LIT("POSITION"))) {
						pos_idx = primitive.attributes[a].value;
					} else if (stringIsEqual(tg3_to_String(primitive.attributes[a].key), STRING_LIT("NORMAL"))) {
						normal_idx = primitive.attributes[a].value;
					} else if (stringIsEqual(tg3_to_String(primitive.attributes[a].key), STRING_LIT("TEXCOORD_0"))) {
						uv_idx = primitive.attributes[a].value;
					}
				}

				const tg3_accessor *pos_acc    = &gltf_model.accessors[pos_idx];
				const tg3_accessor *normal_acc = &gltf_model.accessors[normal_idx];
				const tg3_accessor *uv_acc     = &gltf_model.accessors[uv_idx];

				const tg3_buffer_view *pos_bv    = &gltf_model.buffer_views[pos_acc->buffer_view];
				const tg3_buffer_view *normal_bv = &gltf_model.buffer_views[normal_acc->buffer_view];
				const tg3_buffer_view *uv_bv     = &gltf_model.buffer_views[uv_acc->buffer_view];

				float *positions = (float *)(gltf_model.buffers[pos_bv->buffer].data.data + pos_bv->byte_offset + pos_acc->byte_offset);
				float *normals   = (float *)(gltf_model.buffers[normal_bv->buffer].data.data + normal_bv->byte_offset + normal_acc->byte_offset);
				float *uvs       = (float *)(gltf_model.buffers[uv_bv->buffer].data.data + uv_bv->byte_offset + uv_acc->byte_offset);

				mesh.vertex_count = (u32)pos_acc->count;
				// TODO: Bad allocation
				mesh.vertices = malloc(sizeof(Vertex) * mesh.vertex_count);
				for (u32 i = 0; i < mesh.vertex_count; i++) {
					mesh.vertices[i].pos[0]    = positions[i*3];
					mesh.vertices[i].pos[1]    = positions[i*3+1];
					mesh.vertices[i].pos[2]    = positions[i*3+2];
					mesh.vertices[i].normal[0] = normals[i*3];
					mesh.vertices[i].normal[1] = normals[i*3+1];
					mesh.vertices[i].normal[2] = normals[i*3+2];
					mesh.vertices[i].uv[0]     = uvs[i*2];
					mesh.vertices[i].uv[1]     = uvs[i*2+1];
				}

				// Indices
				const tg3_accessor *idx_acc = &gltf_model.accessors[primitive.indices];
				const tg3_buffer_view *idx_bv = &gltf_model.buffer_views[idx_acc->buffer_view];
				void *idx_data = (void *)(gltf_model.buffers[idx_bv->buffer].data.data + idx_bv->byte_offset + idx_acc->byte_offset);
				mesh.index_count = (u32)idx_acc->count;
				u8	index_type_size;
				if (idx_acc->component_type == 5121) {
					mesh.index_type = VK_INDEX_TYPE_UINT8;
					index_type_size = 1;
				}
				else if (idx_acc->component_type == 5123) {
					mesh.index_type = VK_INDEX_TYPE_UINT16;
					index_type_size = 2;
				}
				else {
					mesh.index_type = VK_INDEX_TYPE_UINT32;
					index_type_size = 4;
				}
				// TODO: Bad allocation
				mesh.indices = malloc(index_type_size * mesh.index_count);
				memcpy(mesh.indices, idx_data, index_type_size * mesh.index_count);

				model->linear_nodes[n].mesh = mesh;
			}
		}
	}
}

internal void	gltfLoadAnimations(GLTFModel *model, tg3_model gltf_model)
{
	// TODO: Bad allocation
	model->animation_count = gltf_model.animations_count;
	model->animations = malloc(sizeof(Animation) * gltf_model.animations_count);
	for (u32 a = 0; a < gltf_model.animations_count; a++) {
		const tg3_animation	gltf_animation = gltf_model.animations[a];

		Animation	animation = {};

		animation.name = tg3_to_String(gltf_animation.name);
		animation.channels_count = gltf_animation.channels_count;
		animation.channels = malloc(sizeof(AnimationChannel) * gltf_animation.channels_count);
		animation.samplers_count = gltf_animation.samplers_count;
		animation.samplers = malloc(sizeof(AnimationSampler) * gltf_animation.samplers_count);

		for (u32 s = 0; s < gltf_animation.samplers_count; s++) {
			const tg3_animation_sampler	gltf_sampler = gltf_animation.samplers[s];

			AnimationSampler	sampler = {};

			if (stringIsEqual(tg3_to_String(gltf_sampler.interpolation), STRING_LIT("CUBICSPLINE"))) {
				sampler.interpolation = CUBICSPLINE;
			} else if (stringIsEqual(tg3_to_String(gltf_sampler.interpolation), STRING_LIT("STEP"))) {
				sampler.interpolation = STEP;
			} else if (stringIsEqual(tg3_to_String(gltf_sampler.interpolation), STRING_LIT("LINEAR"))) {
				sampler.interpolation = LINEAR;
			} else {
				engine_error("Gltf loading", "Unrecognized interpolation type");
			}

			sampler.input = gltf_sampler.input;
			sampler.output = gltf_sampler.output;

			animation.samplers[s] = sampler;
		}

		for (u32 c = 0; c < gltf_animation.channels_count; c++) {
			const tg3_animation_channel	gltf_channel = gltf_animation.channels[c];
			const tg3_animation_channel_target	gltf_target = gltf_channel.target;

			AnimationChannel	channel = {};

			if (stringIsEqual(tg3_to_String(gltf_target.path), STRING_LIT("TRANSLATION"))) {
				channel.target.path = TRANSLATION;
			} else if (stringIsEqual(tg3_to_String(gltf_target.path), STRING_LIT("ROTATION"))) {
				channel.target.path = ROTATION;
			} else if (stringIsEqual(tg3_to_String(gltf_target.path), STRING_LIT("SCALE"))) {
				channel.target.path = SCALE;
			} else if (stringIsEqual(tg3_to_String(gltf_target.path), STRING_LIT("WEIGHTS"))) {
				channel.target.path = WEIGHTS;
			} else {
				engine_error("Gltf loading", "Unrecognized channel path type");
			}

			channel.sampler = gltf_channel.sampler;
			channel.target.node = gltf_target.node;

			animation.channels[c] = channel;
		}

		model->animations[a] = animation;
	}
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

	gltfLoadTextures(ctx, model, gltf_model);
	gltfLoadMaterials(model, gltf_model);
	gltfBuildSceneGraph(model, gltf_model);
	gltfSetMeshData(model, gltf_model);
	gltfLoadAnimations(model, gltf_model);

	engine_log("Gltf loading", "Successfully loaded texture %S", filename);
}

void	createDescriptorSetsForMaterials(Material *materials, u32 material_count)
{
	VkDescriptorSetLayoutBinding	bindings[] = {
		// Base Color Texture binding
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		// Metalic roughness binding
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		// Normal map binding
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		// Occlusion map binding
		{
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
		// Emissive map binding
		{
			.binding = 4,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
		},
	};

	VkDescriptorSetLayoutCreateInfo	descriptor_set_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.bindingCount = sizeofarray(bindings),
		.pBindings = bindings
	};

}

// TODO: This function
void	gltf_destroy(GLTFModel model)
{
}
