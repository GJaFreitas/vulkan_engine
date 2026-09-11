#ifndef SHARED_STRUCTS_H
#define SHARED_STRUCTS_H

#define GLOBAL_SAMPLER_PBR	0
#define GLOBAL_SAMPLER_UI	1
#define GLOBAL_SAMPLER_COUNT	2

#ifdef __SLANG__
	// =========================================================================
	// SLANG / GPU ENVIRONMENT
	// =========================================================================
	typedef uint        u32;
	typedef uint64_t    u64;
	typedef float       f32;

	// In Slang, we want BDA pointers to be strictly typed so we can dereference them
	#define BDA_PTR(Type) Type*
	#define SHARED_STRUCT(name) struct name
#else
	// =========================================================================
	// C / CPU ENVIRONMENT
	// =========================================================================
#include <stdint.h>

	// Assuming you have typedefs for u32, u64, and cglm included already:
	#include "typedefs.h"

	// In C, Vulkan Buffer Device Addresses are just 64-bit integers
	#define BDA_PTR(Type) u64
	#define SHARED_STRUCT(name) typedef struct name name; struct name
#endif

// =========================================================================
// SHARED DATA STRUCTURES
// =========================================================================

SHARED_STRUCT(Vertex)
{
	float3 pos;
	float3 normal;
	float2 uv;
	float4 tangent;
};

SHARED_STRUCT(EntityInstanceData)
{
	float4x4 model_mat;
};

#define MAX_POINT_LIGHTS 16
SHARED_STRUCT(PointLight)
{
	float4 position; // xyz = position, w = radius (for attenuation cutoff)
	float4 color;    // rgb = color, a = intensity
};

SHARED_STRUCT(UniformBufferObject)
{
	float4x4	view;
	float4x4	proj;
	float4x4	inv_view;
	float4x4	inv_proj;

	float4x4	light_space_matrices[4];
	float4	cascade_split_depths;

	float4	sun_direction;
	float4	sun_color;
	PointLight	point_lights[MAX_POINT_LIGHTS];
	u32	point_light_count;

	float4	cam_pos;			// For view dependent effects
	float	exposure;			// for HDR rendering
	float	gamma;				// gamma correction
};


// --- Push Constants ---

#define SHADOW_MAP_CASCADE_COUNT	4
SHARED_STRUCT(PBRRootConstants)
{
	BDA_PTR(UniformBufferObject)	ubo_addr;
	BDA_PTR(EntityInstanceData)	instance_addr;
	BDA_PTR(Vertex)			vertex_addr;

	u32	base_color_tex;			// texture coordinate set for base color
	u32	metallic_roughness_tex;		// texture coordinate set for metallic-roughness
	u32	normal_tex;			// texture coordinate set for normal map
	u32	occlusion_tex;			// texture coordinate set for occlusion
	u32	emissive_tex;			// texture coordinate set for emission

	u32	shadow_cascades[SHADOW_MAP_CASCADE_COUNT];

	float4	base_color_factor;			// rgb base color and alpha
	float	metallic_factor;			// how metallic the surface is
	float	roughness_factor;			// how rough the surface is
	float	alpha_mask;				// whether to use alpha masking
	float	alpha_cutoff;			// alpha threshold for masking
};

SHARED_STRUCT(ShadowRootConstants)
{
	BDA_PTR(UniformBufferObject)	ubo_addr;
	BDA_PTR(EntityInstanceData)	instance_addr;
	BDA_PTR(Vertex)			vertex_addr;
	u32 cascade_index;
};

SHARED_STRUCT(UiRenderInstance)
{
	float2	pos;
	float2	size;
	float2	uv_offset;
	float2	uv_size;
	float4	color;
	float4	inner_color;
	u32	primitive_type;
	f32	corner_radius;
	f32	stroke_width;		// 0.0 = Solid Fill, >0.0 = Outline thickness (pixels)
	float4	clip_rect;
};

SHARED_STRUCT(TextRootConstants)
{
	BDA_PTR(UiRenderInstance)	instance_addr;   // Replaces vkCmdBindVertexBuffers for UI instances
	float2			window_size;
	float2			atlas_size;
	float			px_range;
	u32			atlas_tex_idx;   // Replaces atlas descriptor set
};

SHARED_STRUCT(GridProperties)
{
	float grid_size;		// spacing between minor lines, e.g. 1.0
	float line_width;	// in world units, e.g. 0.02
	float major_line_every;	// e.g. every 10th line is "major" (thicker/brighter)
	float fade_distance;	// distance at which grid fully fades out
};

SHARED_STRUCT(GridRootConstants)
{
	BDA_PTR(UniformBufferObject)	ubo_addr;
	GridProperties			properties;
};

#endif // SHARED_STRUCTS_H
