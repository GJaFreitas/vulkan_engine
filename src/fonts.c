#include "base_layer.h"
#include "fonts.h"

#define ATLAS_SIZE	2048

GlyphInfo	*atlasFindGlyph(const TextAtlas *atlas, u32 glyph_index)
{
	// TODO: Test this to see if hashing is more efficent
	// »speed
	for (u32 i = 0; i < atlas->glyph_count; i++) {
		if (atlas->glyphs[i].index == glyph_index) {
			return (&atlas->glyphs[i]);
		}
	}
	return NULL;
}

// Called after textInstanceBuild() 
void	textInstanceBufferUpload(BufferObject *buffer, const GlyphInfo *instances, u32 count)
{
	const VkDeviceSize	needed = count * sizeof(GlyphInfo);
	if (needed > buffer->capacity) {
		engine_debug(LOG_FILE, "textInstanceBufferUpload called and needed size exceeds buffer capacity, count: %u", count);
	}
	memcpy(buffer->mapped, instances, needed);
}

// This function doesnt actually draw any text, it queues the text to be drawn by the TEXT pipeline
void	textDraw(String text, Font *font, vec2 pos, vec4 color)
{
	static hb_buffer_t	*buf = NULL;

	const TextAtlas	*atlas = &font->atlas;

	// On first run create hb buf
	if (!buf) { buf = hb_buffer_create(); }

	hb_buffer_add_utf8(buf, (const char *)text.data, text.count, 0, -1);
	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buf, hb_language_from_string("en", -1));
	hb_shape(font->hb_font, buf, NULL, 0);

	u32			glyph_count;
	hb_glyph_info_t		*glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
	hb_glyph_position_t	*glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

	f32	cursor_x = pos[0]; f32	cursor_y = pos[1];

	for (u32 i = 0; i < glyph_count; i++) {
		// NOTE: despite the field name, `codepoint` here is a glyph INDEX
		// post-shaping, not a Unicode codepoint. Must match how the atlas
		// was baked (by glyph index via FT_Get_Char_Index + FT_Load_Glyph).
		const u32	glyph_index = glyph_info[i].codepoint;
		const GlyphInfo	*g = atlasFindGlyph(atlas, glyph_index);
		float	x_offset = glyph_pos[i].x_offset / 64.0f;
		float	y_offset = glyph_pos[i].y_offset / 64.0f;

		if (g && g->size[0] > 0 && g->size[1] > 0) {
			if (font->glyph_count >= MAX_GLYPH_INSTANCES) { engine_error(LOG_FILE, "Glyph count exceeds max glyphs"); break; }
			GlyphRenderInstance	*inst = &font->render_instances[font->glyph_count++];

			inst->pos[0] = cursor_x + x_offset + g->bearing[0];
			inst->pos[1] = cursor_y - y_offset - g->bearing[1];
			inst->size[0] = g->size[0];
			inst->size[1] = g->size[1];

			inst->uv_offset[0] = g->uv_min[0];
			inst->uv_offset[1] = g->uv_min[1];
			inst->uv_size[0] = g->uv_max[0] - g->uv_min[0];
			inst->uv_size[1] = g->uv_max[1] - g->uv_min[1];

			glm_vec4_copy(color, inst->color);
		}
		// else: glyph wasn't in the prebaked charset (or has no ink, e.g. space) —
		// skipped, but the cursor still advances below.

		cursor_x += glyph_pos[i].x_advance / 64.0f;
		cursor_y += glyph_pos[i].y_advance / 64.0f;
	}

	hb_buffer_reset(buf);
}

void	uploadAtlasToGpu(GraphicsContext *ctx, TextAtlas *atlas, u8 *atlas_data)
{
	stagingBufferUpload(ctx, atlas->width, atlas->height, atlas_data, &atlas->gpu_image);

	VkImageViewCreateInfo	image_view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.image = atlas->gpu_image.image,
		.format = VK_FORMAT_R8_UNORM,
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
	if (vkCreateImageView(ctx->device, &image_view_info, NULL, &atlas->gpu_image.view) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create image view");
	}

	VkSamplerCreateInfo	sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		// How to sample when texture is magnified or minified
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.minLod = 0,
		.maxLod = 1,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		// What happens when axes go outside 0 - 1 range
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		// Need to check feature for this
		.anisotropyEnable = VK_FALSE
	};
	vkCreateSampler(ctx->device, &sampler_info, NULL, &atlas->sampler);

	VkDescriptorImageInfo	atlas_image_info = {
		.sampler = atlas->sampler,
		.imageView = atlas->gpu_image.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	VkWriteDescriptorSet	atlas_descriptor_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = ctx->atlas_descriptor_set,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &atlas_image_info,
	};
	vkUpdateDescriptorSets(ctx->device, 1, &atlas_descriptor_write, 0, NULL);

}

static void	renderGlyphToAtlas(Font *font, u32 glyph_index, u8 *atlas_data, GlyphInfo *atlas_glyph)
{
	FT_Face		face = font->face;
	TextAtlas	*atlas = &font->atlas;

	FT_Load_Glyph(face, glyph_index, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT);

	const FT_GlyphSlot	slot = face->glyph;
	const FT_Bitmap	*bitmap = &slot->bitmap;

	const u32	glyph_width = bitmap->width;
	const u32	glyph_height = bitmap->rows;

	if (atlas->next_x + glyph_width + 1 >= atlas->width) {
		atlas->next_x = 1;
		atlas->next_y += atlas->row_height + 1;
		atlas->row_height = 0;
	}

	if (glyph_height > atlas->row_height) {
		atlas->row_height = glyph_height;
	}

	const u32	x = atlas->next_x;
	const u32	y = atlas->next_y;

	for (u32 row = 0; row < glyph_height; row++) {
		for (u32 col = 0; col < glyph_width; col++) {
			u32 atlas_idx = (y + row) * atlas->width + (x + col);
			// WARN: If there is a problem with rendering chars it might
			// be because the pitch is negative, check here first
			u32 bitmap_idx = row * bitmap->pitch + col;
			atlas_data[atlas_idx] = bitmap->buffer[bitmap_idx];
		}
	}

	atlas_glyph->index = glyph_index;
	glm_vec2((vec2){(float)x / atlas->width, (float)y / atlas->height}, atlas_glyph->uv_min);
	glm_vec2((vec2){((float)x + glyph_width) / atlas->width, ((float)y + glyph_height) / atlas->height}, atlas_glyph->uv_max);
	glm_vec2((vec2){glyph_width, glyph_height}, atlas_glyph->size);
	glm_vec2((vec2){slot->bitmap_left, slot->bitmap_top}, atlas_glyph->bearing);
	atlas_glyph->advance = slot->advance.x >> 6;	// Convert to pixels

	atlas->next_x += glyph_width + 1;
}

void	createTextAtlas(GraphicsContext *ctx, Font *font, String charset, Allocator *frame_allocator)
{
	TextAtlas	*atlas = &font->atlas;

	atlas->width = ATLAS_SIZE;
	atlas->height = ATLAS_SIZE;
	atlas->next_x = 1;
	atlas->next_y = 1;
	atlas->row_height = 0;

	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8_UNORM,
		.extent = {atlas->width, atlas->height, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	if (wrapperVMAcreateImage(ctx->vma_allocator, &image_info, &atlas->gpu_image.image, &atlas->gpu_image.allocation) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Unable to create atlas image");
		exit(1);
	}

	const u32	glyph_count = charset.count;

	// TODO: Bad allocation
	atlas->glyphs = malloc(glyph_count * sizeof(GlyphInstance));
	atlas->glyph_count = glyph_count;

	// Staging buffer
	u8	*atlas_data = frame_allocator->fp_allocation(frame_allocator, atlas->width * atlas->height, DEFAULT_ALIGN);

	for (u32 i = 0; i < glyph_count; i++) {
		const u32	codepoint = charset.data[i];
		const u32	glyph_index = FT_Get_Char_Index(font->face, codepoint);
		renderGlyphToAtlas(font, glyph_index, atlas_data, &atlas->glyphs[i]);
	}

	uploadAtlasToGpu(ctx, atlas, atlas_data);
	font->allocator = frame_allocator;
	// TODO: Bad allocation
	font->render_instances = malloc(sizeof(GlyphRenderInstance) * MAX_GLYPH_INSTANCES);
}

TextRenderInfo	buildTextInfo(Font *font)
{
	TextRenderInfo	info = {};

	info.glyph_count = font->glyph_count;
	info.render_instances = font->render_instances;
	info.upload_size = font->glyph_count * sizeof(GlyphRenderInstance);
	font->glyph_count = 0;
	return info;
}
