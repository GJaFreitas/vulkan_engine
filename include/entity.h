#pragma once

#include "graphics_layer.h"
#include "base_layer.h"

typedef union {
	uint64_t	id;
	struct {
		uint32_t	index;
		uint32_t	generation;
	};
}	EntityHandle;

typedef struct EntityColdData {
	bool		is_selected;
	char		str[32];
	u8		str_count;
	u64		guid;
}	EntityColdData;

typedef struct EntityHotData {
	u64		id;
	PipelineState	pipeline_state;
	ModelHandle	model_handle;
	bool		active;
	vec3		pos;
	versor		rotation;
	f32		scale;
}	EntityHotData;

// The Sparse Set Registry
typedef struct EntityRegistry {
	u32			count;
	u32			capacity;
	
	// Dense arrays (packed tightly, size = count)
	// NOTE: Possible optimization. Isntead of checking hot->active in hot
	// code paths i can just have a u32 active count and push unactive
	// entities back
	EntityHotData		*hot_data;
	EntityColdData		*cold_data;
	u32			*dense_to_sparse;

	// Sparse arrays (indexed by handle.index, size = capacity)
	u32			*sparse_to_dense;
	u32			*generations;

	// Freelist for reusing sparse indices when entities are destroyed
	u32			*free_indices;
	u32			free_count;
}	EntityRegistry;

EntityHotData	*getEntityHot(EntityRegistry *reg, EntityHandle handle);
EntityColdData	*getEntityCold(EntityRegistry *reg, EntityHandle handle);
EntityHandle	createEntity(EntityRegistry *reg, ModelCache *cache, GraphicsContext *ctx, String model_path);
void		initEntityRegistry(EntityRegistry *reg, Allocator *a, u32 max_entities);
