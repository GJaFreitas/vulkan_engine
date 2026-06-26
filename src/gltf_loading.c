#include "graphics_layer.h"

void gltf_upload(GraphicsContext *ctx, GLTFModel *gltf);

i32	file_exists_fn(const char *path, uint32_t path_len, void *user_data)
{
	return access(path, F_OK);
}

i32	read_file_fn(uint8_t **out_data, uint64_t *out_size,
		 const char *path, uint32_t path_len, void *user_data)
{
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
	munmap(data, size);
}

i32	write_file_fn(const char *path, uint32_t path_len,
		  const uint8_t *data, uint64_t size, void *user_data)
{
	int	fd = open(path, O_RDWR);
	if (fd == -1) {
		engine_error("gltf_loading.c", "Failed to open file for writing: %s\n", path);
		return 0;
	}

	write(fd, data, size);
	close(fd);
}

i32	resolve_path_fn(char *out_path, uint32_t out_cap,
		    uint32_t *out_len, const char *path,
		    uint32_t path_len, void *user_data)
{
	engine_log("gltf_loading.c", "The resolve_path_fn was called and i dont know what it does\n");
}

i32	get_file_size_fn(uint64_t *out_size, const char *path,
		     uint32_t path_len, void *user_data)
{
	int	fd = open(path, O_RDWR);
	if (fd == -1) {
		engine_error("gltf_loading.c", "Failed to open file: %s\n", path);
		return 0;
	}

	struct stat	stats;
	fstat(fd, &stats);
	return (stats.st_size);
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

	gltf_upload(ctx, model);
}

void	gltf_destroy(GLTFModel model)
{
	tg3_model_free(&model.model);
	tg3_error_stack_free(&model.errors);
}

typedef struct UploadCopyData {
	VkBuffer src;
	VkBuffer dst;
	VkDeviceSize size;
} UploadCopyData;

void copy_buffer_cmd(VkCommandBuffer cmd, void *data)
{
	UploadCopyData *d = (UploadCopyData *)data;
	VkBufferCopy region = { .size = d->size };
	vkCmdCopyBuffer(cmd, d->src, d->dst, 1, &region);
}

void gltf_upload(GraphicsContext *ctx, GLTFModel *gltf)
{
	tg3_model *model = &gltf->model;
	const tg3_primitive *prim = &model->meshes[0].primitives[0];

	// Find accessor indices
	int pos_idx = -1, normal_idx = -1, uv_idx = -1;
	for (uint32_t i = 0; i < prim->attributes_count; i++) {
		if (strcmp(prim->attributes[i].key.data, "POSITION") == 0)
			pos_idx = prim->attributes[i].value;
		else if (strcmp(prim->attributes[i].key.data, "NORMAL") == 0)
			normal_idx = prim->attributes[i].value;
		else if (strcmp(prim->attributes[i].key.data, "TEXCOORD_0") == 0)
			uv_idx = prim->attributes[i].value;
	}

	const tg3_accessor *pos_acc    = &model->accessors[pos_idx];
	const tg3_accessor *normal_acc = &model->accessors[normal_idx];
	const tg3_accessor *uv_acc     = &model->accessors[uv_idx];
	uint32_t vertex_count = (uint32_t)pos_acc->count;

	// Get raw buffer data for each accessor via buffer_view
	const tg3_buffer_view *pos_bv    = &model->buffer_views[pos_acc->buffer_view];
	const tg3_buffer_view *normal_bv = &model->buffer_views[normal_acc->buffer_view];
	const tg3_buffer_view *uv_bv     = &model->buffer_views[uv_acc->buffer_view];

	float *positions = (float *)(model->buffers[pos_bv->buffer].data.data    + pos_bv->byte_offset    + pos_acc->byte_offset);
	float *normals   = (float *)(model->buffers[normal_bv->buffer].data.data + normal_bv->byte_offset + normal_acc->byte_offset);
	float *uvs       = (float *)(model->buffers[uv_bv->buffer].data.data     + uv_bv->byte_offset     + uv_acc->byte_offset);

	// Build interleaved vertex array
	VkDeviceSize vertex_size = sizeof(Vertex) * vertex_count;
	Vertex *vertices = malloc(vertex_size);
	for (uint32_t i = 0; i < vertex_count; i++) {
		vertices[i].pos[0] = positions[i*3];
		vertices[i].pos[1] = positions[i*3+1];
		vertices[i].pos[2] = positions[i*3+2];
		vertices[i].normal[0] = normals[i*3];
		vertices[i].normal[1] = normals[i*3+1];
		vertices[i].normal[2] = normals[i*3+2];
		vertices[i].uv[0] = uvs[i*2];
		vertices[i].uv[1] = uvs[i*2+1];
	}

	// Index data
	const tg3_accessor *idx_acc = &model->accessors[prim->indices];
	const tg3_buffer_view *idx_bv = &model->buffer_views[idx_acc->buffer_view];
	const void *idx_data = model->buffers[idx_bv->buffer].data.data + idx_bv->byte_offset + idx_acc->byte_offset;
	gltf->index_count = (uint32_t)idx_acc->count;
	VkDeviceSize index_size = sizeof(uint32_t) * gltf->index_count;

	// GLTF indices may be u16, convert to u32
	uint32_t *indices = malloc(index_size);
	if (idx_acc->component_type == 5123) { // UNSIGNED_SHORT
		uint16_t *src = (uint16_t *)idx_data;
		for (uint32_t i = 0; i < gltf->index_count; i++)
			indices[i] = src[i];
	} else {
		memcpy(indices, idx_data, index_size);
	}

	// Create and upload vertex buffer
	VkBufferCreateInfo vbuf_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = vertex_size,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
	};
	VkBuffer staging_vbuf;
	void *staging_valloc;
	VkBufferCreateInfo staging_vbuf_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = vertex_size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	};
	wrapperVMAcreateBuffer(ctx->vma_allocator, &staging_vbuf_info, &staging_vbuf, &staging_valloc, true);
	void *mapped;
	wrapperVMAmapMemory(ctx->vma_allocator, staging_valloc, &mapped);
	memcpy(mapped, vertices, vertex_size);
	wrapperVMAunmapMemory(ctx->vma_allocator, staging_valloc);
	wrapperVMAcreateBuffer(ctx->vma_allocator, &vbuf_info, &gltf->vertex_buffer, &gltf->vertex_allocation, false);
	fprintf(stderr, "vertex_buffer handle: %p\n", (void*)gltf->vertex_buffer);
	fprintf(stderr, "vertex_count: %u\n", vertex_count);
	fprintf(stderr, "vertex_size: %lu\n", vertex_size);

	UploadCopyData vcopy = { staging_vbuf, gltf->vertex_buffer, vertex_size };
	immediate_submit(ctx, copy_buffer_cmd, &vcopy);
	wrapperVMAdestroyBuffer(ctx->vma_allocator, staging_vbuf, staging_valloc);

	// Create and upload index buffer
	VkBufferCreateInfo ibuf_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = index_size,
		.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
	};
	VkBuffer staging_ibuf;
	void *staging_ialloc;
	VkBufferCreateInfo staging_ibuf_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = index_size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT };
	wrapperVMAcreateBuffer(ctx->vma_allocator, &staging_ibuf_info, &staging_ibuf, &staging_ialloc, true);
	wrapperVMAmapMemory(ctx->vma_allocator, staging_ialloc, &mapped);
	memcpy(mapped, indices, index_size);
	wrapperVMAunmapMemory(ctx->vma_allocator, staging_ialloc);
	wrapperVMAcreateBuffer(ctx->vma_allocator, &ibuf_info, &gltf->index_buffer, &gltf->index_allocation, false);

	UploadCopyData icopy = { staging_ibuf, gltf->index_buffer, index_size };
	immediate_submit(ctx, copy_buffer_cmd, &icopy);
	wrapperVMAdestroyBuffer(ctx->vma_allocator, staging_ibuf, staging_ialloc);


	free(vertices);
	free(indices);
}
