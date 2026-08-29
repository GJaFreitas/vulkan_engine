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

void	imguiText(Font *font, f32 font_size, Allocator *frame_arena, const char *fmt, ...)
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
	text->font = font;
	text->font_size = font_size;

	// Since the parent panel is UI_LAYOUT_VERTICAL, we only need to define its height padding
	text->fixed_size[Y] = font_size + 4.0f; // Give it a little breathing room
	text->offset[X] = 10.0f; // Indent slightly from the panel edge

	// Point the component to the arena string
	text->text.data = text_data;
	text->text.count = len;

	// Solid white text
	text->color[0] = 1.0f; text->color[1] = 1.0f; text->color[2] = 1.0f; text->color[3] = 1.0f;
}

void	imguiBeginPanel(u16 parent_node, UiAnchor anchor, f32 x, f32 y, f32 w, f32 h)
{
	u16 panel_idx = imguiAllocateComponent();
	UiComponent *panel = uiGet(panel_idx);

	// Layout and Styling
	panel->layout_type = UI_LAYOUT_VERTICAL; // Automatically stack contents downwards
	panel->anchor = anchor;
	panel->offset[X] = x;
	panel->offset[Y] = y;
	panel->fixed_size[X] = w;
	panel->fixed_size[Y] = h;
	panel->primitive_type = UI_PRIMITIVE_RECTANGLE;
	panel->corner_radius = 6.0f;

	// Dark semi-transparent background
	panel->color[0] = 0.0f; panel->color[1] = 0.0f; panel->color[2] = 0.0f; panel->color[3] = 0.6f;

	// Anchor it to the provided RMGUI node (usually the screen root)
	uiAppendChild(parent_node, panel_idx);

	// Push to the context stack so imgui_text knows who its parent is
	g_ui_ctx.imgui_parent_stack[g_ui_ctx.imgui_stack_count++] = panel_idx;
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
		.color = {0, 0, 0, 0},
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
		.color = {0, 0, 0, 0},
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

	// Copy styling...
	instances[idx].primitive_type = node->primitive_type; 
	// ... set colors, radius, etc.

	(*count)++;
}

// »speed, this can be cached
static void pushTextInstances(UiComponent *node, UiRenderInstance *instances, u32 *count)
{
	static hb_buffer_t *buf = NULL;
	if (!buf) {
		buf = hb_buffer_create();
	}

	Font *font = node->font;
	const TextAtlas *atlas = &font->atlas;
	const f32 px_per_em = node->font_size;
	const f32 hb_scale = node->font_size / (f32)font->units_per_EM;

	// Note: using node->text (assuming it is your String type)
	hb_buffer_add_utf8(buf, (const char *)node->text.data, node->text.count, 0, -1);
	hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buf, hb_language_from_string("en", -1));

	hb_feature_t features[] = {
		{ HB_TAG('c','a','l','t'), 0, 0, (unsigned int)-1 },
		{ HB_TAG('l','i','g','a'), 0, 0, (unsigned int)-1 },
		{ HB_TAG('c','l','i','g'), 0, 0, (unsigned int)-1 },
	};
	hb_shape(font->hb_font, buf, features, sizeofarray(features));

	u32 glyph_count;
	hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
	hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

	f32 cursor_x = node->final_screen_pos[X];
	f32 cursor_y = node->final_screen_pos[Y];

	for (u32 i = 0; i < glyph_count; i++) {
		const u32 glyph_index = glyph_info[i].codepoint;
		const GlyphInfo *g = atlasFindGlyph(atlas, glyph_index);

		if (g) {
			const f32 glyph_width = g->plane_max[0] - g->plane_min[0];
			const f32 glyph_height = g->plane_max[1] - g->plane_min[1];

			if (glyph_width > 0 && glyph_height > 0) {

				u32 idx = *count;
				UiRenderInstance *inst = &instances[idx];

				inst->primitive_type = UI_PRIMITIVE_TEXT_GLYPH;
				inst->stroke_width   = 0.0f;
				inst->corner_radius  = 0.0f;

				const f32 left = g->plane_min[0] * px_per_em;
				const f32 top = g->plane_min[1] * px_per_em;
				const f32 right = g->plane_max[0] * px_per_em;
				const f32 bottom = g->plane_max[1] * px_per_em;

				inst->pos[X] = roundf(cursor_x + left);
				inst->pos[Y] = roundf(cursor_y + top);

				inst->size[X] = right - left;
				inst->size[Y] = bottom - top;

				inst->uv_offset[X] = g->uv_min[0];
				inst->uv_offset[Y] = g->uv_min[1];

				inst->uv_size[X] = g->uv_max[0] - g->uv_min[0];
				inst->uv_size[Y] = g->uv_max[1] - g->uv_min[1];

				glm_vec4_copy(node->color, inst->color);

				(*count)++;
			}
		}

		cursor_x += glyph_pos[i].x_advance * hb_scale;
		cursor_y += glyph_pos[i].y_advance * hb_scale;
	}

	hb_buffer_reset(buf);
}

static void traverseForRender(u16 node_idx, UiRenderInstance *instances, u32 *count)
{
	UiComponent *node = uiGet(node_idx);

	// A node is visible if it's text, OR if it has physical dimensions. And it must have alpha > 0.
	bool has_size = (node->primitive_type == UI_PRIMITIVE_TEXT_GLYPH) || 
	                (node->final_screen_size[X] > 0.0f && node->final_screen_size[Y] > 0.0f);

	if (has_size && node->color[3] > 0.0f) {
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

void uiCalculateLayout(u16 node_idx, f32 parent_x, f32 parent_y, f32 parent_w, f32 parent_h)
{
	UiComponent *node = uiGet(node_idx);

	// 1. Root nodes take the passed-in screen dimensions. Everyone else uses fixed_size.
	if (node_idx == g_ui_ctx.rmgui_root || node_idx == g_ui_ctx.imgui_root) {
		node->final_screen_pos[X] = parent_x;
		node->final_screen_pos[Y] = parent_y;
		node->final_screen_size[X] = parent_w;
		node->final_screen_size[Y] = parent_h;
	} else {
		node->final_screen_size[X] = node->fixed_size[X];
		node->final_screen_size[Y] = node->fixed_size[Y];

		// Map the anchor enum to a 0.0 -> 1.0 multiplier
		f32 anchor_x = 0.0f;
		f32 anchor_y = 0.0f;
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

		// Calculate position
		node->final_screen_pos[X] = parent_x + (parent_w * anchor_x) + node->offset[X] - (node->final_screen_size[X] * anchor_x);
		node->final_screen_pos[Y] = parent_y + (parent_h * anchor_y) + node->offset[Y] - (node->final_screen_size[Y] * anchor_y);
	}

	// 2. Cascade down to children
	u16 child_idx = node->first_child;
	while (child_idx != UI_NULL_INDEX) {
		uiCalculateLayout(child_idx, 
		    node->final_screen_pos[X], 
		    node->final_screen_pos[Y],
		    node->final_screen_size[X], 
		    node->final_screen_size[Y]);

		child_idx = uiGet(child_idx)->next_sibling;
	}
}

// TODO: Remove extra u16
TextRenderInfo	uiBuildRenderData(u16 rm_root_node, Allocator *frame_arena)
{
	TextRenderInfo	info = {};

	// Pessimistic allocation: MAX_COMPONENTS is the maximum possible instances
	// Since it's a frame arena, this over-allocation is instantaneous and free.
	u64 max_bytes = MAX_COMPONENTS * sizeof(UiRenderInstance);

	info.render_instances = (UiRenderInstance *)frame_arena->fp_allocation(frame_arena, max_bytes, DEFAULT_ALIGN);
	info.instance_count = 0;
	info.px_range = 8.0;

	traverseForRender(rm_root_node, info.render_instances, &info.instance_count);
	info.upload_size = info.instance_count * sizeof(UiRenderInstance);
	return (info);
}
