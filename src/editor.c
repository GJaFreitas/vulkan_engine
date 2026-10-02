#include "base_layer.h"
#include "ui.h"
#include "world.h"

void	_test_func(void) {
	print("Hi from button\n");
}

typedef struct
{
	f32 toolbar_h;
	f32 left_panel_w;
	f32 right_panel_w;
}	EditorContext;

static EditorContext	g_editor_ctx = {
	.toolbar_h = 40.0f,
	.left_panel_w = 250.0f,
	.right_panel_w = 300.0f,
};

void	drawEditorUI(GraphicsContext *ctx, Font *font)
{
	const f32 screen_w = (f32)ctx->swapchain_width;
	const f32 screen_h = (f32)ctx->swapchain_height;
	const f32 toolbar_h = g_editor_ctx.toolbar_h;
	const f32 left_panel_w = g_editor_ctx.left_panel_w;
	const f32 right_panel_w = g_editor_ctx.right_panel_w;

	// 2. Calculate and apply the 3D Viewport boundaries for the Grid/PBR passes
	ctx->viewport_offset[X] = (int)left_panel_w;
	ctx->viewport_offset[Y] = (int)toolbar_h;
	ctx->viewport_extent[X] = (uint)(screen_w - left_panel_w - right_panel_w);
	ctx->viewport_extent[Y] = (uint)(screen_h - toolbar_h);

	const ButtonSpec default_btn_spec = {
		.font          = font,
		.font_size     = 14,
		.padding       = 6.0f,
		.corner_radius = 2.0f,
		.color_idle    = {0.3f, 0.3f, 0.3f, 1.0f},
		.color_hover   = {0.5f, 0.5f, 0.5f, 1.0f},
		.color_active  = {0.2f, 0.2f, 0.2f, 1.0f},
		.text_color    = {1.0f, 1.0f, 1.0f, 1.0f}
	};

	TextSpec header_text = {
		.font       = font,
		.font_size  = 16,
		.text_color = {1.0f, 1.0f, 1.0f, 1.0f}
	};

	// --- TOP TOOLBAR ---
	imguiBeginPanel((PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_HORIZONTAL,
		.offset         = {0.0f, 0.0f}, // Dock to Top-Left
		.fixed_size     = {screen_w, toolbar_h},
		.inner_color    = {0.15f, 0.15f, 0.15f, 1.0f},
		.padding        = 8.0f,
		.spacing        = 10.0f
	});
	
	// We can wire this button directly to your game state!
	if (imguiButton(STRING_LIT("Play (Game Mode)"), default_btn_spec) == BUTTON_LEFT_CLICK) {
		print("Button clicked\n");
		gameStateSet(&g_game_state, NoClip, 0);
		g_engine_mode = ENGINE_MODE_GAME;
	}
	
	imguiEndPanel(); // Pop Toolbar

	// --- LEFT PANEL ---
	imguiBeginPanel((PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_VERTICAL,
		.offset         = {0.0f, toolbar_h}, // Start right below the toolbar
		.fixed_size     = {left_panel_w, screen_h - toolbar_h},
		.inner_color    = {0.2f, 0.2f, 0.2f, 1.0f},
		.padding        = 10.0f,
		.spacing        = 4.0f
	});
	
	imguiText(header_text, "LEFT PANEL!!");

	static bool	menu_open = false;

	MenuSpec	menu_spec = {
		.font = font,
		.font_size = 16,
		.item_font_size = 12,
		.padding = 2.0f,
		.corner_radius = 0,
		.color_idle    = {0.3f, 0.3f, 0.3f, 1.0f},
		.color_hover   = {0.5f, 0.5f, 0.5f, 1.0f},
		.color_active  = {0.2f, 0.2f, 0.2f, 1.0f},
		.text_color    = {1.0f, 1.0f, 1.0f, 1.0f},
		.spacing = 2.0f,
		.open = &menu_open,
	};

	MenuButton	menu_buttons[] = {
		{default_btn_spec, STRING_LIT("Button 1")},
		{default_btn_spec, STRING_LIT("Button 2")},
	};

	i32 pressed = imguiMenu(STRING_LIT("Menu name!!!11!"), menu_buttons, sizeofarray(menu_buttons), menu_spec);
	switch (pressed) {
		case 0: _test_func(); break;
		case 1: _test_func(); break;

		default: break;
	}

	imguiEndPanel(); // Pop Left Panel

	// --- RIGHT PANEL ---
	imguiBeginPanel((PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_VERTICAL,
		.offset         = {screen_w - right_panel_w, toolbar_h}, // Dock to Right edge
		.fixed_size     = {right_panel_w, screen_h - toolbar_h},
		.inner_color    = {0.2f, 0.2f, 0.2f, 1.0f},
		.padding        = 10.0f,
		.spacing        = 15.0f
	});

	imguiText(header_text, "RIGHT PANEL !11!");
	
	imguiEndPanel(); // Pop Component Block
	imguiEndPanel(); // Pop Right Panel
}
