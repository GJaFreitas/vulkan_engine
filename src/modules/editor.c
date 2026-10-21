#include "base_layer.h"
#include "ui.h"
#include "world.h"

void	_test_func(void) {
	print("Hi from button\n");
}

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
	.color_idle	= {0.3f, 0.3f, 0.3f, 1.0f}, \
	.color_hover	= {0.5f, 0.5f, 0.5f, 1.0f}, \
	.color_active	= {0.2f, 0.2f, 0.2f, 1.0f}, \
	.text_color	= {1.0f, 1.0f, 1.0f, 1.0f}, \
	.max_height	= 2, \
	__VA_ARGS__ \
}

#define UI_MENU(ctx, pool, name, spec, btn_spec, ...) \
	imguiMenu(ctx, pool, name, \
		(String[]){__VA_ARGS__}, \
		sizeof((String[]){__VA_ARGS__}) / sizeof(String), \
		btn_spec, \
		spec)

void	drawEditorUI(EditorContext *ectx, UiContext *uictx, ComponentPool *pool, GraphicsContext *ctx, Font *font)
{
	const f32 screen_w = (f32)ctx->swapchain_width;
	const f32 screen_h = (f32)ctx->swapchain_height;
	const f32 toolbar_h = ectx->toolbar_h;
	const f32 left_panel_w = ectx->left_panel_w;
	const f32 right_panel_w = ectx->right_panel_w;

	// 2. Calculate and apply the 3D Viewport boundaries for the Grid/PBR passes
	ctx->viewport_offset[X] = (int)left_panel_w;
	ctx->viewport_offset[Y] = (int)toolbar_h;
	ctx->viewport_extent[X] = (uint)(screen_w - left_panel_w - right_panel_w);
	ctx->viewport_extent[Y] = (uint)(screen_h - toolbar_h);

	// --- TOP TOOLBAR ---
	imguiBeginPanel(uictx, pool, (PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_HORIZONTAL,
		.offset         = {0.0f, 0.0f}, // Dock to Top-Left
		.fixed_size     = {screen_w, toolbar_h},
		.inner_color    = {0.15f, 0.15f, 0.15f, 1.0f},
		.padding        = 8.0f,
		.spacing        = 10.0f
	});
	
	if (imguiButton(uictx, pool, STRING_LIT("Play (Game Mode)"), UI_BTN_SPEC()) == BUTTON_LEFT_CLICK) {
		print("Button clicked\n");
		gameStateSet(&g_game_state, NoClip, 0);
		g_engine_mode = ENGINE_MODE_GAME;
	}
	
	imguiEndPanel(uictx, pool); // Pop Toolbar

	// --- LEFT PANEL ---
	imguiBeginPanel(uictx, pool, UI_PANEL_SPEC(
		.offset         = {0.0f, toolbar_h}, // Start right below the toolbar
		.fixed_size     = {left_panel_w, screen_h - toolbar_h},
	));
	
	imguiText(uictx, pool, UI_TEXT_SPEC(), "LEFT PANEL!!");


	switch (
		UI_MENU(uictx, pool, STRING_LIT("Menu name"), UI_MENU_SPEC(), UI_BTN_SPEC(),
			STRING_LIT("Button 0"),
			STRING_LIT("Button 1"),
			STRING_LIT("Button 2"),
		)
	)
	{
		// Nothing was pressed
		case -1: break;

		// Button clicks:
		case 0: _test_func(); break;
		case 1: _test_func(); break;
		case 2: _test_func(); break;

		default: break;
	}

	imguiEndPanel(uictx, pool); // Pop Left Panel

	// --- RIGHT PANEL ---
	imguiBeginPanel(uictx, pool, (PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_VERTICAL,
		.offset         = {screen_w - right_panel_w, toolbar_h}, // Dock to Right edge
		.fixed_size     = {right_panel_w, screen_h - toolbar_h},
		.inner_color    = {0.2f, 0.2f, 0.2f, 1.0f},
		.padding        = 10.0f,
		.spacing        = 15.0f
	});

	imguiText(uictx, pool, UI_TEXT_SPEC(), "RIGHT PANEL !11!");
	
	imguiEndPanel(uictx, pool); // Pop Component Block
	imguiEndPanel(uictx, pool); // Pop Right Panel
}
