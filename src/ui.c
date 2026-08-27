#include "ui.h"

u32	getShape(u16 n) {
	return (n >> 8);
}

u16	setShape(enum ComponentShapeType shape) {
	return (shape << 8);
}

void	uiComponentCreate(UiState *ui, UiComponent component, Font *font)
{
	// ui->components[ui->components_used] = component;
	// ui->components_used++;

	u32	type = getShape(component.shape);

	UiRenderInstance	*inst = &font->render_instances[font->glyph_count++];

	glm_vec2_copy(component.pos, inst->pos);
	glm_vec2_copy(component.size, inst->size);
	glm_vec4_copy(component.color, inst->color);
	inst->primitive_type = type;
	inst->corner_radius = 0.0;
}

void	initUi(UiState *ui)
{
	ui->ui_allocator = newArenaAllocator(MB(1), NULL, DEFAULT_ALIGN);

	ui->component_count = 32;
	ui->components_used = 0;
	ui->components = malloc(sizeof(UiComponent) * 32);
}
