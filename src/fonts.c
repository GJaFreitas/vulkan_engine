#include "base_layer.h"
#include "fonts.h"

#define ATLAS_SIZE	2048

GlyphInfo	*atlasFindGlyph(TextAtlas *atlas, u32 glyph_index)
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

void	createTextAtlas(GraphicsContext *ctx, Font *font, String charset)
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

	VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = atlas->gpu_image.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8_UNORM,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	vkCreateImageView(ctx->device, &view_info, NULL, &atlas->gpu_image.view);

	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.anisotropyEnable = VK_FALSE
	};
	vkCreateSampler(ctx->device, &sampler_info, NULL, &atlas->sampler);

	const u32	glyph_count = charset.count;

	// TODO: Bad allocation
	atlas->glyphs = malloc(glyph_count * sizeof(GlyphInstance));
	atlas->glyph_count = glyph_count;

	// Staging buffer
	// TODO: Bad allocation
	u8	*atlas_data = malloc(atlas->width * atlas->height);

	for (u32 i = 0; i < glyph_count; i++) {
		const u32	codepoint = charset.data[i];
		const u32	glyph_index = FT_Get_Char_Index(font->face, codepoint);
		renderGlyphToAtlas(font, glyph_index, atlas_data, &atlas->glyphs[i]);
	}

	uploadAtlasToGpu(ctx, atlas, atlas_data);
	free(atlas_data);
}
