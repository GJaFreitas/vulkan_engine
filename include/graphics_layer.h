#pragma once
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "vulkan_inner.h"
#include <volk.h>

#include <shaderc/shaderc.h>

#define MAX_FRAMES_IN_FLIGHT	2

#include "typedefs.h"

typedef struct
{
	vec3	pos;
	vec3	normal;
	vec2	uv;
}	Vertex;

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
}	GraphicsContext;


void	startGraphics(GraphicsContext *ctx);
void	endGraphics(GraphicsContext *ctx);
void	render(GraphicsContext *ctx);
