#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "vulkan_inner.h"
#include <volk.h>

#include <shaderc/shaderc.h>

#define MAX_FRAMES_IN_FLIGHT	2

#include "typedefs.h"
#include "base_layer.h"

#define TINYGLTF3_ENABLE_FS
#include "tiny_gltf_v3.h"

#include "shared_structs.h"

typedef struct BufferObject
{
	VkBuffer	handle;
	VkDeviceSize	capacity;
	void		*allocation;
	u64		device_address;

	// Optional
	void		*mapped;
}	BufferObject;

typedef struct
{
	VkDeviceSize		size;
	VkBufferUsageFlags	usage;
	bool			cpu_accessible;
}	BufferInfo;

// Optional struct for specific mip/layer ranges
typedef struct TransitionRange
{
	u32	baseMipLevel;
	u32	levelCount;
	u32	baseArrayLayer;
	u32	layerCount;
}	TransitionRange;

typedef struct ImageTransitionInfo
{
	VkImage			image;
	VkImageLayout		oldLayout;
	VkImageLayout		newLayout;
	VkPipelineStageFlags2	srcStageMask;
	VkAccessFlags2		srcAccessMask;
	VkPipelineStageFlags2	dstStageMask;
	VkAccessFlags2		dstAccessMask;
	VkImageAspectFlags	aspectMask;
    
	const TransitionRange* pRange; // Set to NULL to transition the entire image
}	ImageTransitionInfo;

typedef struct TextPushConstants
{
	vec2	screen_size;
	vec2	atlas_size;
	float	px_range;
}	TextPushConstants;

typedef struct ImageObject
{
	VkImageView	view;
	VkImage		image;
	void		*allocation;
}	ImageObject;

typedef struct Texture
{
	// TODO: Not a good identifier outside of debug, find another way
	String	name;

	VkSampler	sampler;
	ImageObject	gpu_image;

	u32		global_index;

}	Texture;

typedef struct Material
{
	u32		base_color_tex_idx;
	u32		metallic_roughness_tex_idx;
	u32		normal_tex_idx;
	u32		occlusion_tex_idx;
	u32		emissive_tex_idx;

	float		base_color_factor[4];
	float		roughness_factor;
	float		metallic_factor;
	float		emissive_factor[3];
	float		alpha_cutoff;
}	Material;

typedef struct Mesh
{
	// -1 if absent
	i32		material_index;
	Vertex		*vertices;
	u32		vertex_count;
	void		*indices;
	u32		index_count;
	VkIndexType	index_type;

	BufferObject	gpu_vertex_data;
	BufferObject	gpu_index_data;
}	Mesh;

typedef struct Node
{
	// TODO: I dont know if this should stay
	String	name;

	Mesh	*meshes;
	u32	mesh_count;
	u32	index;

	float	rotation[4];     /* Default: {0,0,0,1} */
	float	scale[3];        /* Default: {1,1,1} */
	float	translation[3];  /* Default: {0,0,0} */

	i32	*children;
	u32	children_count;

	i32	parent;

	mat4	local_transform;
	mat4	world_transform;

}	Node;

enum	AnimationChannelTargetPath
{
	TRANSLATION,
	ROTATION,
	SCALE,
	WEIGHTS
};

typedef struct AnimationChannelTarget
{
	i32				node;    /* -1 if absent */
	enum AnimationChannelTargetPath	path;
}	AnimationChannelTarget;

typedef struct AnimationChannel
{
	i32			sampler; /* Required */
	AnimationChannelTarget	target;
}	AnimationChannel;

enum	SamplerInterpolationType
{
	LINEAR,
	STEP,
	CUBICSPLINE
};

typedef struct AnimationSampler
{
	i32				input;
	i32				output;
	enum SamplerInterpolationType	interpolation;
}	AnimationSampler;


typedef struct Animation
{
	String			name;

	AnimationChannel	*channels;
	u32			channels_count;
	AnimationSampler	*samplers;
	u32			samplers_count;
}	Animation;

// TODO: See if i can cut down this struct a bit
typedef struct GLTFModel
{
	// Model owns its own memory
	Allocator	arena;
	String		path;

	Texture		*textures;	u32	texture_count;
	Material	*materials;	u32	material_count;
	Node		*linear_nodes;	u32	node_count;
	Mesh		*meshes;	u32	mesh_count;
	Animation	*animations;	u32	animation_count;
}	Model;

typedef struct ModelCacheEntry
{
	Model	*model;
	String		path;

	// Unloading logic
	u32		ref_count;
	bool		pending_unload;
	u32		frames_since_unload;
}	ModelCacheEntry;

typedef struct ModelCache
{
	Allocator	pool;
}	ModelCache;

typedef struct FrameResources
{
	VkCommandPool	cmd_pool;
	VkCommandBuffer	cmd_buf;
	VkSemaphore	image_acquired_semaphore;

	BufferObject	uniform_buffer;

	BufferObject	instance_buffer;

	BufferObject	text_instance_buffer;

}	FrameResources;

#define SHADOW_MAP_RESOLUTION		2048
typedef struct CascadedShadowMap
{
	ImageObject	image;
	VkImageView	cascade_views[SHADOW_MAP_CASCADE_COUNT]; // Individual layer views
	VkDescriptorSet	descriptor_set;
}	CascadedShadowMap;

#define BINDLESS_TEXTURE_COUNT 8192
typedef struct GraphicsContext
{
	i32			window_width;
	i32			window_height;
	SDL_Window		*window;

	u32			required_extension_count;
	const			char      *const *required_extensions;
	u32			required_layer_count;
	const			char      *const *required_layers;
	VkInstance		vk_instance;

	VkSurfaceKHR		surface;

	VkPhysicalDevice	phys_device;
	VkDevice		device;
	VkQueue			queue;
	u32			queue_family_index;

	void			*vma_allocator;

	VkSwapchainKHR		swapchain;
	VkSurfaceFormatKHR	swapchain_format;
	VkPresentModeKHR	swapchain_present_mode;
	u32			image_count;
	VkImage			*swapchain_images;
	VkImageView		*swapchain_image_views;
	VkFormat		swapchain_depth_format;
	ImageObject		swapchain_depth_image;
	VkSemaphore		*render_complete_sequence_semaphores;
	u32			swapchain_width;
	u32			swapchain_height;
	bool			swapchain_require_recreate;

	VkCommandPool		single_time_pool;

	VkDescriptorSetLayout	global_descriptor_layout; // Binding 0: Textures[], Binding 1: Samplers[]
	VkDescriptorSetLayout	shadow_descriptor_layout;
	VkDescriptorPool	global_descriptor_pool;   // Allocated once for MAX_TEXTURES
	VkDescriptorSet		global_descriptor_set;    // Bound ONCE at the start of every frame
	VkPipelineLayout	global_pipeline_layout;   // Shared across ALL pipelines in the engine

	// Texture Heap Index Allocators
	u32			next_free_texture_index;

	// Sampler handles (only for destroying later)
	VkSampler		default_pbr_sampler;
	VkSampler		ui_sampler;

	// Shadow Pipeline ------------
	CascadedShadowMap	shadow_maps[MAX_FRAMES_IN_FLIGHT];
	VkSampler		shadow_sampler;
	VkPipeline		pipeline_shadow;
	// ---------------------

	// PBR Pipeline ------------
	VkPipeline		pipeline_pbr;

	Texture			default_base_color_texture;
	Texture			default_metallic_texture;
	Texture			default_normal_texture;
	Texture			default_occlusion_texture;
	Texture			default_emissive_texture;
	// -------------------------

	// Transparent Pipeline ------------
	// VkPipeline			pipeline_transparent;
	// ---------------------------------

	// Grid Pipeline ------------
	VkPipeline		pipeline_grid;
	GridProperties		grid_properties;
	// --------------------------

	// Text Pipeline ------------
	VkPipeline		pipeline_text;
	u32			font_atlas_global_index;
	// --------------------------

	u32			frames_in_flight_count;
	VkSemaphore		timeline_semaphore;
	FrameResources		*frame_resources;

	u32			frame_index;
	u64			next_signal_value;

} GraphicsContext;

// TODO: Test test test, how many instances are needed? Am i gonna use particles like this later? (2026-07-23)
#define	MAX_INSTANCES	4096
typedef struct EntityRenderData
{
	EntityInstanceData	instance_data;
	i16		model_idx;
}	EntityRenderData;

typedef struct EntityRenderInfo
{
	Model			**models;	i16	model_count;
	EntityRenderData	*data;		u32	entity_count;

}	EntityRenderInfo;

// TODO: Change all colors to u32
#define MAX_GLYPH_INSTANCES 1024

typedef struct UiRenderInfo
{
	vec2			atlas_size;
	f32			px_range;
	u64			upload_size;
	u32			instance_count;
	UiRenderInstance	*render_instances;
}	UiRenderInfo;

enum CameraMovement {
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT,
	UP,
	DOWN
};

typedef struct Camera
{
	// Spatial positioning and orientation vectors
	// These form the camera's local coordinate system in world space
	vec3 position;     // Camera's location in world coordinates
	vec3 front;        // Forward direction (where camera is looking)
	vec3 up;           // Camera's local up direction (for roll control)
	vec3 right;        // Camera's local right direction (perpendicular to front and up)
	vec3 worldUp;      // Global up vector reference (typically Y-axis)

	// Rotation representation using Euler angles
	// Provides intuitive control while managing gimbal lock and other rotation complexities
	float yaw;              // Horizontal rotation around the world up-axis (left-right looking)
	float pitch;            // Vertical rotation around the camera's right axis (up-down looking)

	// User interaction and behavior parameters
	// These control how the camera responds to input and environmental factors
	float movementSpeed;    // Units per second for translation movement
	float mouseSensitivity; // Multiplier for mouse input to rotation angle conversion
	float fov;             // Field of view control for perspective projection

	float	near_z;
	float	far_z;

}	Camera;


static inline u32	allocateBindlessTexture(GraphicsContext *ctx) 
{
	if (ctx->next_free_texture_index >= BINDLESS_TEXTURE_COUNT) {
		engine_error(LOG_FILE, "Bindless texture heap exhausted!\n");
		exit(1);
	}
	return ctx->next_free_texture_index++;
}

void	immediate_submit(GraphicsContext *ctx, void (*fn)(VkCommandBuffer cmd, void *data), void *data);
void	startGraphics(GraphicsContext *ctx);
void	endGraphics(GraphicsContext *ctx);
void	render(GraphicsContext *ctx, Camera *camera, EntityRenderInfo entity_info, UiRenderInfo text_info);
void	beginSingleTimeCommand(GraphicsContext *ctx, VkCommandBuffer *cmd_buffer);
void	stagingBufferUpload(GraphicsContext *ctx, u32 img_w, u32 img_h, u32 data_size, void *data_for_upload, ImageObject *gpu_image);
void	createVkBuffer(GraphicsContext *ctx, const BufferInfo *info, BufferObject *out_buffer);


void	modelLoad(String filename, GraphicsContext *ctx, Model *model);
void	gltf_destroy(Model model);
void	createDefaultTextures(GraphicsContext *ctx);

void	initModelCache(void);
Model	*modelCacheAcquire(GraphicsContext *ctx, String path);
void	modelCacheRelease(Model *model);
void	modelCacheSweep();
u32	getModelCountFromCache();
