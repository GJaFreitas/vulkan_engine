#pragma once

#include "harfbuzz/hb.h"
#include "freetype2/ft2build.h"
#include "harfbuzz/hb-ft.h"

#include "base_layer.h"
#include "graphics_layer.h"

typedef struct GlyphInfo {
	u32		index;
	vec2		uv_min;
	vec2		uv_max;
	vec2		size;		// Glyph size in pixels
	vec2		bearing;	// Bearing offset
	float		advance;	// Advance width
}	GlyphInfo;

typedef struct TextAtlas
{
	ImageObject	gpu_image;
	VkSampler	sampler;

	u32		width;
	u32		height;
	u32		next_x;
	u32		next_y;
	u32		row_height;

	GlyphInfo	*glyphs;
	u32		glyph_count;

}	TextAtlas;

typedef struct Font
{
	FT_Face		face;
	hb_font_t	*hb_font;
	TextAtlas	atlas;
	float		font_size;
}	Font;

void	createTextAtlas(GraphicsContext *ctx, Font *font, String charset);
