#pragma once

#include "base_layer.h"
#include "fonts.h"
#include "graphics_layer.h"
#include <stdarg.h>
#include <stdio.h>
#include "stb_sprintf.h"
#include "console.h"

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

typedef struct UiShapedGlyph
{
	u32	atlas_index;
	f32	x_advance;
	f32	y_advance;
}	UiShapedGlyph;

#define TEXT_IS_DIRTY	(1 << 0)
#define TEXT_HAS_CURSOR	(1 << 1)

//»speed
// TODO: Pack this struct... a lot
typedef struct UiComponent
{
	// --- TOPOLOGY ---
	u16 parent, first_child, last_child, next_sibling, prev_sibling;

	// --- LAYOUT DEFINITION ---
	UiPrimitiveType	primitive_type;
	UiLayoutType	layout_type;	// How I arrange my children
	UiAnchor	anchor;		// How I attach to my parent (if absolute)

	vec2		offset;		// My manual X/Y nudge
	vec2		fixed_size;	// My requested width/height

	// --- COMPUTED RESULT ---
	vec2		final_screen_pos;  // Calculated during the layout pass
	vec2		final_screen_size;
	f32		clip_rect[4];

	// --- TEXT ---
	Font		*font;
	String		text;
	u8		font_size;
	vec4		text_color;
	UiShapedGlyph	*shaped_glyphs;
	u32		shaped_glyph_count;
	u32		text_flags;

	// --- SHAPE ---
	vec4		outer_color;
	vec4		inner_color;
	f32		border_width;
	f32		corner_radius;
	f32		padding;	// Padding applied to all things inside

} UiComponent;

typedef struct PanelSpec
{
	// --- LAYOUT DEFINITION ---
	UiPrimitiveType	primitive_type;	// Type of panel (Rectangle/Elipse)
	UiLayoutType	layout_type;	// How I arrange my children
	UiAnchor	anchor;		// How I attach to my parent (if absolute)

	vec2		offset;		// My manual X/Y nudge
	vec2		fixed_size;	// My requested width/height (UI_SIZE_FIXED)

	vec4		outer_color;	// Color outside inner space (Border)
	vec4		inner_color;	// Inner color
	f32		border_width;	// Size of border
	f32		corner_radius;	// Rounded corners
	f32		padding;	// The padding (left and right)
	f32		spacing;	// Space between children inside this panel
}	PanelSpec;

typedef struct TextSpec
{
	UiAnchor	anchor;			// How I attach to my parent (if absolute)
	Font		*font;			// The font the text will use
	u8		font_size;
	vec4		text_color;
	f32		nudge_x;
	f32		nudge_y;
	u32		flags;
}	TextSpec;

typedef struct ButtonSpec
{
	Font	*font;
	u8	font_size;
	f32	padding;
	f32	corner_radius;
	vec2	fixed_size;
	vec4	color_idle;
	vec4	color_hover;
	vec4	color_active;
	vec4	text_color;
}	ButtonSpec;

typedef enum ButtonState {
	BUTTON_NONE,
	BUTTON_LEFT_CLICK,
	BUTTON_RIGHT_CLICK,
}	ButtonState;

typedef void (*FP_ButtonFunction)(void);

typedef struct MenuSpec
{
	Font	*font;
	u8	font_size;
	u8	item_font_size;
	f32	padding;
	f32	corner_radius;
	vec4	outer_color;
	vec4	inner_color;
	vec4	color_idle;
	vec4	color_hover;
	vec4	color_active;
	vec4	text_color;
	f32	spacing;	// Space between children inside this panel
	f32	max_height;
	f32	width;
}	MenuSpec;

typedef struct ComponentPool
{
	u16	hint;
	u16	free_count;
	u16	imgui_count;

	bool	*used;
	u8	*mem;
}	ComponentPool;

#define IMGUI_STACK_MAX 64

typedef struct UiLayoutState {
	u16 component_idx;	// The panel we are currently inside
	f32 cursor_x;		// Current X pen position
	f32 cursor_y;		// Current Y pen position
	f32 start_x;		// Where the pen started (so we can calculate total width/height later)
	f32 start_y;
	f32 spacing;		// Space between children inside this panel
	UiLayoutType layout_type;
}	UiLayoutState;

typedef struct {
	i32	selected;
	u32	hash_id;
	f32	scroll_offset;
	f32	cached_width;
}	UiWidgetState;

typedef struct UiContext {
	u16 imgui_root;
	u16 rmgui_root;

	// Cursor Stack
	UiLayoutState layout_stack[IMGUI_STACK_MAX];
	u32           layout_stack_count;

	// Interaction State
	u32 hot_item_hash;
	u32 active_item_hash;

	// States
	UiWidgetState	widget_states[256];
	u32		widget_state_count;
}	UiContext;

typedef struct
{
	f32 toolbar_h;
	f32 left_panel_w;
	f32 right_panel_w;
}	EditorContext;

// For the RMGUI
typedef struct UiState
{
	u16		rmgui_root;
	u16		imgui_root;
	UiContext	ctx;
	EditorContext	editor_ctx;
	ComponentPool	pool;
}	UiState;

// The __VA_ARGS__ means that any value we pass will be used isntead

#define UI_PANEL_SPEC(...) (PanelSpec){ \
	.primitive_type	= UI_PRIMITIVE_RECTANGLE, \
	.layout_type	= UI_LAYOUT_VERTICAL, \
	.offset		= {0.0f, 0.0f}, \
	.fixed_size	= {0.0f, 0.0f}, \
	.inner_color	= {0.2f, 0.2f, 0.2f, 1.0f}, \
	.outer_color	= {0.0f, 0.0f, 0.0f, 0.0f}, \
	.padding	= 10.0f, \
	.spacing	= 4.0f, \
	.border_width	= 0.0f, \
	.corner_radius	= 0.0f, \
	__VA_ARGS__ \
}

#define UI_BTN_SPEC(...) (ButtonSpec){ \
	.font		= font, \
	.font_size	= 14.0f, \
	.padding	= 6.0f, \
	.corner_radius	= 2.0f, \
	.color_idle	= {0.3f, 0.3f, 0.3f, 1.0f}, \
	.color_hover	= {0.5f, 0.5f, 0.5f, 1.0f}, \
	.color_active	= {0.2f, 0.2f, 0.2f, 1.0f}, \
	.text_color	= {1.0f, 1.0f, 1.0f, 1.0f}, \
	.fixed_size	= {0.0f, 0.0f}, \
	__VA_ARGS__ \
}

#define UI_TEXT_SPEC(...) (TextSpec){ \
	.font		= font, \
	.font_size	= 16.0f, \
	.text_color	= {1.0f, 1.0f, 1.0f, 1.0f}, \
	.nudge_x	= 0.0f, \
	.nudge_y	= 0.0f, \
	.flags		= 0, \
	__VA_ARGS__ \
}

#define UI_MENU_SPEC(...) (MenuSpec){ \
	.font		= font, \
	.font_size	= 16.0f, \
	.item_font_size	= 12.0f, \
	.padding	= 2.0f, \
	.spacing	= 2.0f, \
	.corner_radius	= 0.0f, \
	.outer_color	= {0.0f, 0.0f, 0.0f, 0.0f}, \
	.inner_color	= {0.0f, 0.0f, 0.0f, 0.0f}, \
	.color_idle	= {0.3f, 0.3f, 0.3f, 1.0f}, \
	.color_hover	= {0.5f, 0.5f, 0.5f, 1.0f}, \
	.color_active	= {0.2f, 0.2f, 0.2f, 1.0f}, \
	.text_color	= {1.0f, 1.0f, 1.0f, 1.0f}, \
	.width		= 0.0f, \
	.max_height	= 1, \
	__VA_ARGS__ \
}

#define UI_MENU(ctx, pool, name, spec, btn_spec, ...) \
	imguiMenu(ctx, pool, name, \
		(String[]){__VA_ARGS__}, \
		sizeof((String[]){__VA_ARGS__}) / sizeof(String), \
		btn_spec, \
		spec)

void	initUi(UiState *ui, u32 screen_w, u32 screen_h, Allocator *perm);

void		imguiBeginPanel(UiContext *uictx, ComponentPool *pool, PanelSpec spec);
ButtonState	imguiButton(UiContext *uictx, ComponentPool *pool, StringView text_view, ButtonSpec spec);
void		imguiText(UiContext *uictx, ComponentPool *pool, TextSpec spec, const char *fmt, ...);
i32		imguiMenu(UiContext *uictx, ComponentPool *pool, StringView menu_name, String *btn_list, u32 btn_count, ButtonSpec btn_spec, MenuSpec spec);
void		imguiEndPanel(UiContext *uictx, ComponentPool *pool);
void		imguiResetFrame(UiContext *uictx, ComponentPool *pool);

UiRenderInfo	buildUiRenderInfo(ComponentPool *pool, u16 rm_root_node);

void	openConsole(UiContext *uictx, ComponentPool *pool, u32 screen_w, u32 screen_h, Font *font, u8 console_font_size);
void	showFps(UiContext *uictx, ComponentPool *pool, Font *font, double fps);
void	drawEditorUI(EditorContext *ectx, UiContext *uictx, ComponentPool *pool, GraphicsContext *ctx, Font *font);

UiRenderInfo	runUi(GraphicsContext *graphics_ctx, UiState *ui, Font *font, double fps);

String	consoleBackend(void);
