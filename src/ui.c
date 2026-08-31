#include "ui.h"

typedef struct ComponentPool
{
	u16	hint;
	u16	free_count;
	u16	imgui_count;

	bool	*used;
	u8	*mem;
}	ComponentPool;

#define IMGUI_STACK_MAX 64

typedef struct UiContext {
	u16	imgui_root;
	u16	rmgui_root;

	// IMGUI Layout Stack
	u16	imgui_parent_stack[IMGUI_STACK_MAX];
	u32	imgui_stack_count;

	// IMGUI Cross-Frame State (Hashes, not indices!)
	u32	hot_item_hash;    // The item the mouse is hovering over
	u32	active_item_hash; // The item currently being clicked/dragged
} UiContext;

UiContext	g_ui_ctx = {};

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
	g_ui_ctx.imgui_stack_count = 0;

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

void	imguiText(TextSpec spec, Allocator *frame_arena, const char *fmt, ...)
{
	// 1. Get the current active panel from the stack
	if (g_ui_ctx.imgui_stack_count == 0) {
		engine_error(LOG_FILE, "imgui_text called without an active panel!");
		return;
	}
	u16 parent_idx = g_ui_ctx.imgui_parent_stack[g_ui_ctx.imgui_stack_count - 1];

	// 2. Format the string dynamically
	va_list args;
	va_start(args, fmt);
	// Figure out exactly how much memory the formatted string needs
	int len = stbsp_vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	// Allocate that exact amount from the frame arena (+1 for null terminator)
	u8 *text_data = frame_arena->fp_allocation(frame_arena, len + 1, DEFAULT_ALIGN);

	// Actually write the formatted string into the arena memory
	va_start(args, fmt);
	stbsp_vsnprintf((char *)text_data, len + 1, fmt, args);
	va_end(args);

	// 3. Allocate and link the component
	u16 text_idx = imguiAllocateComponent();
	uiAppendChild(parent_idx, text_idx);

	UiComponent *text = uiGet(text_idx);
	text->primitive_type = UI_PRIMITIVE_TEXT_GLYPH;
	text->anchor = spec.anchor;
	text->font = spec.font;
	text->font_size = spec.font_size;
	text->fixed_size[Y] = spec.font_size + 4.0f; // Y padding
	text->size_mode[X] = UI_SIZE_AUTO;
	text->size_mode[Y] = UI_SIZE_AUTO;
	glm_vec4_copy(spec.text_color, text->text_color);
	text->is_text_dirty = true;
	text->offset[X] = spec.nudge_x;
	text->offset[Y] = spec.nudge_y;

	// Point the component to the arena string
	text->text.data = text_data;
	text->text.count = len;
}

void	imguiSetActivePanel(u16 idx)
{
	g_ui_ctx.imgui_parent_stack[g_ui_ctx.imgui_stack_count++] = idx;
}

void	imguiBeginPanel(u16 parent_node, PanelSpec spec)
{
	u16 panel_idx = imguiAllocateComponent();
	UiComponent *panel = uiGet(panel_idx);

	panel->primitive_type = spec.primitive_type;
	panel->layout_type = spec.layout_type;
	panel->anchor = spec.anchor;
	panel->size_mode[X] = spec.size_mode[X];
	panel->size_mode[Y] = spec.size_mode[Y];
	panel->offset[X] = spec.offset[X];
	panel->offset[Y] = spec.offset[Y];
	panel->fixed_size[X] = spec.fixed_size[X];
	panel->fixed_size[Y] = spec.fixed_size[Y];
	glm_vec4_copy(spec.outer_color, panel->outer_color);
	glm_vec4_copy(spec.inner_color, panel->inner_color);
	panel->border_width = spec.border_width;
	panel->corner_radius = spec.corner_radius;
	panel->padding = spec.padding;

	// Anchor it to the provided RMGUI node (usually the screen root)
	uiAppendChild(parent_node, panel_idx);

	// Push to the context stack so imgui_text knows who its parent is
	imguiSetActivePanel(panel_idx);
}

void	imguiEndPanel(void)
{
	// Pop the panel off the stack
	if (g_ui_ctx.imgui_stack_count > 0) {
		g_ui_ctx.imgui_stack_count--;
	}
}

bool	imguiButton(StringView text)
{
	u16 button_idx = imguiAllocateComponent();

	// Automatically link to whatever the current IMGUI parent is
	u16 current_parent = g_ui_ctx.imgui_parent_stack[g_ui_ctx.imgui_stack_count - 1];
	uiAppendChild(current_parent, button_idx);

	// Hash the text to check cross-frame state (clicks/hovers)
	u32 id = hashString32(text);
	print("Id: %u\n", id);
	// TODO: Change this
	return true;
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

static void pushTextInstances(UiComponent *node, UiRenderInstance *instances, u32 *count)
{
	const TextAtlas *atlas = &node->font->atlas;
	const f32 px_per_em = node->font_size;
	
	f32 cursor_x = node->final_screen_pos[X];
	// Font size so that the baseline of the text aligns with the box its in
	f32 cursor_y = node->final_screen_pos[Y] + node->font_size;

	// Loop over our cached glyphs instead of calling HarfBuzz!
	for (u32 i = 0; i < node->shaped_glyph_count; i++) {
		UiShapedGlyph *cached = &node->shaped_glyphs[i];
		const GlyphInfo *g = atlasFindGlyph(atlas, cached->atlas_index);

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
			pushTextInstances(node, instances, count);
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

void uiCalculateSizes(u16 node_idx, Allocator *arena)
{
	UiComponent *node = uiGet(node_idx);

	// 1. Traverse to the absolute bottom of the tree first
	u16 child_idx = node->first_child;
	while (child_idx != UI_NULL_INDEX) {
		uiCalculateSizes(child_idx, arena);
		child_idx = uiGet(child_idx)->next_sibling;
	}

	// 2. Now calculate our own size
	for (int axis = 0; axis < 2; axis++) {
		if (node->size_mode[axis] == UI_SIZE_FIXED) {
			node->final_screen_size[axis] = node->fixed_size[axis];
		} 
		else if (node->size_mode[axis] == UI_SIZE_AUTO) {
			
			// === TEXT CACHING & MEASUREMENT ===
			if (node->primitive_type == UI_PRIMITIVE_TEXT_GLYPH) {
				if (node->is_text_dirty == false) break ;

				// 1. Run HarfBuzz
				static hb_buffer_t *buf = NULL;
				if (!buf) buf = hb_buffer_create();

				Font *font = node->font;
				const f32 hb_scale = node->font_size / (f32)font->units_per_EM;

				hb_buffer_add_utf8(buf, (const char *)node->text.data, node->text.count, 0, -1);
				hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
				hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
				hb_buffer_set_language(buf, hb_language_from_string("en", -1));
				hb_shape(font->hb_font, buf, NULL, 0);

				u32 g_count;
				hb_glyph_info_t *g_info = hb_buffer_get_glyph_infos(buf, &g_count);
				hb_glyph_position_t *g_pos = hb_buffer_get_glyph_positions(buf, &g_count);

				// 2. Allocate cache array from the arena
				// TODO: Investigate possible optimizations by caching text for rmgui
				node->shaped_glyph_count = g_count;
				node->shaped_glyphs = arena->fp_allocation(arena, sizeof(UiShapedGlyph) * g_count, DEFAULT_ALIGN);

				// 3. Cache the glyphs AND calculate the total width
				f32 total_width = 0.0f;
				for (u32 i = 0; i < g_count; i++) {
					node->shaped_glyphs[i].atlas_index = g_info[i].codepoint;
					node->shaped_glyphs[i].x_advance = g_pos[i].x_advance * hb_scale;
					node->shaped_glyphs[i].y_advance = g_pos[i].y_advance * hb_scale;

					total_width += node->shaped_glyphs[i].x_advance;
				}

				node->final_screen_size[X] = total_width;
				node->final_screen_size[Y] = node->font_size;

				hb_buffer_reset(buf);
				node->is_text_dirty = false;
			} else {

				// If it's a container, size is based on children
				f32 children_sum = 0.0f;
				f32 max_child_size = 0.0f;

				u16 c_idx = node->first_child;
				while (c_idx != UI_NULL_INDEX) {
					UiComponent *child = uiGet(c_idx);

					// Calculate total stacked size, and largest single child
					children_sum += child->final_screen_size[axis];
					if (child->final_screen_size[axis] > max_child_size) {
						max_child_size = child->final_screen_size[axis];
					}

					c_idx = child->next_sibling;
				}

				// If we stack in this axis, our size is the sum. 
				// If we stack in the opposite axis, our size is the largest child.
				if ((axis == X && node->layout_type == UI_LAYOUT_HORIZONTAL) ||
					(axis == Y && node->layout_type == UI_LAYOUT_VERTICAL)) {
					node->final_screen_size[axis] = children_sum + node->padding * 2.0f;
				} else {
					node->final_screen_size[axis] = max_child_size + node->padding * 2.0f;
				}

				if (node->first_child != UI_NULL_INDEX) {
					// TODO: Create padding optional in struct for use here
					node->final_screen_size[axis] += 10.0f; // 5px padding on each side
				}
			}
		}
	}
}

void	uiCalculatePositions(u16 node_idx, f32 parent_x, f32 parent_y, f32 parent_w, f32 parent_h)
{
	UiComponent	*node = uiGet(node_idx);
	UiComponent	*parent_node = uiGet(node->parent);

	// 1. Root nodes take screen dimensions. Everyone else uses their computed size.
	if (node_idx == g_ui_ctx.rmgui_root || node_idx == g_ui_ctx.imgui_root) {
		node->final_screen_pos[X] = parent_x;
		node->final_screen_pos[Y] = parent_y;
		node->final_screen_size[X] = parent_w;
		node->final_screen_size[Y] = parent_h;
		node->clip_rect[0] = 0.0f; node->clip_rect[1] = 0.0f;
		node->clip_rect[2] = parent_w; node->clip_rect[3] = parent_h;
	} else {
		// Map the anchor enum to a 0.0 -> 1.0 multiplier
		f32 anchor_x = 0.0f; f32 anchor_y = 0.0f;
		switch (node->anchor) {
			case UI_ANCHOR_TOP_LEFT:      anchor_x = 0.0f; anchor_y = 0.0f; break;
			case UI_ANCHOR_TOP_CENTER:    anchor_x = 0.5f; anchor_y = 0.0f; break;
			case UI_ANCHOR_TOP_RIGHT:     anchor_x = 1.0f; anchor_y = 0.0f; break;
			case UI_ANCHOR_CENTER_LEFT:   anchor_x = 0.0f; anchor_y = 0.5f; break;
			case UI_ANCHOR_CENTER:        anchor_x = 0.5f; anchor_y = 0.5f; break;
			case UI_ANCHOR_CENTER_RIGHT:  anchor_x = 1.0f; anchor_y = 0.5f; break;
			case UI_ANCHOR_BOTTOM_LEFT:   anchor_x = 0.0f; anchor_y = 1.0f; break;
			case UI_ANCHOR_BOTTOM_CENTER: anchor_x = 0.5f; anchor_y = 1.0f; break;
			case UI_ANCHOR_BOTTOM_RIGHT:  anchor_x = 1.0f; anchor_y = 1.0f; break;
		}

		node->final_screen_pos[X] = parent_x + (parent_w * anchor_x) + node->offset[X] - (node->final_screen_size[X] * anchor_x);
		node->final_screen_pos[Y] = parent_y + (parent_h * anchor_y) + node->offset[Y] - (node->final_screen_size[Y] * anchor_y);
		node->clip_rect[0] = MAX(parent_node->clip_rect[0], parent_node->final_screen_pos[X]);
		node->clip_rect[1] = MAX(parent_node->clip_rect[1], parent_node->final_screen_pos[Y]);
		node->clip_rect[2] = MIN(parent_node->clip_rect[2], parent_node->final_screen_pos[X] + parent_node->final_screen_size[X]);
		node->clip_rect[3] = MIN(parent_node->clip_rect[3], parent_node->final_screen_pos[Y] + parent_node->final_screen_size[Y]);
	}

	// 2. Cascade down to children
	f32 cursor_x = 0.0f; // Tracks stacking offsets
	f32 cursor_y = 0.0f; 

	u16 child_idx = node->first_child;
	while (child_idx != UI_NULL_INDEX) {
		UiComponent *child = uiGet(child_idx);
		
		f32 original_offset_x = child->offset[X];
		f32 original_offset_y = child->offset[Y];

		// If stacking, push the child down/right by the cursor amount
		if (node->layout_type == UI_LAYOUT_VERTICAL) {
			child->offset[Y] += cursor_y; 
		} else if (node->layout_type == UI_LAYOUT_HORIZONTAL) {
			child->offset[X] += cursor_x;
		}

		// Recurse using the inner padded rectangle
		uiCalculatePositions(child_idx, 
		                     node->final_screen_pos[X] + node->padding, 
		                     node->final_screen_pos[Y] + node->padding,
		                     node->final_screen_size[X] - (node->padding * 2.0f), 
		                     node->final_screen_size[Y] - (node->padding * 2.0f));

		// Restore offsets so IMGUI doesn't infinitely accumulate them
		child->offset[X] = original_offset_x;
		child->offset[Y] = original_offset_y;

		// Advance cursor by the child's computed size
		if (node->layout_type == UI_LAYOUT_VERTICAL) {
			cursor_y += child->final_screen_size[Y]; 
		} else if (node->layout_type == UI_LAYOUT_HORIZONTAL) {
			cursor_x += child->final_screen_size[X];
		}
		
		child_idx = child->next_sibling;
	}
}

void	uiCalculateLayout(f32 screen_w, f32 screen_h, Allocator *frame_arena)
{
	uiCalculateSizes(g_ui_ctx.rmgui_root, frame_arena);
	uiCalculatePositions(g_ui_ctx.rmgui_root, 0, 0, screen_w, screen_h);
}

// TODO: Remove extra u16
TextRenderInfo	uiBuildRenderData(u16 rm_root_node, Allocator *frame_arena)
{
	TextRenderInfo	info = {};

	// Pessimistic allocation: MAX_COMPONENTS is the maximum possible instances
	// Since it's a frame arena, this over-allocation is instantaneous and free.
	u64 max_bytes = MAX_COMPONENTS * sizeof(UiRenderInstance);

	info.render_instances = (UiRenderInstance *)frame_arena->fp_allocation(frame_arena, max_bytes, DEFAULT_ALIGN);
	memset(info.render_instances, 0, max_bytes);
	info.instance_count = 0;
	info.px_range = 8.0;

	traverseForRender(rm_root_node, info.render_instances, &info.instance_count);
	info.upload_size = info.instance_count * sizeof(UiRenderInstance);
	return (info);
}

// --- OTHER STUFF ---
void	openConsole(u32 screen_w, u32 screen_h, Font *font, u8 console_font_size, Allocator *frame_arena)
{
	static f32	scroll_y;

	if (scroll_y < 0.0f) scroll_y = 0.0f;

	// --- MAIN PANEL --- //
	const u32	third_height = screen_h / 3;
	const u16	main_panel_idx = imguiAllocateComponent();
	imguiSetActivePanel(main_panel_idx);
	uiAppendChild(g_ui_ctx.imgui_root, main_panel_idx);
	UiComponent	*panel_component = uiGet(main_panel_idx);
	*panel_component = (UiComponent){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type = UI_LAYOUT_VERTICAL,
		.anchor = UI_ANCHOR_TOP_LEFT,
		.size_mode = {UI_SIZE_FIXED, UI_SIZE_FIXED},
		.offset = {},
		.fixed_size = {screen_w, third_height},
		.outer_color = {},
		.inner_color = {0.5f, 0.5f, 0.5f, 1.0f},
		.border_width = 0,
		.corner_radius = 0,
		.padding = 0,
	};

	TextSpec	text_console_spec = {
		.anchor = UI_ANCHOR_TOP_LEFT,
		.font = font,
		.font_size = console_font_size,
		.text_color = {1, 1, 1, 1},
		.nudge_x = 0,
		.nudge_y = -scroll_y,
	};
	imguiText(text_console_spec, frame_arena, "%S", consoleHist());

	// --- TEXT PANEL --- //
	const u32	text_box_w = screen_w;
	const u16	text_panel_idx = imguiAllocateComponent();
	imguiSetActivePanel(text_panel_idx);
	uiAppendChild(main_panel_idx, text_panel_idx);
	UiComponent	*text_component = uiGet(text_panel_idx);
	*text_component = (UiComponent){
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type = UI_LAYOUT_VERTICAL,
		.anchor = UI_ANCHOR_BOTTOM_LEFT,
		.size_mode = {UI_SIZE_FIXED, UI_SIZE_AUTO},
		.offset = {},
		.fixed_size = {text_box_w, 0},
		.outer_color = {1, 1, 1, 1},
		.inner_color = {0.5f, 0.5f, 0.5f, 1.0f},
		.border_width = 2.0f,
		.corner_radius = 0,
		.padding = 10.0f,
	};

	TextSpec	text_input_spec = {
		.anchor = UI_ANCHOR_BOTTOM_LEFT,
		.font = font,
		.font_size = console_font_size,
		.text_color = {255, 255, 255, 255},
		.nudge_x = 0,
		.nudge_y = 0,
	};
	imguiText(text_input_spec, frame_arena, "> %S", consoleInput());

	imguiEndPanel();
	imguiEndPanel();
}

void	showFps(u16 imgui_root, Font *font, double fps, Allocator *frame_arena)
{
	const PanelSpec spec = {
		.primitive_type = UI_PRIMITIVE_RECTANGLE,
		.layout_type = UI_LAYOUT_VERTICAL,
		.anchor = UI_ANCHOR_TOP_RIGHT,
		.size_mode = {UI_SIZE_AUTO, UI_SIZE_AUTO},
		.offset = {},
		.fixed_size = {}, // Not needed since auto sizing
		.outer_color = {},
		.inner_color = {},
		.border_width = 0.0f,
		.corner_radius = 0.0f,
		.padding = 0.0f,
	};
	const TextSpec tspec = {
		.anchor = UI_ANCHOR_CENTER,
		.font = font,
		.font_size = 12,
		.text_color = {255, 255, 255, 255},
		.nudge_x = 1.0f,
	};
	imguiBeginPanel(imgui_root, spec);
	imguiText(tspec, frame_arena, "FPS: %.1f", fps);
	imguiEndPanel();
}
