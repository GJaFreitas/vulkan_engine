#pragma once

#include "base_layer.h"
#include "fonts.h"
#include "graphics_layer.h"
#include <stdarg.h>
#include <stdio.h>
#include "stb_sprintf.h"

#define RMGUI_MAX_COMPONENTS		8192
#define IMGUI_START_INDEX		8192
#define MAX_COMPONENTS			16384

enum ComponentShapeType
{
	GLYPH,
	RECTANGLE,
	ELIPSE,
};

enum ComponentShapeFillType
{
	FILLED,
	OUTLINE,
	TRANSPARENT,
};

typedef u8	UiSizeMode;
enum {
	UI_SIZE_FIXED,
	UI_SIZE_AUTO,
};


typedef u8	UiPrimitiveType;
enum {
	UI_PRIMITIVE_TEXT_GLYPH,
	UI_PRIMITIVE_RECTANGLE,
	UI_PRIMITIVE_CIRCLE,
};

typedef u8	UiLayoutType;
enum {
	UI_LAYOUT_ABSOLUTE,		// Children use their own anchors/offsets
	UI_LAYOUT_VERTICAL,		// Parent stacks children top-to-bottom
	UI_LAYOUT_HORIZONTAL,		// Parent stacks children left-to-right
};

typedef u8	UiAnchor;
enum {
	UI_ANCHOR_TOP_LEFT,
	UI_ANCHOR_TOP_CENTER,
	UI_ANCHOR_TOP_RIGHT,
	UI_ANCHOR_CENTER_LEFT,
	UI_ANCHOR_CENTER,
	UI_ANCHOR_CENTER_RIGHT,
	UI_ANCHOR_BOTTOM_LEFT,
	UI_ANCHOR_BOTTOM_CENTER,
	UI_ANCHOR_BOTTOM_RIGHT,
};

#define UI_NULL_INDEX	0

typedef struct UiShapedGlyph
{
	u32	atlas_index;
	f32	x_advance;
	f32	y_advance;
}	UiShapedGlyph;

// Add these to your UiComponent struct:
// UiShapedGlyph *shaped_glyphs;
// u32 shaped_glyph_count;
// bool is_text_dirty;

// TODO: Pack this struct... a lot
typedef struct UiComponent
{
	// --- TOPOLOGY ---
	u16 parent, first_child, last_child, next_sibling, prev_sibling;

	// --- LAYOUT DEFINITION ---
	UiPrimitiveType	primitive_type;
	UiLayoutType	layout_type;	// How I arrange my children
	UiAnchor	anchor;		// How I attach to my parent (if absolute)
	UiSizeMode	size_mode[2];	// X and Y size policies

	vec2		offset;		// My manual X/Y nudge
	vec2		fixed_size;	// My requested width/height

	// --- COMPUTED RESULT ---
	vec2		final_screen_pos;  // Calculated during the layout pass
	vec2		final_screen_size;

	// --- TEXT ---
	Font		*font;
	String		text;
	u8		font_size;
	// TODO: Add support for this
	vec4		text_color;
	UiShapedGlyph	*shaped_glyphs;
	u32		shaped_glyph_count;
	bool		is_text_dirty;

	// --- SHAPE ---
	vec4		outer_color;
	vec4		inner_color;
	f32		border_width;
	f32		corner_radius;

} UiComponent;

// For the RMGUI
typedef struct UiState
{
	u16	rmgui_root;
	u16	imgui_root;
}	UiState;

typedef struct PanelSpec
{
	// --- LAYOUT DEFINITION ---
	UiPrimitiveType	primitive_type;	// Type of panel (Rectangle/Elipse)
	UiLayoutType	layout_type;	// How I arrange my children
	UiAnchor	anchor;		// How I attach to my parent (if absolute)
	UiSizeMode	size_mode[2];	// X and Y size policies

	vec2		offset;		// My manual X/Y nudge
	vec2		fixed_size;	// My requested width/height (UI_SIZE_FIXED)

	vec4		outer_color;	// Color outside inner space (Border)
	vec4		inner_color;	// Inner color
	f32		border_width;	// Size of border
	f32		corner_radius;	// Rounded corners
}	PanelSpec;

void	initUi(UiState *ui, u32 screen_w, u32 screen_h, Allocator *perm);


void	imguiBeginPanel(u16 parent_node, PanelSpec spec);
bool	imguiButton(StringView text);
void	imguiEndPanel(void);
u16	imguiAllocateComponent(void);
void	imguiResetFrame(void);
void	imguiText(Font *font, f32 font_size, Allocator *frame_arena, const char *fmt, ...);

u16	setShape(enum ComponentShapeType shape);
u16	setShapeFill(enum ComponentShapeFillType fill);

TextRenderInfo	uiBuildRenderData(u16 rm_root_node, Allocator *frame_arena);
void		uiCalculateLayout(f32 screen_w, f32 screen_h, Allocator *frame_arena);
