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

#define MAX_PIPELINES	128
#define MAX_SHADERS	64

enum PipelineType {
	PIPELINE_TYPE_GRAPHICS = 0,
	PIPELINE_TYPE_COMPUTE  = 1
};

// A lightweight handle to pass around the engine instead of Vulkan pointers.
typedef u8	PipelineHandle;
typedef u16	ShaderHandle;
typedef u64	PipelineState;

// Shifts for the bitmask
#define SHIFT_SHADER	0	// 16bit	(Bits 0-15)
#define SHIFT_CULL	16	// 3bit		(Bits 16-18)
#define SHIFT_BLEND	20	// 3bit		(Bits 20-22)
#define SHIFT_DEPTH	24	// 3bit		(Bits 24-26)
#define SHIFT_TOPO	28	// 3bit		(Bits 28-30)
#define SHIFT_TYPE	31	// 1bit		(Bit 31) 0 = Graphics | 1 = Compute
#define SHIFT_PASS	32	// 3bit		(Bits 32-34)
#define SHIFT_DYNAMIC	35	// 1bit		(Bit 35) 0 = Depth bias disabled | 1 = enabled
#define SHIFT_COMPARE	36	// 3bit		(Bit 36-39)

// ---------------------------------------------------------
// RENDER STATE ENUMS
// ---------------------------------------------------------
enum PassType {
	PASS_MAIN = 0,		// Color + Depth
	PASS_SHADOW,		// Depth Only
	PASS_UI			// Color Only (No Depth)
};

enum CullMode {
	CULL_NONE = 0,
	CULL_BACK,
	CULL_FRONT
};

enum BlendMode {
	BLEND_NONE = 0,
	BLEND_ALPHA,
	BLEND_ADDITIVE
};

enum DepthMode {
	DEPTH_NONE = 0,
	DEPTH_READ,
	DEPTH_RW		// Read and Write
};

enum TopologyMode {
	TOPO_TRIANGLES = 0,
	TOPO_TRIANGLE_STRIP,
	TOPO_LINES,
	TOPO_POINTS
};

enum DynamicMode {
	DYNAMIC_DEPTH_DISABLE = 0,
	DYNAMIC_DEPTH_ENABLE
};

enum CompareMode {
	COMPARE_LESS = 0,
	COMPARE_LESS_OR_EQUAL,
	COMPARE_GREATER,
	COMPARE_GREATER_OR_EQUAL,
	COMPARE_EQUAL,
	COMPARE_ALWAYS
};

// Helper to pack the state
static inline PipelineState createPipelineState(u16 shader, u8 cull, u8 blend, u8 depth, u8 compare, u8 topo, u8 type, u8 pass) {
	return	((u64)shader	<< SHIFT_SHADER)   |
		((u64)cull	<< SHIFT_CULL)     |
		((u64)blend	<< SHIFT_BLEND)    |
		((u64)depth	<< SHIFT_DEPTH)    |
		((u64)topo	<< SHIFT_TOPO)     |
		((u64)type	<< SHIFT_TYPE)     |
		((u64)pass	<< SHIFT_PASS)     |
		((u64)compare	<< SHIFT_COMPARE);
}

// All settings off
static inline PipelineState createComputeState(u16 shader) {
	return	((u64)shader			<< SHIFT_SHADER) |
		((u64)PIPELINE_TYPE_COMPUTE	<< SHIFT_TYPE);
}

#define MAX_SHADER_STAGES 2
typedef struct {
	String					filename;
	u64					last_modified_time;

	u32					stage_count;
	char					pNames[MAX_SHADER_STAGES][12];
	VkPipelineShaderStageCreateInfo		stages[MAX_SHADER_STAGES];
}	ShaderRecord;

typedef struct {
	// Vulkan context
	VkDevice			device;
	VkPipelineLayout		g_layout;
	VkFormat			main_color_format;
	VkFormat			main_depth_format;
	VkFormat			shadow_depth_format;

	// Shader Base Code Storage
	ShaderRecord			shaders[MAX_SHADERS];
	u32				shader_count;

	PipelineState			cached_states[MAX_PIPELINES];
	VkPipeline			cached_pipelines[MAX_PIPELINES];
	u32				pipeline_count;
}	PipelineRegistry;

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
	uint2			viewport_extent;
	int2			viewport_offset;
	bool			swapchain_require_recreate;

	VkCommandPool		single_time_pool;

	VkDescriptorSetLayout	global_descriptor_layout; // Binding 0: Textures[], Binding 1: Samplers[]
	VkDescriptorSetLayout	shadow_descriptor_layout;
	VkDescriptorPool	global_descriptor_pool;   // Allocated once for MAX_TEXTURES
	VkDescriptorSet		global_descriptor_set;    // Bound ONCE at the start of every frame

	PipelineRegistry	*pipeline_registry;

	// Texture Heap Index Allocators
	u32			next_free_texture_index;

	// Sampler handles (only for destroying later)
	VkSampler		default_pbr_sampler;
	VkSampler		ui_sampler;

	// Shadow Pipeline ------------
	CascadedShadowMap	shadow_maps[MAX_FRAMES_IN_FLIGHT];
	VkSampler		shadow_sampler;
	ShaderHandle		shadow_shader_handle;
	u64			shadow_pipeline_state;
	// ---------------------

	// PBR Pipeline ------------
	ShaderHandle		pbr_shader_handle;

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
	ShaderHandle		grid_shader_handle;
	u64			grid_pipeline_state;
	GridProperties		grid_properties;
	// --------------------------

	// Text Pipeline ------------
	ShaderHandle		ui_shader_handle;
	u64			ui_pipeline_state;
	u32			font_atlas_global_index;
	// --------------------------

	u32			frames_in_flight_count;
	VkSemaphore		timeline_semaphore;
	FrameResources		*frame_resources;

	u32			frame_index;
	u64			next_signal_value;

}	GraphicsContext;

// TODO: Test test test, how many instances are needed? Am i gonna use particles like this later? (2026-07-23)
#define	MAX_INSTANCES	4096
typedef struct EntityRenderData
{
	i16	model_idx;
	// TODO: See if this is is needed in the entity struct itself or if i can build it
	// in buildEntityRenderInfo
	u64	pipeline_state;
	vec3	pos;
	versor	rotation;
	f32	scale;
}	EntityRenderData;

typedef struct EntityRenderInfo
{
	EntityRenderData	*data;
	u32			entity_count;
}	EntityRenderInfo;

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
	assert(ctx->next_free_texture_index < BINDLESS_TEXTURE_COUNT);
	return ctx->next_free_texture_index++;
}

// --- MODEL CACHE --- //
#define UNLOAD_TIME	3 
#define MAX_MODELS	(1L << 12)
#define HASH_TABLE_SIZE	(MAX_MODELS << 1)
#define HASH_EMPTY	0x0000000000000000ull
#define HASH_TOMBSTONE	0xFFFFFFFFFFFFFFFFull
#define UNLOAD_DELAY_MS	10000	// 10 seconds

typedef u16	ModelHandle;

typedef struct ModelHashEntry
{
	ModelHandle	handle;
	u64		hash;
}	ModelHashEntry;

typedef struct ModelCache
{
	// Path -> Handle
	ModelHashEntry		hash_table[HASH_TABLE_SIZE];

	// Data (Indexed by handle)
	Model		*models[MAX_MODELS];
	String32	paths[MAX_MODELS];
	u32		ref_counts[MAX_MODELS];
	u16		time_to_unload[MAX_MODELS]; // in ms

	// Recycling
	ModelHandle	free_handles[MAX_MODELS];
	u32		free_count;
}	ModelCache;

void		initModelCache(ModelCache *cache);
u32		modelCacheGetCount(ModelCache *cache);
void		modelCacheRelease(ModelCache *cache, ModelHandle handle);
void		modelCacheTick(ModelCache *cache, u16 delta_ms);
ModelHandle	modelCacheAcquire(ModelCache *cache, GraphicsContext *ctx, String model_path);

// ------------------- //

void	immediate_submit(GraphicsContext *ctx, void (*fn)(VkCommandBuffer cmd, void *data), void *data);
void	startGraphics(GraphicsContext *ctx);
void	endGraphics(GraphicsContext *ctx);
void	render(GraphicsContext *ctx, ModelCache *cache, Camera *camera, EntityRenderInfo entity_info, UiRenderInfo text_info);
void	beginSingleTimeCommand(GraphicsContext *ctx, VkCommandBuffer *cmd_buffer);
void	stagingBufferUpload(GraphicsContext *ctx, u32 img_w, u32 img_h, u32 data_size, void *data_for_upload, ImageObject *gpu_image);
void	createVkBuffer(GraphicsContext *ctx, const BufferInfo *info, BufferObject *out_buffer);


void	modelLoad(String filename, GraphicsContext *ctx, Model *model);
void	gltf_destroy(Model model);
void	createDefaultTextures(GraphicsContext *ctx);

void		initPipelineRegistry(PipelineRegistry **reg_out, GraphicsContext *ctx);
VkPipeline	pipelineRegistryGetOrCreate(PipelineRegistry *registry, PipelineState state);
