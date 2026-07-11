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
#include "logs.h"
#include "base_layer.h"

#define TINYGLTF3_ENABLE_FS
#include "tiny_gltf_v3.h"

typedef struct UniformBufferObject
{
	mat4	model;
	mat4	view;
	mat4	proj;

	vec4	light_positions[4];		// Pos and radius
	vec4	light_colors[4];		// Color and intensity
	vec4	cam_pos;			// For view dependent effects
	float	exposure;			// for HDR rendering
	float	gamma;				// gamma correction
	float	prefiltered_cube_miplevels;	// for image based lighting
	float	scale_ibl_ambient;		// Scale factor for ambient light
}	UniformBufferObject;

typedef struct Texture
{
	// TODO: Not a good identifier outside of debug, find another way
	String	name;

	VkImageView	image_view;
	VkSampler	sampler;

	VkImage		gpu_image;
	void		*gpu_image_alloc;
}	Texture;

typedef struct Material
{
	Texture		base_color_texture;
	Texture		metallic_roughness_texture;
	Texture		normal_texture;
	Texture		occlusion_texture;
	Texture		emissive_texture;

	float		base_color_factor[4];
	float		roughness_factor;
	float		metallic_factor;
	float		emissive_factor[3];
	float		alpha_cutoff;

	VkDescriptorSet	descriptor_set;
}	Material;

typedef struct
{
	vec3	pos;
	vec3	normal;
	vec2	uv;
	vec4	tangent;
}	Vertex;

typedef struct Mesh
{
	// -1 if absent
	i32		material_index;
	Vertex		*vertices;
	u32		vertex_count;
	void		*indices;
	u32		index_count;
	VkIndexType	index_type;

	VkBuffer	gpu_vertex_data;
	void		*gpu_vertex_alloc;
	VkBuffer	gpu_index_data;
	void		*gpu_index_alloc;
}	Mesh;

typedef struct Node
{
	// TODO: I dont know if this should stay
	String	name;

	Mesh	mesh;
	u32	index;

	double	rotation[4];     /* Default: {0,0,0,1} */
	double	scale[3];        /* Default: {1,1,1} */
	double	translation[3];  /* Default: {0,0,0} */

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
	tg3_error_stack	errors;
	Texture		*textures;	u32	texture_count;
	Material	*materials;	u32	material_count;
	Node		*linear_nodes;	u32	node_count;
	Mesh		*meshes;	u32	mesh_count;
	Animation	*animations;	u32	animation_count;

	UniformBufferObject	ubo;
}	GLTFModel;

typedef struct MaterialProperties
{
	vec4	base_color_factor;			// rgb base color and alpha
	float	metallic_factor;			// how metallic the surface is
	float	roughness_factor;			// how rough the surface is
	i32	basecolor_texture_set;			// texture coordinate set for base color
	i32	physical_descriptor_texture_set;	// texture coordinate set for metallic-roughness
	i32	normal_texture_set;			// texture coordinate set for normal map
	i32	occlusion_texture_set;			// texture coordinate set for occlusion
	i32	emissive_texture_set;			// texture coordinate set for emission
	float	alpha_mask;				// whether to use alpha masking
	float	alpha_mask_cut_off;			// alpha threshold for masking
}	MaterialProperties;

typedef struct FrameResources
{
	VkCommandPool	command_pool;
	VkCommandBuffer	command_buffer;
	VkSemaphore	image_acquired_semaphore;
}	FrameResources;

typedef struct PipelinePushConstants
{
	u32			push_constant_count;
	VkPushConstantRange	*push_constant_ranges;
	u32			*push_constant_offsets;
}	PipelinePushConstants;

typedef struct GraphicsContext
{
	i32			window_width;
	i32			window_height;
	SDL_Window		*window;

	u32			required_extension_count;
	const char		*const *required_extensions;
	u32			required_layer_count;
	const char		*const *required_layers;
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
	VkImage			swapchain_depth_image;
	VkImageView		swapchain_depth_image_view;
	void			*swapchain_depth_image_allocation;
	VkSemaphore		*render_complete_sequence_semaphores;
	u32			swapchain_width;
	u32			swapchain_height;
	bool			swapchain_require_recreate;

	VkShaderModule		vertex_shader;
	VkShaderModule		frag_shader;

	PipelinePushConstants	pipeline_push_constants;

	VkPipelineLayout	pipeline_layout;
	VkPipeline		pipeline;

	u32			frames_in_flight_count;
	VkSemaphore		timeline_semaphore;
	FrameResources		*frame_resources;

	u32			frame_index;
	u64			next_signal_value;

	VkDescriptorSetLayout	ubo_descriptor_layout;
	VkBuffer		uniform_buffers[MAX_FRAMES_IN_FLIGHT];
	void			*uniform_buffers_mapped[MAX_FRAMES_IN_FLIGHT];
	void			*uniform_buffer_allocations[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorPool	descriptor_pool;
	VkDescriptorSet		ubo_descriptor_sets[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorSetLayout	material_descriptor_layout;
	VkDescriptorSet		material_descriptor_sets;

	GLTFModel		model;

	Texture			default_base_color_texture;
	Texture			default_metallic_texture;
	Texture			default_normal_texture;
	Texture			default_occlusion_texture;
	Texture			default_emissive_texture;

}	GraphicsContext;


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
	float zoom;             // Field of view control for perspective projection

}	Camera;

void immediate_submit(GraphicsContext *ctx, void (*fn)(VkCommandBuffer cmd, void *data), void *data);
void	startGraphics(GraphicsContext *ctx);
void	endGraphics(GraphicsContext *ctx);
void	render(GraphicsContext *ctx, Camera *world);


void	gltfLoad(String filename, GLTFModel *model, GraphicsContext *ctx);
void	gltf_destroy(GLTFModel model);
void	createDefaultTextures(GraphicsContext *ctx);
void	recreatePipeline(void *user_data);
