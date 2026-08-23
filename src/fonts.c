#include "base_layer.h"
#include "fonts.h"
#include "base_layer.h"
#include "str.h"


static GlyphInfo	*atlasFindGlyph(const TextAtlas *atlas, u32 glyph_index)
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

// This function doesnt actually draw any text, it queues the text to be drawn by the TEXT pipeline
void textDraw(String text, Font *font, f32 font_size, vec2 pos, vec4 color)
{
	static hb_buffer_t *buf = NULL;

	const TextAtlas *atlas = &font->atlas;
	const f32 px_per_em = font_size;
	const f32 hb_scale = font_size / (f32)font->units_per_EM;


	if (!buf)
		buf = hb_buffer_create();

	hb_buffer_add_utf8(buf, (const char *)text.data, text.count, 0, -1);
	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buf, hb_language_from_string("en", -1));
	hb_shape(font->hb_font, buf, NULL, 0);

	u32 glyph_count;
	hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
	hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

	f32 cursor_x = pos[0];
	f32 cursor_y = pos[1];

	static int check = 0;


	for (u32 i = 0; i < glyph_count; i++) {
		// HarfBuzz's post-shaping codepoint is the font glyph index.
		const u32 glyph_index = glyph_info[i].codepoint;
		const GlyphInfo *g = atlasFindGlyph(atlas, glyph_index);

		if (check == 0) {
			printf("SHAPED[%u]: glyph_index=%u\n", i, glyph_index);
		}

		f32 x_offset = glyph_pos[i].x_offset * hb_scale;
		f32 y_offset = glyph_pos[i].y_offset * hb_scale;

		if (g) {
			const f32 glyph_width = g->plane_max[0] - g->plane_min[0];
			const f32 glyph_height = g->plane_max[1] - g->plane_min[1];

			// Glyphs such as space have no ink/geometry.
			if (glyph_width > 0 && glyph_height > 0) {
				if (font->glyph_count >= MAX_GLYPH_INSTANCES) {
					engine_error(LOG_FILE, "Glyph count exceeds max glyphs");
					break;
				}

				GlyphRenderInstance *inst = &font->render_instances[font->glyph_count++];

				const f32 left = g->plane_min[0] * px_per_em;
				const f32 top = g->plane_min[1] * px_per_em;
				const f32 right = g->plane_max[0] * px_per_em;
				const f32 bottom = g->plane_max[1] * px_per_em;

				inst->pos[0] = roundf(cursor_x + left);
				inst->pos[1] = roundf(cursor_y + top);

				inst->size[0] = right - left;
				inst->size[1] = bottom - top;

				inst->uv_offset[0] = g->uv_min[0];
				inst->uv_offset[1] = g->uv_min[1];

				inst->uv_size[0] = g->uv_max[0] - g->uv_min[0];
				inst->uv_size[1] = g->uv_max[1] - g->uv_min[1];

				glm_vec4_copy(color, inst->color);
			}
		}

		cursor_x += glyph_pos[i].x_advance * hb_scale;

		cursor_y += glyph_pos[i].y_advance * hb_scale;

	}
	check++;

	hb_buffer_reset(buf);
}

void	uploadAtlasToGpu(GraphicsContext *ctx, TextAtlas *atlas, u8 *atlas_data)
{

	VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
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

	stagingBufferUpload(ctx, atlas->width, atlas->height, atlas->width * atlas->height * CHANNELS, atlas_data, &atlas->gpu_image);

	VkImageViewCreateInfo	image_view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.image = atlas->gpu_image.image,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
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
		.maxLod = 0,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
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

static void	initFont(Font *font, FontHeader header, FT_Library ft_lib)
{
	FT_Face	face;

	FT_New_Face(ft_lib, header.path, 0, &face);

	hb_font_t	*hb_font = hb_ft_font_create(face, NULL);
	hb_font_set_scale(hb_font, face->units_per_EM, face->units_per_EM);
	font->face = face;
	font->units_per_EM = face->units_per_EM;
	font->hb_font = hb_font;
	font->atlas.width = header.atlas_width;
	font->atlas.height = header.atlas_height;
	font->atlas.glyph_count = header.glyph_count;
}

void	debug_dump_atlas(u8 *pixels, u32 width, u32 height)
{
	// Dump as a raw RGBA image so we can inspect the MSDF channels directly.
	// Use ImageMagick or a script to convert: magick -size WxH -depth 8 rgba:atlas_debug.raw atlas_debug.png
	FILE *dbg = fopen("/tmp/atlas_debug.raw", "wb");
	fwrite(pixels, 1, (size_t)width * height * CHANNELS, dbg);
	fclose(dbg);
	printf("Dumped raw RGBA atlas: %dx%d -> /tmp/atlas_debug.raw\n", width, height);
}

void	initFonts(GraphicsContext *ctx, LoadedFonts *loaded_fonts, Allocator *allocator, Allocator *frame_allocator)
{
	ArenaAllocator	frame_arena = frame_allocator->arena;
	String	font_file_path = STRING_LIT("data/font_file.bin");

	FT_Library	ft_lib;
	FT_Init_FreeType(&ft_lib);

	String	file = readFile(font_file_path);
	StringView	fileview = file;

	FileHeader	file_header;
	strReadSize(&fileview, &file_header, sizeof(FileHeader));

	if (file_header.magic != FATL_MAGIC || file_header.version != FATL_VERSION) {
		engine_error(LOG_FILE, "Bad or incompatible atlas file: %S", font_file_path);
		exit(1);
	}
	loaded_fonts->font_count = file_header.font_count;
	loaded_fonts->fonts = allocator->fp_allocation(allocator, sizeof(Font) * loaded_fonts->font_count, DEFAULT_ALIGN);

	for (u32 i = 0; i < loaded_fonts->font_count; i++) {
		Font		*font = &loaded_fonts->fonts[i];
		FontHeader	fheader;
		strReadSize(&fileview, &fheader, sizeof(FontHeader));
		initFont(font, fheader, ft_lib);

		font->atlas.glyphs = allocator->fp_allocation(allocator, sizeof(GlyphInfo) * fheader.glyph_count, DEFAULT_ALIGN);
		strReadSize(&fileview, font->atlas.glyphs, sizeof(GlyphInfo) * fheader.glyph_count);

		const u64	offset = frame_arena.offset;
		const u32	pixels_size = fheader.atlas_height * fheader.atlas_width * CHANNELS;
		u8	*pixels = frame_allocator->fp_allocation(frame_allocator, pixels_size, DEFAULT_ALIGN);
		strReadSize(&fileview, pixels, pixels_size);

		uploadAtlasToGpu(ctx, &font->atlas, pixels);
		debug_dump_atlas(pixels, fheader.atlas_width, fheader.atlas_height);
		font->allocator = frame_allocator;
		// TODO: Bad allocation
		font->render_instances = calloc(MAX_GLYPH_INSTANCES, sizeof(GlyphRenderInstance));
		// WARN: Messing with allocators like this is discouraged but oh well this is more efficent
		frame_arena.offset = offset;

		engine_log(LOG_FILE, "Loaded font: %s", fheader.path);
	}

	destroyFile(file);
}

TextRenderInfo	buildTextRenderInfo(Font *font)
{
	TextRenderInfo	info = {};

	info.glyph_count = font->glyph_count;
	info.render_instances = font->render_instances;
	info.upload_size = font->glyph_count * sizeof(GlyphRenderInstance);
	font->glyph_count = 0;
	return info;
}
