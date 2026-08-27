#pragma once

#include "base_layer.h"
#include "fonts.h"
#include "graphics_layer.h"

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

typedef struct UiComponent
{
	u16	shape;		// The component shape, first 8bits
				// are the shape, after the fill type

	vec2	pos;		// Centre of the component
	vec2	size;
	vec4	color;
}	UiComponent;

typedef struct UiState
{
	Allocator	ui_allocator;

	UiComponent	*components;
	u32		component_count;
	u32		components_used;
}	UiState;

void	initUi(UiState *ui);
void	uiComponentCreate(UiState *ui, UiComponent component, Font *font);

u16	setShape(enum ComponentShapeType shape);
u16	setShapeFill(enum ComponentShapeFillType fill);
