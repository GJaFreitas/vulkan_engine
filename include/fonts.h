#pragma once

#include "harfbuzz/hb.h"
#include "freetype2/ft2build.h"
#include "harfbuzz/hb-ft.h"

#include "base_layer.h"
#include "graphics_layer.h"

typedef struct GlyphInfo
{
	u32		index;
	vec2		uv_min;
	vec2		uv_max;
	vec2		size;		// Glyph size in pixels
	vec2		bearing;	// Bearing offset
	float		advance;	// Advance width
}	GlyphInfo;

#define MAX_GLYPH_INSTANCES 1024
typedef struct GlyphRenderInstance
{
	vec2	pos;
	vec2	size;
	vec2	uv_offset;
	vec2	uv_size;
	vec4	color;
}	GlyphRenderInstance;

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
	FT_Face			face;
	hb_font_t		*hb_font;
	TextAtlas		atlas;
	float			font_size;
	Allocator		*allocator;

	// Per frame stuff
	// NOTE: This 'render_instances' ptr doesnt change for now but later it might
	// when i add support for more stuff (2026-08-16)
	GlyphRenderInstance	*render_instances;
	u32			glyph_count;
}	Font;

void	createTextAtlas(GraphicsContext *ctx, Font *font, String charset, Allocator *allocator);
void	textDraw(String text, Font *font, vec2 pos, vec4 color);
void	textInstanceBuild(TextAtlas *atlas, Font font, const String text, f32 x, f32 y, vec4 color, GlyphRenderInstance *out, u32 *out_count, Allocator *allocator);
TextRenderInfo	buildTextInfo(Font *font);
