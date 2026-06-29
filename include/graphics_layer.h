#pragma once
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cglm/cglm.h>

#include "vulkan_inner.h"
#include <volk.h>

#include <shaderc/shaderc.h>

#define MAX_FRAMES_IN_FLIGHT	2

#include "typedefs.h"
#include "logs.h"
#include "base_layer.h"

#define TINYGLTF3_ENABLE_FS
#include "tiny_gltf_v3.h"

typedef struct ToyVertex
{
	vec2	pos;
	vec3	color;
}	ToyVertex;

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
	Texture		normal_texture;
	Texture		occlusion_texture;
	Texture		emissive_texture;
	double		base_color_factor[4];
	double		roughness_factor;
	double		metallic_factor;
	VkDescriptorSet	descriptor_set;
}	Material;

typedef struct
{
	vec3	pos;
	vec3	normal;
	vec2	uv;
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

typedef struct GLTFModel
{
	tg3_error_stack	errors;
	Texture		*textures;	u32	texture_count;
	Material	*materials;	u32	material_count;
	Node		*linear_nodes;	u32	node_count;
	Mesh		*meshes;	u32	mesh_count;
	Animation	*animations;	u32	animation_count;
}	GLTFModel;

typedef struct UniformBufferObject
{
	mat4	model;
	mat4	view;
	mat4	proj;
}	UniformBufferObject;

typedef struct FrameResources
{
	VkCommandPool	command_pool;
	VkCommandBuffer	command_buffer;
	VkSemaphore	image_acquired_semaphore;
}	FrameResources;

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

	VkPipelineLayout	pipeline_layout;
	VkPipeline		pipeline;

	u32			frames_in_flight_count;
	VkSemaphore		timeline_semaphore;
	FrameResources		*frame_resources;

	u32			frame_index;
	u64			next_signal_value;


	u32			vertex_count;
	VkBuffer		vertex_buffer;
	u32			index_count;
	VkBuffer		index_buffer;
	void			*vertex_buffer_allocation;
	void			*index_buffer_allocation;

}	GraphicsContext;


void immediate_submit(GraphicsContext *ctx, void (*fn)(VkCommandBuffer cmd, void *data), void *data);
void	startGraphics(GraphicsContext *ctx);
void	endGraphics(GraphicsContext *ctx);
void	render(GraphicsContext *ctx);


void	gltf_load(String filename, GLTFModel *model, GraphicsContext *ctx);
void	gltf_destroy(GLTFModel model);
