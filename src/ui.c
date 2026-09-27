#include "ui.h"
#include "world.h"

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
} UiLayoutState;

typedef struct UiContext {
	u16 imgui_root;
	u16 rmgui_root;

	// Cursor Stack
	UiLayoutState layout_stack[IMGUI_STACK_MAX];
	u32           layout_stack_count;

	// Interaction State
	u32 hot_item_hash;
	u32 active_item_hash;
} UiContext;

static UiContext	g_ui_ctx = {};

ComponentPool	pool = { .hint = 1, .free_count = MAX_COMPONENTS - 1 };

static inline UiComponent*	uiGet(u16 index) {
	return (UiComponent*)&pool.mem[index * sizeof(UiComponent)];
}

// --- SETUP HELPERS ---

static inline u32	hashString32(StringView str)
{
	// FNV offset basis
	u32 hash = 2166136261u;

	for (u64 i = 0; i < str.count; i++) {
		hash ^= str.data[i];
		// FNV prime
		hash *= 16777619u;
	}

	return hash;
}

void	uiSetSize(u16 node_idx, f32 width, f32 height) {
	UiComponent* node = uiGet(node_idx);
	node->fixed_size[X] = width;
	node->fixed_size[Y] = height;
}

void	uiSetAbsolute(u16 node_idx, UiAnchor anchor, f32 offset_x, f32 offset_y) {
	UiComponent* node = uiGet(node_idx);
	node->anchor = anchor;
	node->offset[X] = offset_x;
	node->offset[Y] = offset_y;
}

void	uiSetLayout(u16 node_idx, UiLayoutType type) {
	uiGet(node_idx)->layout_type = type;
}

typedef struct TextMeasurement
{
	UiShapedGlyph	*glyphs;
	u32		count;
	f32		width;
}	TextMeasurement;

TextMeasurement	uiShapeText(Font *font, u8 font_size, const char *text_str, int len)
{
	static hb_buffer_t	*buf = NULL;
	
	if (!buf) {
		buf = hb_buffer_create();
	} else {
		hb_buffer_clear_contents(buf);
	}

	hb_buffer_add_utf8(buf, text_str, len, 0, -1);
	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buf, hb_language_from_string("en", -1));
	hb_shape(font->hb_font, buf, NULL, 0);

	u32	g_count;
	hb_glyph_info_t	*g_info = hb_buffer_get_glyph_infos(buf, &g_count);
	hb_glyph_position_t	*g_pos = hb_buffer_get_glyph_positions(buf, &g_count);

	UiShapedGlyph	*shaped_glyphs = g_frame_arena.fp_allocation(&g_frame_arena, sizeof(UiShapedGlyph) * g_count, DEFAULT_ALIGN);

	const f32	hb_scale = font_size / (f32)font->units_per_EM;
	f32	text_width = 0.0f;

	for (u32 i = 0; i < g_count; i++) {
		shaped_glyphs[i].atlas_index = g_info[i].codepoint;
		shaped_glyphs[i].x_advance = g_pos[i].x_advance * hb_scale;
		shaped_glyphs[i].y_advance = g_pos[i].y_advance * hb_scale;
		
		text_width += shaped_glyphs[i].x_advance;
	}

	TextMeasurement	result;
	result.glyphs = shaped_glyphs;
	result.count = g_count;
	result.width = text_width;

	return result;
}

// --- Allocators ---

u16	imguiAllocateComponent(void)
{
	if (pool.imgui_count >= (MAX_COMPONENTS - IMGUI_START_INDEX)) {
		engine_error(LOG_FILE, "IMGUI node limit reached this frame!");
		exit(1);
	}

	u16 index = IMGUI_START_INDEX + pool.imgui_count;
	pool.imgui_count++;

	// Get the pointer and zero it out so topology/indices are clean
	UiComponent *node = (UiComponent *)&pool.mem[index * sizeof(UiComponent)];
	memset(node, 0, sizeof(UiComponent));

	return index;
}

void	imguiResetFrame(void)
{
	pool.imgui_count = 0; 
	g_ui_ctx.layout_stack_count = 0;

	// Sever the links so RMGUI forgets about the wiped IMGUI nodes!
	UiComponent *imgui_root = uiGet(g_ui_ctx.imgui_root);
	imgui_root->first_child = UI_NULL_INDEX;
	imgui_root->last_child  = UI_NULL_INDEX;
}

void	freeComponent(u16 index)
{
	// Prevent freeing the null index or out-of-bounds IMGUI nodes
	if (index == 0 || index >= RMGUI_MAX_COMPONENTS) {
		return;
	}

	if (!pool.used[index]) {
		return;
	}

	pool.free_count++;
	pool.hint = index;
	pool.used[index] = false;
}

u16	allocateComponent(void)
{
	if (pool.free_count == 0) {
		engine_error(LOG_FILE, "No free component slots in pool!");
		exit(1);
	}

	const u16 last_free = pool.hint;

	// Fast Path
	if (!pool.used[last_free]) {
		pool.used[last_free] = true;
		pool.free_count--;

		pool.hint = (last_free + 1) % RMGUI_MAX_COMPONENTS;
		if (pool.hint == 0) pool.hint = 1; // Skip the null index

		UiComponent *node = (UiComponent *)&pool.mem[last_free * sizeof(UiComponent)];
		memset(node, 0, sizeof(UiComponent));
		return last_free;
	}

	// Slow Path
	for (u32 i = 0; i < RMGUI_MAX_COMPONENTS; i++) {

		u16 index = (last_free + i) % RMGUI_MAX_COMPONENTS;

		if (pool.used[index] == false) {
			pool.used[index] = true;
			pool.free_count--;

			pool.hint = (index + 1) % RMGUI_MAX_COMPONENTS;
			if (pool.hint == 0) pool.hint = 1; // Skip the null index

			UiComponent *node = (UiComponent *)&pool.mem[index * sizeof(UiComponent)];
			memset(node, 0, sizeof(UiComponent));
			return index;
		}
	}

	// Unreachable as long as free_count is valid
	return 0; 
}

// --- IMGUI Code ---

void	uiAppendChild(u16 parent, u16 new_idx)
{
	UiComponent	*parent_node = uiGet(parent);
	UiComponent	*new_node = uiGet(new_idx);

	new_node->parent = parent;
	const u16	last_child = parent_node->last_child;
	if (last_child == UI_NULL_INDEX) {
		parent_node->first_child = new_idx;
		parent_node->last_child = new_idx;
		return ;
	}

	UiComponent	*last_child_node = uiGet(last_child);
	last_child_node->next_sibling = new_idx;
	parent_node->last_child = new_idx;

	new_node->prev_sibling = last_child;
}

void	imguiText(TextSpec spec, const char *fmt, ...)
{
	if (g_ui_ctx.layout_stack_count == 0) {
		engine_error(LOG_FILE, "imguiText called without an active panel!");
		return;
	}
	UiLayoutState	*layout = &g_ui_ctx.layout_stack[g_ui_ctx.layout_stack_count - 1];

	// 1. Format the string dynamically
	va_list	args;
	va_start(args, fmt);
	int	len = stbsp_vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	u8	*text_data = g_frame_arena.fp_allocation(&g_frame_arena, len + 1, DEFAULT_ALIGN);

	va_start(args, fmt);
	stbsp_vsnprintf((char *)text_data, len + 1, fmt, args);
	va_end(args);

	// 2. Immediate HarfBuzz Shaping
	TextMeasurement	measurement = uiShapeText(spec.font, spec.font_size, (const char *)text_data, len);

	// 3. Allocate and populate the UI Component
	u16	text_idx = imguiAllocateComponent();
	UiComponent	*text = uiGet(text_idx);
	
	text->primitive_type = UI_PRIMITIVE_TEXT_GLYPH;
	text->font = spec.font;
	text->font_size = spec.font_size;
	glm_vec4_copy(spec.text_color, text->text_color);
	text->text_flags = spec.flags;

	// Link the data
	text->text.data = text_data;
	text->text.count = len;
	text->shaped_glyphs = measurement.glyphs;
	text->shaped_glyph_count = measurement.count;

	// 4. Calculate Final Screen Position IMMEDIATELY using the layout cursor
	text->final_screen_pos[X] = layout->cursor_x + spec.nudge_x;
	text->final_screen_pos[Y] = layout->cursor_y + spec.nudge_y;
	text->final_screen_size[X] = measurement.width;
	text->final_screen_size[Y] = spec.font_size;

	// Inherit clipping rect from parent panel
	UiComponent	*parent_panel = uiGet(layout->component_idx);
	glm_vec4_copy(parent_panel->clip_rect, text->clip_rect);

	// 5. Advance the Layout Cursor
	if (layout->layout_type == UI_LAYOUT_VERTICAL) {
		layout->cursor_y += spec.font_size + layout->spacing;
	} else if (layout->layout_type == UI_LAYOUT_HORIZONTAL) {
		layout->cursor_x += measurement.width + layout->spacing;
	}
}

void imguiBeginPanel(PanelSpec spec)
{
	u16 panel_idx = imguiAllocateComponent();
	UiComponent *panel = uiGet(panel_idx);
	
	// Copy styling...
	panel->primitive_type = spec.primitive_type;
	glm_vec4_copy(spec.inner_color, panel->inner_color);
	panel->padding = spec.padding;
	// ... (copy other visual specs) ...

	// 1. Where does this panel start?
	f32 start_x = spec.offset[X];
	f32 start_y = spec.offset[Y];

	if (g_ui_ctx.layout_stack_count > 0) {
		// We are nested inside another panel! Start at its cursor.
		UiLayoutState *parent_layout = &g_ui_ctx.layout_stack[g_ui_ctx.layout_stack_count - 1];
		start_x += parent_layout->cursor_x;
		start_y += parent_layout->cursor_y;
	}

	// Set our absolute position immediately
	panel->final_screen_pos[X] = start_x;
	panel->final_screen_pos[Y] = start_y;
	
	// We DON'T know our size yet if we aren't fixed!
	panel->final_screen_size[X] = spec.fixed_size[X]; 
	panel->final_screen_size[Y] = spec.fixed_size[Y];

	// 2. Push our new layout state for our children to use
	UiLayoutState *layout = &g_ui_ctx.layout_stack[g_ui_ctx.layout_stack_count++];
	layout->component_idx = panel_idx;
	layout->layout_type   = spec.layout_type;
	layout->start_x       = start_x + spec.padding; // Children start inside the padding
	layout->start_y       = start_y + spec.padding;
	layout->cursor_x      = layout->start_x;
	layout->cursor_y      = layout->start_y;
	layout->spacing       = spec.spacing;
}

void imguiEndPanel(void)
{
	if (g_ui_ctx.layout_stack_count == 0) return;

	// 1. Pop the layout stack
	UiLayoutState *layout = &g_ui_ctx.layout_stack[--g_ui_ctx.layout_stack_count];
	UiComponent *panel = uiGet(layout->component_idx);

	// If the user didn't specify a fixed size, we calculate how far the cursor moved.
	if (panel->final_screen_size[X] == 0.0f) {
		panel->final_screen_size[X] = (layout->cursor_x - panel->final_screen_pos[X]) + panel->padding;
	}
	if (panel->final_screen_size[Y] == 0.0f) {
		panel->final_screen_size[Y] = (layout->cursor_y - panel->final_screen_pos[Y]) + panel->padding;
	}

	// 3. If we are nested, we must advance our PARENT'S cursor by our total size!
	if (g_ui_ctx.layout_stack_count > 0) {
		UiLayoutState *parent_layout = &g_ui_ctx.layout_stack[g_ui_ctx.layout_stack_count - 1];
		
		if (parent_layout->layout_type == UI_LAYOUT_VERTICAL) {
			parent_layout->cursor_y += panel->final_screen_size[Y] + parent_layout->spacing; // 4px margin
		} else {
			parent_layout->cursor_x += panel->final_screen_size[X] + parent_layout->spacing;
		}
	}
}

ButtonState	imguiButton(StringView text_view, ButtonSpec spec)
{
	if (g_ui_ctx.layout_stack_count == 0) {
		engine_error(LOG_FILE, "imguiButton called without an active panel!");
		return false;
	}
	UiLayoutState	*layout = &g_ui_ctx.layout_stack[g_ui_ctx.layout_stack_count - 1];

	const char	*text_str = (const char *)text_view.data; 
	const int	len = text_view.count; 

	// 1. Immediate HarfBuzz Shaping
	TextMeasurement	measurement = uiShapeText(spec.font, spec.font_size, text_str, len);

	// 2. Calculate Final Screen Bounds
	const f32	btn_width = measurement.width + (spec.padding * 2.0f);
	const f32	btn_height = spec.font_size + (spec.padding * 2.0f);

	f32	pos_x = layout->cursor_x;
	f32	pos_y = layout->cursor_y;

	// 3. Interaction Logic
	u32	id = hashString32(text_view);
	ButtonState	btn_state = NONE;
	
	f32	mx = g_input_state.mouse_pos[X];
	f32	my = g_input_state.mouse_pos[Y];
	bool	mouse_left_down = mouseHeld(SDL_BUTTON_LEFT);
	bool	mouse_left_released = mouseReleased(SDL_BUTTON_LEFT);
	bool	mouse_right_down = mouseHeld(SDL_BUTTON_RIGHT);
	bool	mouse_right_released = mouseReleased(SDL_BUTTON_RIGHT);

	// Is mouse inside button?
	if (mx >= pos_x && mx <= pos_x + btn_width && 
	    my >= pos_y && my <= pos_y + btn_height) 
	{
		// Yes it is
		g_ui_ctx.hot_item_hash = id;
		

		// Is the button clicked?
		if ((mouse_left_down || mouse_right_down) && g_ui_ctx.active_item_hash == 0) {
			// Yes it is
			g_ui_ctx.active_item_hash = id;
		}
	}

	// Was this button originally clicked?
	if (g_ui_ctx.active_item_hash == id) {
		// Yes, is the mouse button still on the button?
		if (g_ui_ctx.hot_item_hash == id) {
			// Yes it is, was it the left or right button?
			if (mouse_left_released) {
				btn_state = LEFT_CLICK;
			} else if (mouse_right_released) {
				btn_state = RIGHT_CLICK;
			}

			// Button was clicked
			g_ui_ctx.active_item_hash = 0;
		}
	}

	// 4. Create Background Component
	u16	btn_idx = imguiAllocateComponent();
	UiComponent	*btn = uiGet(btn_idx);
	
	btn->primitive_type = UI_PRIMITIVE_RECTANGLE;
	btn->final_screen_pos[X] = pos_x;
	btn->final_screen_pos[Y] = pos_y;
	btn->final_screen_size[X] = btn_width;
	btn->final_screen_size[Y] = btn_height;
	btn->corner_radius = spec.corner_radius; 
	
	UiComponent	*parent_panel = uiGet(layout->component_idx);
	glm_vec4_copy(parent_panel->clip_rect, btn->clip_rect);

	if (g_ui_ctx.active_item_hash == id) {
		glm_vec4_copy(spec.color_active, btn->inner_color); 
	} else if (g_ui_ctx.hot_item_hash == id) {
		glm_vec4_copy(spec.color_hover, btn->inner_color);
	} else {
		glm_vec4_copy(spec.color_idle, btn->inner_color);
	}

	// 5. Create Text Component
	u16	text_idx = imguiAllocateComponent();
	UiComponent	*text = uiGet(text_idx);
	
	text->primitive_type = UI_PRIMITIVE_TEXT_GLYPH;
	text->font = spec.font;
	text->font_size = spec.font_size;
	glm_vec4_copy(spec.text_color, text->text_color); 

	u8	*text_data = g_frame_arena.fp_allocation(&g_frame_arena, len + 1, DEFAULT_ALIGN);
	memcpy(text_data, text_str, len);
	text_data[len] = '\0';
	
	text->text.data = text_data;
	text->text.count = len;
	text->shaped_glyphs = measurement.glyphs;
	text->shaped_glyph_count = measurement.count;
	
	text->final_screen_pos[X] = pos_x + spec.padding;
	text->final_screen_pos[Y] = pos_y + spec.padding;
	text->final_screen_size[X] = measurement.width;
	text->final_screen_size[Y] = spec.font_size;
	glm_vec4_copy(parent_panel->clip_rect, text->clip_rect);

	// 6. Advance the Layout Cursor
	if (layout->layout_type == UI_LAYOUT_VERTICAL) {
		layout->cursor_y += btn_height + layout->spacing;
	} else if (layout->layout_type == UI_LAYOUT_HORIZONTAL) {
		layout->cursor_x += btn_width + layout->spacing;
	}

	return btn_state;
}

// --- INIT CODE ---

void	initUi(UiState *ui, u32 screen_w, u32 screen_h, Allocator *perm)
{
	// Pool initialization
	pool.mem = perm->fp_allocation(perm, sizeof(UiComponent) * RMGUI_MAX_COMPONENTS, DEFAULT_ALIGN);
	pool.used = perm->fp_allocation(perm, sizeof(bool) * RMGUI_MAX_COMPONENTS, DEFAULT_ALIGN);
	memset(pool.used, 0, sizeof(bool) * RMGUI_MAX_COMPONENTS);

	ui->rmgui_root = allocateComponent();
	UiComponent	*rmgui_root = uiGet(ui->rmgui_root);
	*rmgui_root = (UiComponent){
		.parent = UI_NULL_INDEX,
		.first_child = UI_NULL_INDEX,
		.last_child = UI_NULL_INDEX,
		.next_sibling = UI_NULL_INDEX,
		.prev_sibling = UI_NULL_INDEX,
		.layout_type = UI_LAYOUT_ABSOLUTE,
		.anchor = UI_ANCHOR_TOP_LEFT,
		.final_screen_size = {screen_w, screen_h},
	};

	ui->imgui_root = allocateComponent();
	UiComponent	*imgui_root = uiGet(ui->imgui_root);
	*imgui_root = (UiComponent){
		.parent = UI_NULL_INDEX,
		.first_child = UI_NULL_INDEX,
		.last_child = UI_NULL_INDEX,
		.next_sibling = UI_NULL_INDEX,
		.prev_sibling = UI_NULL_INDEX,
		.layout_type = UI_LAYOUT_ABSOLUTE,
		.anchor = UI_ANCHOR_TOP_LEFT,
		.final_screen_size = {screen_w, screen_h},
	};

	g_ui_ctx.rmgui_root = ui->rmgui_root;
	g_ui_ctx.imgui_root = ui->imgui_root;
	uiAppendChild(g_ui_ctx.rmgui_root, g_ui_ctx.imgui_root);
}

// --- Vulkan ---

static void pushShapeInstance(UiComponent *node, UiRenderInstance *instances, u32 *count)
{
	u32 idx = *count;

	instances[idx].pos[X] = node->final_screen_pos[X];
	instances[idx].pos[Y] = node->final_screen_pos[Y];
	instances[idx].size[X] = node->final_screen_size[X];
	instances[idx].size[Y] = node->final_screen_size[Y];

	instances[idx].uv_offset[X] = 0.0f;
	instances[idx].uv_offset[Y] = 0.0f;
	instances[idx].uv_size[X] = 1.0f;
	instances[idx].uv_size[Y] = 1.0f;

	instances[idx].primitive_type = node->primitive_type; 
	
	glm_vec4_copy(node->outer_color, instances[idx].color);
	glm_vec4_copy(node->inner_color, instances[idx].inner_color);
	
	instances[idx].stroke_width = node->border_width;
	instances[idx].corner_radius = node->corner_radius;

	(*count)++;
}

// -1 cursor pos for text that wont render a cursor
static void pushTextInstances(UiComponent *node, UiRenderInstance *instances, u32 *count, i32 cursor_pos)
{
	const TextAtlas *atlas = &node->font->atlas;
	const f32 px_per_em = node->font_size;
	
	f32 cursor_x = node->final_screen_pos[X];
	// Font size so that the baseline of the text aligns with the box its in
	f32 cursor_y = node->final_screen_pos[Y] + node->font_size;

	// The console string starts with '> ' as such we need to advance the cursor
	if (cursor_pos != -1)
		cursor_pos += 2;
	f32	text_cursor_x = -1;

	// Loop over our cached glyphs instead of calling HarfBuzz!
	for (u32 i = 0; i < node->shaped_glyph_count; i++) {
		UiShapedGlyph *cached = &node->shaped_glyphs[i];
		const GlyphInfo *g = atlasFindGlyph(atlas, cached->atlas_index);

		if (cursor_pos != -1 && (i32)i == cursor_pos) {
			text_cursor_x = cursor_x;
		}

		if (g) {
			const f32 glyph_width = g->plane_max[X] - g->plane_min[X];
			const f32 glyph_height = g->plane_max[Y] - g->plane_min[Y];

			if (glyph_width > 0 && glyph_height > 0) {
				u32 idx = *count;
				UiRenderInstance *inst = &instances[idx];

				glm_vec4_copy(node->clip_rect, inst->clip_rect);
				inst->primitive_type = UI_PRIMITIVE_TEXT_GLYPH;
				inst->stroke_width   = 0.0f;
				inst->corner_radius  = 0.0f;

				const f32 left   = g->plane_min[X] * px_per_em;
				const f32 top    = g->plane_min[Y] * px_per_em;
				const f32 right  = g->plane_max[X] * px_per_em;
				const f32 bottom = g->plane_max[Y] * px_per_em;

				inst->pos[X] = cursor_x + left;
				inst->pos[Y] = cursor_y + top;
				inst->size[X] = right - left;
				inst->size[Y] = bottom - top;

				inst->uv_offset[X] = g->uv_min[X];
				inst->uv_offset[Y] = g->uv_min[Y];
				inst->uv_size[X] = g->uv_max[X] - g->uv_min[X];
				inst->uv_size[Y] = g->uv_max[Y] - g->uv_min[Y];

				glm_vec4_copy(node->text_color, inst->color);
				(*count)++;
			}
		}

		// Advance the cursor using the cached HarfBuzz metrics
		cursor_x += cached->x_advance;
		cursor_y += cached->y_advance;
	}
	if (cursor_pos == -1) return ;

	// The cursor is at end of string
	if (text_cursor_x == -1)
		text_cursor_x = cursor_x;

	const u32 idx = *count;
	UiRenderInstance *inst = &instances[idx];
	memset(inst, 0, sizeof(UiRenderInstance));

	glm_vec4_copy(node->clip_rect, inst->clip_rect);
	inst->primitive_type = UI_PRIMITIVE_RECTANGLE; // Render as a solid shape
	inst->stroke_width   = 2.0f;
	inst->corner_radius  = 0.0f;

	// Draw a line 2 pixels thick, resting exactly on the text baseline
	inst->pos[X] = text_cursor_x;
	inst->pos[Y] = node->final_screen_pos[Y] + node->font_size + 2.0f; 

	// Width of the cursor (half an EM width looks standard for console text)
	inst->size[X] = node->font_size * 0.5f; 
	inst->size[Y] = 2.0f; 

	glm_vec4_copy(node->text_color, inst->color);
	(*count)++;
}

static void traverseForRender(u16 node_idx, UiRenderInstance *instances, u32 *count)
{
	UiComponent *node = uiGet(node_idx);

	// A node is visible if it's text, OR if it has physical dimensions. And it must have alpha > 0.
	const bool has_size =
			(node->primitive_type == UI_PRIMITIVE_TEXT_GLYPH) || 
	                (node->final_screen_size[X] > 0.0f && node->final_screen_size[Y] > 0.0f);

	const bool has_color =
			(node->outer_color[3] > 0.0f) || 
			(node->inner_color[3] > 0.0f) ||
			(node->text_color[3] > 0.0f);

	if (has_size && has_color) {
		if (node->primitive_type == UI_PRIMITIVE_TEXT_GLYPH) {
			pushTextInstances(
				node,
				instances,
				count,
				consoleCursor(queryFlag32(node->text_flags, TEXT_HAS_CURSOR))
			);
		} else {
			pushShapeInstance(node, instances, count);
		}
	}

	u16 child_idx = node->first_child;
	while (child_idx != UI_NULL_INDEX) {
		traverseForRender(child_idx, instances, count);
		child_idx = uiGet(child_idx)->next_sibling;
	}
}

UiRenderInfo	buildUiRenderInfo(u16 rm_root_node)
{
	UiRenderInfo	info = {};

	// Pessimistic allocation: MAX_COMPONENTS is the maximum possible instances
	// Since it's a frame arena, this over-allocation is instantaneous and free.
	u64 max_bytes = MAX_COMPONENTS * sizeof(UiRenderInstance);

	info.render_instances = (UiRenderInstance *)g_frame_arena.fp_allocation(&g_frame_arena, max_bytes, DEFAULT_ALIGN);
	memset(info.render_instances, 0, max_bytes);
	info.instance_count = 0;
	info.px_range = 8.0;

	traverseForRender(rm_root_node, info.render_instances, &info.instance_count);
	info.upload_size = info.instance_count * sizeof(UiRenderInstance);
	return (info);
}

// --- OTHER STUFF ---
void	openConsole(u32 screen_w, u32 screen_h, Font *font, u8 console_font_size)
{
	static f32	scroll_y = 0.0f;
	if (scroll_y < 0.0f) scroll_y = 0.0f;

	const f32	third_height = (f32)screen_h / 3.0f;
	const f32	input_padding = 10.0f;
	const f32	input_panel_height = console_font_size + (input_padding * 2.0f);
	const f32	log_panel_height = third_height - input_panel_height;

	// --- MAIN PANEL --- //
	imguiBeginPanel((PanelSpec){
		.primitive_type	= UI_PRIMITIVE_RECTANGLE,
		.layout_type	= UI_LAYOUT_VERTICAL,
		.offset		= {0.0f, 0.0f},
		.fixed_size	= {(f32)screen_w, third_height},
		.inner_color	= {0.5f, 0.5f, 0.5f, 1.0f},
		.padding	= 0.0f,
		.spacing	= 0.0f
	});

	// --- LOG PANEL --- //
	// This panel takes up all the space ABOVE the input box
	imguiBeginPanel((PanelSpec){
		.primitive_type	= UI_PRIMITIVE_RECTANGLE,
		.layout_type	= UI_LAYOUT_VERTICAL,
		.fixed_size	= {(f32)screen_w, log_panel_height},
		.inner_color	= {0.0f, 0.0f, 0.0f, 0.0f}, // Transparent so main background shows
		.padding	= 5.0f,
		.spacing	= 2.0f
	});

	u16	current_history_idx;
	ConsoleCommand	*commands = consoleHist(&current_history_idx);
	
	for (u16 i = 0; i < MAX_HISTORY; i++) {
		u16	idx = (current_history_idx + i) % MAX_HISTORY;
		if (commands[idx].command_len > 0) {
			imguiText((TextSpec){
				.font		= font,
				.font_size	= console_font_size,
				.text_color	= {0.8f, 0.8f, 0.8f, 1.0f},
				.nudge_y	= -scroll_y // Manual scrolling offset
			}, "%s", commands[idx].command);
		}
	}
	imguiEndPanel(); // Pops Log Panel

	// --- TEXT PANEL (INPUT) --- //
	// Because of UI_LAYOUT_VERTICAL on the Main Panel, the cursor is automatically 
	// positioned exactly where the Log Panel ended!
	imguiBeginPanel((PanelSpec){
		.primitive_type	= UI_PRIMITIVE_RECTANGLE,
		.layout_type	= UI_LAYOUT_VERTICAL,
		.fixed_size	= {(f32)screen_w, input_panel_height},
		.outer_color	= {1.0f, 1.0f, 1.0f, 1.0f},
		.inner_color	= {0.4f, 0.4f, 0.4f, 1.0f},
		.border_width	= 2.0f,
		.padding	= input_padding,
		.spacing	= 0.0f
	});

	imguiText((TextSpec){
		.font		= font,
		.font_size	= console_font_size,
		.text_color	= {1.0f, 1.0f, 1.0f, 1.0f},
		.flags		= TEXT_HAS_CURSOR
	}, "> %S", consoleInput());
	
	imguiEndPanel(); // Pops Text Input Panel
	imguiEndPanel(); // Pops Main Panel
}

void	showFps(Font *font, double fps)
{
	imguiBeginPanel((PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_VERTICAL,
		.anchor         = UI_ANCHOR_TOP_RIGHT,
	});
	
	imguiText((TextSpec){
		.anchor     = UI_ANCHOR_CENTER,
		.font       = font,
		.font_size  = 12,
		.text_color = {1.0f, 1.0f, 1.0f, 1.0f},
		.nudge_x    = 1.0f
	}, "FPS: %.1f", fps);
	
	imguiEndPanel();
}

void drawEditorUI(GraphicsContext *ctx, Font *font)
{
	const f32 screen_w = (f32)ctx->swapchain_width;
	const f32 screen_h = (f32)ctx->swapchain_height;

	// 1. Define our panel dimensions
	const f32 toolbar_h     = 40.0f;
	const f32 left_panel_w  = 250.0f;
	const f32 right_panel_w = 300.0f;

	// 2. Calculate and apply the 3D Viewport boundaries for the Grid/PBR passes!
	ctx->viewport_offset[X] = (int)left_panel_w;
	ctx->viewport_offset[Y] = (int)toolbar_h;
	ctx->viewport_extent[X] = (uint)(screen_w - left_panel_w - right_panel_w);
	ctx->viewport_extent[Y] = (uint)(screen_h - toolbar_h);

	ButtonSpec btn_spec = {
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
	if (imguiButton(STRING_LIT("Play (Game Mode)"), btn_spec)) {
		gameStateSet(&g_game_state, NoClip, 0);
		g_engine_mode = ENGINE_MODE_GAME;
	}
	imguiButton(STRING_LIT("Save Scene"), btn_spec);
	imguiButton(STRING_LIT("Settings"), btn_spec);
	
	imguiEndPanel(); // Pop Toolbar

	// --- LEFT PANEL (Hierarchy) ---
	imguiBeginPanel((PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_VERTICAL,
		.offset         = {0.0f, toolbar_h}, // Start right below the toolbar
		.fixed_size     = {left_panel_w, screen_h - toolbar_h},
		.inner_color    = {0.2f, 0.2f, 0.2f, 1.0f},
		.padding        = 10.0f,
		.spacing        = 4.0f
	});
	
	imguiText(header_text, "Scene Hierarchy");
	imguiText((TextSpec){.font=font, .font_size=14, .text_color={0.7f, 0.7f, 0.7f, 1.0f}}, " - Main Camera");
	imguiText((TextSpec){.font=font, .font_size=14, .text_color={0.7f, 0.7f, 0.7f, 1.0f}}, " - Directional Light");
	imguiButton(STRING_LIT("Player Entity"), btn_spec);
	
	imguiEndPanel(); // Pop Left Panel

	// --- RIGHT PANEL (Inspector) ---
	imguiBeginPanel((PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_VERTICAL,
		.offset         = {screen_w - right_panel_w, toolbar_h}, // Dock to Right edge
		.fixed_size     = {right_panel_w, screen_h - toolbar_h},
		.inner_color    = {0.2f, 0.2f, 0.2f, 1.0f},
		.padding        = 10.0f,
		.spacing        = 15.0f
	});

	imguiText(header_text, "Inspector");
	
	// Nested Panel for a "Component" block (Auto-height shrink-wrapping!)
	imguiBeginPanel((PanelSpec){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type    = UI_LAYOUT_VERTICAL,
		.fixed_size     = {right_panel_w - 20.0f, 0.0f}, // 0.0f height = Auto shrink-wrap
		.inner_color    = {0.25f, 0.25f, 0.25f, 1.0f},
		.padding        = 8.0f,
		.spacing        = 6.0f
	});
	
	imguiText((TextSpec){.font=font, .font_size=14, .text_color={0.9f, 0.9f, 0.9f, 1.0f}}, "Transform");
	imguiButton(STRING_LIT("Reset Position"), btn_spec);
	// (Future float sliders go here!)
	
	imguiEndPanel(); // Pop Component Block
	imguiEndPanel(); // Pop Right Panel
}
