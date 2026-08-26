#pragma once

#include "harfbuzz/hb.h"
#include "freetype2/ft2build.h"
#include "freetype2/freetype/ftmodapi.h"
#include "harfbuzz/hb-ft.h"

#include "base_layer.h"
#include "graphics_layer.h"
#include "font_atlas_format.h"

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
	u16			units_per_EM;
	Allocator		*allocator;

	// Per frame stuff
	// NOTE: This 'render_instances' ptr doesnt change for now but later it might
	// when i add support for more stuff (2026-08-16)
	GlyphRenderInstance	*render_instances;
	u32			glyph_count;
}	Font;

typedef struct LoadedFonts
{
	Font	*fonts;
	u32	font_count;
}	LoadedFonts;

void	textDraw(String text, Font *font, f32 font_size, vec2 pos, vec4 color);
TextRenderInfo	buildTextRenderInfo(Font *font);
void	initFonts(GraphicsContext *ctx, LoadedFonts *loaded_fonts, Allocator *allocator, Allocator *frame_allocator);
