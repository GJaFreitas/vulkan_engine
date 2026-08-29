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

typedef struct UiComponent
{
	// --- TOPOLOGY ---
	u16 parent, first_child, last_child, next_sibling, prev_sibling;

	// --- LAYOUT DEFINITION ---
	UiPrimitiveType	primitive_type;
	UiLayoutType	layout_type; // How I arrange my children
	UiAnchor	anchor;      // How I attach to my parent (if absolute)

	vec2		offset;     // My manual X/Y nudge
	vec2		fixed_size; // My requested width/height

	// --- COMPUTED RESULT ---
	vec2		final_screen_pos;  // Calculated during the layout pass
	vec2		final_screen_size;

	// --- LOGIC & LAYOUT ---
	Font		*font;
	String		text;
	u8		font_size;
	vec4		color;
	vec4		inner_color;
	u32		shape;
	f32		stroke_width;
	f32		corner_radius;
} UiComponent;

// For the RMGUI
typedef struct UiState
{
	u16	rmgui_root;
	u16	imgui_root;
}	UiState;

void	initUi(UiState *ui, u32 screen_w, u32 screen_h, Allocator *perm);


void	imguiBeginPanel(u16 parent_node, UiAnchor anchor, f32 x, f32 y, f32 w, f32 h);
bool	imguiButton(StringView text);
void	imguiEndPanel(void);
u16	imguiAllocateComponent(void);
void	imguiResetFrame(void);
void	imguiText(Font *font, f32 font_size, Allocator *frame_arena, const char *fmt, ...);

u16	setShape(enum ComponentShapeType shape);
u16	setShapeFill(enum ComponentShapeFillType fill);

TextRenderInfo	uiBuildRenderData(u16 rm_root_node, Allocator *frame_arena);
void		uiCalculateLayout(u16 node_idx, f32 parent_x, f32 parent_y, f32 parent_w, f32 parent_h);
