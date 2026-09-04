#include "vulkan_inner.h"
#include "world.h"
#include "ui.h"
#include "graphics_layer.h"
#include "fonts.h"
#include <stdint.h>

// --- VULKAN API --- //

void	cmdTransitionImage(VkCommandBuffer cmd, const ImageTransitionInfo *info)
{
	// Default to the entire image
	VkImageSubresourceRange subresource = {
		.aspectMask = info->aspectMask,
		.baseMipLevel = 0,
		.levelCount = VK_REMAINING_MIP_LEVELS,
		.baseArrayLayer = 0,
		.layerCount = VK_REMAINING_ARRAY_LAYERS
	};

	// Override if a specific range was provided
	if (info->pRange != NULL) {
		subresource.baseMipLevel = info->pRange->baseMipLevel;
		subresource.levelCount = info->pRange->levelCount;
		subresource.baseArrayLayer = info->pRange->baseArrayLayer;
		subresource.layerCount = info->pRange->layerCount;
	}

	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.oldLayout = info->oldLayout,
		.newLayout = info->newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = info->image,
		.subresourceRange = subresource,
		.srcStageMask = info->srcStageMask,
		.srcAccessMask = info->srcAccessMask,
		.dstStageMask = info->dstStageMask,
		.dstAccessMask = info->dstAccessMask
	};

	VkDependencyInfo dep_info = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &barrier
	};

	vkCmdPipelineBarrier2(cmd, &dep_info);
}

static inline void	createMappedBuffer(void *allocator, VkBufferCreateInfo *buf_info, BufferObject *buf)
{
	wrapperVMAcreateBuffer(allocator, buf_info, &buf->handle, &buf->allocation, 1);
	wrapperVMAmapMemory(allocator, buf->allocation, &buf->mapped);
}

void	stagingBufferUpload(GraphicsContext *ctx, u32 img_w, u32 img_h, u32 data_size, void *data_for_upload, ImageObject *gpu_image)
{
	VkBuffer	staging_buffer;
	void		*buf_allocation;

	VkBufferCreateInfo	buf_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = data_size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	wrapperVMAcreateBuffer(ctx->vma_allocator, &buf_info, &staging_buffer, &buf_allocation, 1);

	void	*mapped;
	wrapperVMAmapMemory(ctx->vma_allocator, buf_allocation, &mapped);
	memcpy(mapped, data_for_upload, data_size);
	wrapperVMAunmapMemory(ctx->vma_allocator, buf_allocation);
	// ------------

	{
		VkCommandBuffer	cmd;
		beginSingleTimeCommand(ctx, &cmd);

		ImageTransitionInfo	img_undef_transf = {
			.image = gpu_image->image,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.pRange = NULL,
		};
		cmdTransitionImage(cmd, &img_undef_transf);

		VkBufferImageCopy2	region = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageExtent = {img_w, img_h, 1},
			.imageOffset = {0, 0, 0},
			.imageSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		VkCopyBufferToImageInfo2	copy_info = {
			.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
			.srcBuffer = staging_buffer,
			.dstImage = gpu_image->image,
			.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.regionCount = 1,
			.pRegions = &region
		};
		vkCmdCopyBufferToImage2(cmd, &copy_info);

		ImageTransitionInfo	img_transf_shader = {
			.image = gpu_image->image,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.pRange = NULL,
		};
		cmdTransitionImage(cmd, &img_transf_shader);

		vkEndCommandBuffer(cmd);

		VkCommandBufferSubmitInfo	cmd_submit_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = cmd,
		};
		VkSubmitInfo2 submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &cmd_submit_info
		};
		vkQueueSubmit2(ctx->queue, 1, &submit_info, VK_NULL_HANDLE);
		vkQueueWaitIdle(ctx->queue);
		wrapperVMAdestroyBuffer(ctx->vma_allocator, staging_buffer, buf_allocation);

		vkResetCommandPool(ctx->device, ctx->single_time_pool, 0);
	}

}

void	beginSingleTimeCommand(GraphicsContext *ctx, VkCommandBuffer *cmd_buffer)
{
	VkCommandBufferAllocateInfo	alloc_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = ctx->single_time_pool,
		.commandBufferCount = 1,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	};

	vkAllocateCommandBuffers(ctx->device, &alloc_info, cmd_buffer);

	VkCommandBufferBeginInfo	cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(*cmd_buffer, &cmd_begin_info);
}


// --- VULKAN CODE --- //


VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_messenger_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
	void *user_data)
{
	(void)user_data;
	(void)message_type;
	if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		engine_error(LOG_FILE, "%i - %s: %s\n", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
	}
	return VK_FALSE;
}

static void	createInstance(GraphicsContext *ctx)
{
	if (volkInitialize() != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to initialized volk\n");
		exit(1);
	}

	VkApplicationInfo appInfo = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Vulkan triangle",
		.apiVersion = VK_API_VERSION_1_4
	};

	u32	extensionCount = 0;
	const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);

	const char **extensions = malloc(sizeof(char *) * (extensionCount + ctx->required_extension_count));
	engine_log(LOG_FILE, "Extensions ---------------");
	for (u32 i = 0; i < extensionCount + ctx->required_extension_count; i++) {
		if (i < extensionCount) {
			extensions[i] = strdup(sdl_extensions[i]);
		} else {
			extensions[i] = strdup(ctx->required_extensions[i - extensionCount]);
		}
		engine_log(LOG_FILE, "%s", extensions[i]);
	}
	engine_log(LOG_FILE, "Extensions ---------------");

	VkDebugUtilsMessengerCreateInfoEXT debugInfo = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = \
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = \
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debug_utils_messenger_callback
	};

	VkInstanceCreateInfo instanceInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = &debugInfo,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = ctx->required_layer_count,
		.ppEnabledLayerNames = ctx->required_layers,
		.enabledExtensionCount = ctx->required_extension_count + extensionCount,
		.ppEnabledExtensionNames = extensions,
	};

	if (vkCreateInstance(&instanceInfo, NULL, &ctx->vk_instance) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create vulkan instance\n");
		exit(1);
	}


	engine_log(LOG_FILE, "Successfully created vulkan instance");
}

static void	createSurface(GraphicsContext *ctx)
{
	engine_log(LOG_FILE, "SDL video driver: %s", SDL_GetCurrentVideoDriver());
	if (!SDL_Vulkan_CreateSurface(ctx->window, ctx->vk_instance, NULL, &ctx->surface)) {
		engine_error(LOG_FILE, "Failed to create surface, error: %s\n", SDL_GetError());
		exit(1);
	}
	engine_log(LOG_FILE, "Successfully created surface");
}

static VkPresentModeKHR chooseSwapPresentMode(VkPresentModeKHR *presentModes, u32 presentModesCount)
{
	for (u32 i = 0; i < presentModesCount; i++) {
		// This mode is "triple buffering" where we shove images down the
		// queue to be presented and if we can create them faster than
		// the monitor refreshes we just replace them. This is better than
		// VSync since it doenst tear the screen because its not trying to
		// render to a monitor that inst refreshing but doesnt have any latency
		// problems since we can keep rendering stuff and replacing the old frames
		if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			return presentModes[i];
		}
	}
	// This is the default VSync option that exists everywhere
	return VK_PRESENT_MODE_FIFO_KHR;
}

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(VkSurfaceFormatKHR *formats, u32 formatCount)
{
	for (u32 i = 0; i < formatCount; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB
			&& formats[i].colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR)
			return (formats[i]);
	}
	// We could rank them by which is the best but if the ideal isnt here we can just
	// use the first listed one
	return (formats[0]);
}

static void	createDevice(GraphicsContext *ctx)
{
	u32	phys_device_count = 0;
	vkEnumeratePhysicalDevices(ctx->vk_instance, &phys_device_count, NULL);
	VkPhysicalDevice	*phys_devices = malloc(sizeof(VkPhysicalDevice) * phys_device_count);
	vkEnumeratePhysicalDevices(ctx->vk_instance, &phys_device_count, phys_devices);

	// Non gaming laptop
	VkPhysicalDevice	chosen_phys_device = phys_devices[0];
	ctx->phys_device = chosen_phys_device;

	u32	format_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(chosen_phys_device, ctx->surface, &format_count, NULL);
	VkSurfaceFormatKHR	*formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(chosen_phys_device, ctx->surface, &format_count, formats);
	ctx->swapchain_format = chooseSwapSurfaceFormat(formats, format_count);

	u32	present_mode_count = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(chosen_phys_device, ctx->surface, &present_mode_count, NULL);
	VkPresentModeKHR	*present_modes = malloc(sizeof(VkPresentModeKHR) * present_mode_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(chosen_phys_device, ctx->surface, &present_mode_count, present_modes);
	ctx->swapchain_present_mode = chooseSwapPresentMode(present_modes, present_mode_count);

	u32	queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(chosen_phys_device, &queue_family_count, NULL);
	VkQueueFamilyProperties2	*queue_family_properties = malloc(sizeof(VkQueueFamilyProperties2) * queue_family_count);
	memset(queue_family_properties, 0, sizeof(VkQueueFamilyProperties2) * queue_family_count);
	for (u32 i = 0; i < queue_family_count; i++) {
		queue_family_properties[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
	}
	vkGetPhysicalDeviceQueueFamilyProperties2(chosen_phys_device, &queue_family_count, queue_family_properties);


	u32	queue_family_index = 0;
	for (u32 i = 0; i < queue_family_count; i++) {
		VkBool32	has_present_support = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(chosen_phys_device, i, ctx->surface, &has_present_support);

		if (queue_family_properties[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && has_present_support) {
			queue_family_index = i;
			break ;
		}
	}
	ctx->queue_family_index = queue_family_index;

	// Create a linked list of features that we want and vulkan will transverse it and populate structs accordingly
	VkPhysicalDeviceVulkan14Features	supported_features14 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = NULL
	};
	VkPhysicalDeviceVulkan13Features	supported_features13 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &supported_features14
	};
	VkPhysicalDeviceVulkan12Features	supported_features12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &supported_features13
	};
	VkPhysicalDeviceVulkan11Features	supported_features11 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
		.pNext = &supported_features12
	};
	VkPhysicalDeviceFeatures2	supported_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &supported_features11
	};
	vkGetPhysicalDeviceFeatures2(chosen_phys_device, &supported_features);

	if (!supported_features13.dynamicRendering) {
		engine_error(LOG_FILE, "Card doesnt support dynamic rendering\n");
		exit(1);
	}
	if (!supported_features13.synchronization2) {
		engine_error(LOG_FILE, "Card doesnt support sync2\n");
		exit(1);
	}
	if (!supported_features12.timelineSemaphore) {
		engine_error(LOG_FILE, "Card doesnt support timeline semaphore\n");
		exit(1);
	}
	if (!supported_features11.shaderDrawParameters) {
		engine_error(LOG_FILE, "Card doesnt support shader draw parameters\n");
		exit(1);
	}

	// Features I turn on
	VkPhysicalDeviceVulkan14Features	features14 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = NULL,
	};
	VkPhysicalDeviceVulkan13Features	features13 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE,
		.pNext = &features14,
	};
	VkPhysicalDeviceVulkan12Features	features12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.timelineSemaphore = VK_TRUE,
		.pNext = &features13,
	};
	VkPhysicalDeviceVulkan11Features	features11 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
		.shaderDrawParameters = VK_TRUE,
		.pNext = &features12,
	};
	VkPhysicalDeviceFeatures2	features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features11,
	};

	float	q_priorities = 1.0f;
	VkDeviceQueueCreateInfo	queueInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = queue_family_index,
		.queueCount = 1,
		.pQueuePriorities = &q_priorities
	};

	const char	*device_extensions[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
	VkDeviceCreateInfo	device_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &features,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queueInfo,
		.enabledExtensionCount = sizeofarray(device_extensions),
		.ppEnabledExtensionNames = device_extensions,
		.pEnabledFeatures = NULL
	};

	if (vkCreateDevice(chosen_phys_device, &device_create_info, NULL, &ctx->device) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create logical device");
		exit(1);
	}

	engine_log(LOG_FILE, "Successfully created logical device");

	vkGetDeviceQueue(ctx->device, queue_family_index, 0, &ctx->queue);
}

static void	createSwapchain(GraphicsContext *ctx, u32 width, u32 height)
{
	ctx->swapchain_width = width;
	ctx->swapchain_height = height;

	VkSurfaceCapabilitiesKHR	surface_capabilities = {0};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->phys_device, ctx->surface, &surface_capabilities) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to get surface capabilities\n");
		exit(1);
	}

	u32	requested_img_count = 2;
	if (surface_capabilities.minImageCount > 2)
		requested_img_count = surface_capabilities.minImageCount;
	if (surface_capabilities.maxImageCount > 0 && requested_img_count > surface_capabilities.maxImageCount)
		requested_img_count = surface_capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR	swapchain_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = ctx->surface,
		.minImageCount = requested_img_count,
		.imageFormat = ctx->swapchain_format.format,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = surface_capabilities.currentExtent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = surface_capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = ctx->swapchain_present_mode
	};

	if (vkCreateSwapchainKHR(ctx->device, &swapchain_info, NULL, &ctx->swapchain) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create swapchain\n");
	}
	engine_log(LOG_FILE, "Successfully created swapchain");

	u32	image_count = 0;
	vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &image_count, NULL);
	VkImage	*images = malloc(sizeof(VkImage) * image_count);
	vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &image_count, images);
	VkImageView	*image_views = malloc(sizeof(VkImageView) * image_count);
	for (u32 i = 0; i < image_count; i++) {
		VkImageViewCreateInfo img_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = ctx->swapchain_format.format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		if (vkCreateImageView(ctx->device, &img_info, NULL, &image_views[i]) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to create image view nr: %u\n", i);
			exit(1);
		}
	}

	VkSemaphore	*semaphores = malloc(sizeof(VkSemaphore) * image_count);
	for (u32 i = 0; i < image_count; i++) {
		VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		if (vkCreateSemaphore(ctx->device, &semaphore_info, NULL, &semaphores[i]) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to create semaphore nr: %u\n", i);
			exit(1);
		}
	}

	// Z buffer creation
	ctx->swapchain_depth_format = VK_FORMAT_D32_SFLOAT;
	VkImageCreateInfo depth_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = ctx->swapchain_depth_format,
		.extent = {ctx->swapchain_width, ctx->swapchain_height, .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	if (wrapperVMAcreateImage(ctx->vma_allocator, &depth_info, &ctx->swapchain_depth_image.image, &ctx->swapchain_depth_image.allocation) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to allocate depth buffer\n");
		exit(1);
	}

	VkImageViewCreateInfo depth_view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = ctx->swapchain_depth_image.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = ctx->swapchain_depth_format,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};

	if (vkCreateImageView(ctx->device, &depth_view_info, NULL, &ctx->swapchain_depth_image.view) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create depth image view\n");
		exit(1);
	}

	engine_log(LOG_FILE, "Successfully created depth buffer");

	ctx->image_count = image_count;
	ctx->swapchain_images = images;
	ctx->swapchain_image_views = image_views;
	ctx->render_complete_sequence_semaphores = semaphores;
}

static VkShaderModule	createShaderModule(String filename, shaderc_shader_kind kind, GraphicsContext *ctx)
{

	StringView	extension = filename;

	// extension is at '.'
	strViewJumpToChar(&extension, '.');
	// extension after '.'
	strViewAdvance(&extension, 1);

	String shader_code = readFile(filename);
	engine_log(LOG_FILE, "Compiling shader: %S", filename);

	u64	shader_size;
	u32	*shader_data;

	// Shader is already compiled
	if (strEq(extension, STRING_LIT("spv"))) {
		shader_data = (u32 *)shader_code.data;
		shader_size = shader_code.count;
	} else {
		// TODO: Give this an allocator
		char	*filename_cstring = strToCstring(filename, NULL);

		shaderc_compiler_t		compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t	opts = shaderc_compile_options_initialize();
		shaderc_compile_options_set_target_env(opts, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
		shaderc_compile_options_set_target_spirv(opts, shaderc_spirv_version_1_6);
		shaderc_compile_options_set_optimization_level(opts, shaderc_optimization_level_performance);
		shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, (char *)shader_code.data, shader_code.count, kind, filename_cstring, "main", opts);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
			engine_error(LOG_FILE, "Shader compilation error: %s\n", shaderc_result_get_error_message(result));
			exit(1);
		}
		free(filename_cstring);

		shader_size = shaderc_result_get_length(result);
		shader_data = (u32 *)shaderc_result_get_bytes(result);
	}

	VkShaderModuleCreateInfo	shader_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shader_size,
		.pCode = shader_data,
	};
	VkShaderModule	module;
	if (vkCreateShaderModule(ctx->device, &shader_info, NULL, &module) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create shader module: %S\n", filename);
		exit(1);
	}
	return module;
}

static void	createShaders(GraphicsContext *ctx, StringView shader_name, PipelineObject *pipeline)
{
	u8	buf[128] = "shaders/compiled/";
	String	shader = { .data = buf, .count = 17 };
	// full shader path: shaders/compiled/

	memcpy(buf + shader.count, shader_name.data, shader_name.count);
	shader.count += shader_name.count;
	// full shader path: shaders/compiled/{shader_name}

	memcpy(buf + shader.count, ".spv", 4);
	shader.count += 4;
	// full shader path: shaders/compiled/{shader_name}.spv

	pipeline->vertex_shader = createShaderModule(shader, shaderc_vertex_shader, ctx);
	pipeline->frag_shader = pipeline->vertex_shader;
}


// --- PIPELINE CREATION --- //

static void	pipelineShaderCreate(GraphicsContext *ctx, String shader_name, PipelineObject *pipeline, VkShaderStageFlagBits shader_stages, u32 *_stage_count, VkPipelineShaderStageCreateInfo shader_info[2])
{
	createShaders(ctx, shader_name, pipeline);
	u32	stage_count = __builtin_popcount(shader_stages);
	*_stage_count = stage_count;

	shader_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shader_info[0].module = pipeline->vertex_shader;
	shader_info[0].pName = "vertMain";

	stage_count--;
	if (stage_count) {
		shader_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shader_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shader_info[1].module = pipeline->frag_shader;
		shader_info[1].pName = "fragMain";
	}
}

static VkPipelineViewportStateCreateInfo	viewportCreate(void)
{
	VkPipelineViewportStateCreateInfo	viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = NULL,
		.scissorCount = 1,
		.pScissors = NULL
	};

	return viewport_info;
}

static VkPipelineRasterizationStateCreateInfo	rasterizationCreate(VkCullModeFlags cull_mode, VkFrontFace front_face)
{
	VkPipelineRasterizationStateCreateInfo	rasterization_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = cull_mode,
		.frontFace = front_face,
		.lineWidth = 1.0f
	};
	return rasterization_info;
}

static VkPipelineColorBlendAttachmentState	colorBlend(bool blend)
{
	VkBool32	blend_enable;
	if (blend)	blend_enable = VK_TRUE;
	else		blend_enable = VK_FALSE;

	VkPipelineColorBlendAttachmentState	color_blend_attach = {
		.blendEnable = blend_enable,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask =\
		VK_COLOR_COMPONENT_R_BIT
		| VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT
		| VK_COLOR_COMPONENT_A_BIT,
	};
	return color_blend_attach;
}

void	createPBRPipeline(GraphicsContext *ctx)
{
	VkPushConstantRange	push_constant_ranges[] = {
		// Mat push constant
		{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(MaterialProperties),
		},
	};

	VkDescriptorSetLayout	layouts[] = {
		ctx->ubo_descriptor_layout,
		ctx->instance_descriptor_layout,
		ctx->material_descriptor_layout
	};

	VkPipelineLayoutCreateInfo	layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeofarray(layouts),
		.pSetLayouts = layouts,
		.pushConstantRangeCount = sizeofarray(push_constant_ranges),
		.pPushConstantRanges = push_constant_ranges,
	};


	if (vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->pipeline_pbr.layout) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create pipeline layout\n");
		exit(1);
	}

	u32	shader_stage_count;
	VkPipelineShaderStageCreateInfo	shader_stages[2] = {0};
	pipelineShaderCreate(ctx, STRING_LIT("pbr"), &ctx->pipeline_pbr, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &shader_stage_count, shader_stages);

	VkVertexInputBindingDescription	bind_desc = {
		.binding = 0,
		.stride = sizeof(Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkVertexInputAttributeDescription	attribute_desc[] = {
		{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos)},
		{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal)},
		{.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)},
		{.location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, tangent)},
	};

	VkPipelineVertexInputStateCreateInfo	vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pVertexBindingDescriptions = &bind_desc,
		.vertexBindingDescriptionCount = 1,
		.pVertexAttributeDescriptions = attribute_desc,
		.vertexAttributeDescriptionCount = sizeofarray(attribute_desc),
	};

	VkPipelineInputAssemblyStateCreateInfo	imput_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	VkPipelineDepthStencilStateCreateInfo	depth_stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE
	};

	VkPipelineViewportStateCreateInfo	viewport_info = viewportCreate();

	VkPipelineRasterizationStateCreateInfo	rasterization_info = rasterizationCreate(
		VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineMultisampleStateCreateInfo	multisample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	VkPipelineColorBlendAttachmentState	color_blend_attach = colorBlend(true);

	VkPipelineColorBlendStateCreateInfo	blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attach
	};

	VkDynamicState	dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo	dynamic_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeofarray(dynamic_states),
		.pDynamicStates = dynamic_states
	};

	VkPipelineRenderingCreateInfo	render_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &ctx->swapchain_format.format,
		.depthAttachmentFormat = ctx->swapchain_depth_format
	};

	VkGraphicsPipelineCreateInfo	pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &render_info,
		.stageCount = shader_stage_count,
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &imput_assembly_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &rasterization_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_stencil_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state_info,
		.layout = ctx->pipeline_pbr.layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pipeline_pbr.handle) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create graphics pipeline\n");
		exit(1);
	}

	engine_log(LOG_FILE, "Successfully created graphics pipeline");
}

static void	createTEXTPipeline(GraphicsContext *ctx)
{
	VkPushConstantRange	push_constant_ranges[] = {
		// Screen size
		{
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(TextPushConstants),
		},
	};

	VkDescriptorSetLayout	layouts[] = {
		ctx->atlas_descriptor_layout,
	};

	VkPipelineLayoutCreateInfo	layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeofarray(layouts),
		.pSetLayouts = layouts,
		.pushConstantRangeCount = sizeofarray(push_constant_ranges),
		.pPushConstantRanges = push_constant_ranges,
	};

	if (vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->pipeline_text.layout) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create pipeline layout\n");
		exit(1);
	}

	u32	shader_stage_count;
	VkPipelineShaderStageCreateInfo	shader_stages[2] = {0};
	pipelineShaderCreate(ctx, STRING_LIT("text"), &ctx->pipeline_text, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &shader_stage_count, shader_stages);

	VkVertexInputBindingDescription	bind_desc = {
		.binding = 0,
		.stride = sizeof(UiRenderInstance),
		.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
	};

	VkVertexInputAttributeDescription	attribute_desc[] = {
		{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiRenderInstance, pos)},
		{1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiRenderInstance, size)},
		{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiRenderInstance, uv_offset)},
		{3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiRenderInstance, uv_size)},
		{4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiRenderInstance, color)},
		{5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiRenderInstance, inner_color)},
		{6, 0, VK_FORMAT_R32_UINT, offsetof(UiRenderInstance, primitive_type)},
		{7, 0, VK_FORMAT_R32_SFLOAT, offsetof(UiRenderInstance, corner_radius)},
		{8, 0, VK_FORMAT_R32_SFLOAT, offsetof(UiRenderInstance, stroke_width)},
		{9, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiRenderInstance, clip_rect)},
	};

	VkPipelineVertexInputStateCreateInfo	vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pVertexBindingDescriptions = &bind_desc,
		.vertexBindingDescriptionCount = 1,
		.pVertexAttributeDescriptions = attribute_desc,
		.vertexAttributeDescriptionCount = sizeofarray(attribute_desc),
	};

	VkPipelineInputAssemblyStateCreateInfo	imput_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
	};

	// For now no depth testing need since this a ui only pipeline
	VkPipelineDepthStencilStateCreateInfo	depth_stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_FALSE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE
	};

	VkPipelineViewportStateCreateInfo	viewport_info = viewportCreate();

	VkPipelineRasterizationStateCreateInfo	rasterization_info = rasterizationCreate(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineMultisampleStateCreateInfo	multisample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	VkPipelineColorBlendAttachmentState color_blend_attach = colorBlend(true);

	VkPipelineColorBlendStateCreateInfo	blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attach
	};

	VkDynamicState	dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo	dynamic_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeofarray(dynamic_states),
		.pDynamicStates = dynamic_states
	};

	VkPipelineRenderingCreateInfo	render_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &ctx->swapchain_format.format,
		.depthAttachmentFormat = ctx->swapchain_depth_format
	};

	VkGraphicsPipelineCreateInfo	pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &render_info,
		.stageCount = shader_stage_count,
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &imput_assembly_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &rasterization_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_stencil_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state_info,
		.layout = ctx->pipeline_text.layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pipeline_text.handle) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create text pipeline\n");
		exit(1);
	}

	engine_log(LOG_FILE, "Successfully created text pipeline");
}

static void	createGRIDPipeline(GraphicsContext *ctx)
{
	VkPushConstantRange	push_constant_ranges[] = {
		// Grid Properties
		{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(GridProperties),
		},
	};

	VkDescriptorSetLayout	layouts[] = {
		ctx->ubo_descriptor_layout,
	};

	VkPipelineLayoutCreateInfo	layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeofarray(layouts),
		.pSetLayouts = layouts,
		.pushConstantRangeCount = sizeofarray(push_constant_ranges),
		.pPushConstantRanges = push_constant_ranges,
	};


	if (vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->pipeline_grid.layout) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create pipeline layout\n");
		exit(1);
	}

	u32				shader_stage_count;
	VkPipelineShaderStageCreateInfo	shader_stages[2] = {0};
	pipelineShaderCreate(ctx, STRING_LIT("grid"), &ctx->pipeline_grid, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &shader_stage_count, shader_stages);


	VkPipelineVertexInputStateCreateInfo	vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pVertexBindingDescriptions = NULL,
		.vertexBindingDescriptionCount = 0,
		.pVertexAttributeDescriptions = NULL,
		.vertexAttributeDescriptionCount = 0,
	};

	VkPipelineInputAssemblyStateCreateInfo	imput_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};

	VkPipelineDepthStencilStateCreateInfo	depth_stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE
	};

	VkPipelineViewportStateCreateInfo	viewport_info = viewportCreate();

	VkPipelineRasterizationStateCreateInfo	rasterization_info = rasterizationCreate(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineMultisampleStateCreateInfo	multisample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	VkPipelineColorBlendAttachmentState	color_blend_attach = colorBlend(true);

	VkPipelineColorBlendStateCreateInfo	blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attach
	};

	VkDynamicState	dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo	dynamic_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeofarray(dynamic_states),
		.pDynamicStates = dynamic_states
	};

	VkPipelineRenderingCreateInfo	render_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &ctx->swapchain_format.format,
		.depthAttachmentFormat = ctx->swapchain_depth_format
	};

	VkGraphicsPipelineCreateInfo	pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &render_info,
		.stageCount = shader_stage_count,
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &imput_assembly_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &rasterization_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_stencil_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state_info,
		.layout = ctx->pipeline_grid.layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pipeline_grid.handle) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create graphics pipeline\n");
		exit(1);
	}

	engine_log(LOG_FILE, "Successfully created graphics pipeline");
}

static void	createFrameResources(GraphicsContext *ctx)
{
	ctx->frames_in_flight_count = MAX_FRAMES_IN_FLIGHT;
	// TODO: Change allocation?
	ctx->frame_resources = calloc(ctx->frames_in_flight_count, sizeof(FrameResources));

	VkSemaphoreTypeCreateInfo	semaphore_type_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = ctx->frames_in_flight_count
	};

	VkSemaphoreCreateInfo	timeline_semaphore_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &semaphore_type_info
	};

	if (vkCreateSemaphore(ctx->device, &timeline_semaphore_info, NULL, &ctx->timeline_semaphore) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create timeline semaphore");
		exit(1);
	}

	// Single time commands
	VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = ctx->queue_family_index,
	};

	if (vkCreateCommandPool(ctx->device, &pool_info, NULL, &ctx->single_time_pool) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create single time command pool");
		exit(1);
	}

	for(u32 i = 0; i < ctx->frames_in_flight_count; i++) {
		FrameResources	*resource = &ctx->frame_resources[i];

		VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
		};

		if (vkCreateSemaphore(ctx->device, &semaphore_info, NULL, &resource->image_acquired_semaphore) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to create semaphore for frame resources nr: %u\n", i);
			exit(1);
		}

		VkCommandPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = ctx->queue_family_index,
		};

		if (vkCreateCommandPool(ctx->device, &pool_info, NULL, &resource->cmd_pool) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to create command pool nr: %u", i);
			exit(1);
		}

		VkCommandBufferAllocateInfo buffer_alloc_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = resource->cmd_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};

		if (vkAllocateCommandBuffers(ctx->device, &buffer_alloc_info, &resource->cmd_buf) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to create command buffer nr: %u", i);
			exit(1);
		}

		VkBufferCreateInfo	instance_buffer_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(EntityInstanceData) * MAX_INSTANCES,
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		createMappedBuffer(ctx->vma_allocator, &instance_buffer_info, &resource->instance_buffer);

		VkBufferCreateInfo	text_buffer_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(UiRenderInstance) * MAX_COMPONENTS,
			.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		createMappedBuffer(ctx->vma_allocator, &text_buffer_info, &resource->text_instance_buffer);

		VkBufferCreateInfo	ubo_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.size = sizeof(UniformBufferObject),
		};

		createMappedBuffer(ctx->vma_allocator, &ubo_info, &resource->uniform_buffer);
	}

	engine_log(LOG_FILE, "Successfully created frame resources");
}

static void	destroyShaders(GraphicsContext *ctx)
{
	vkDestroyShaderModule(ctx->device, ctx->pipeline_pbr.vertex_shader, NULL);
	vkDestroyShaderModule(ctx->device, ctx->pipeline_pbr.frag_shader, NULL);
}

static void	destroySwapchain(GraphicsContext *ctx)
{
	for (u32 i = 0; i < ctx->image_count; i++) {
		vkDestroyImageView(ctx->device, ctx->swapchain_image_views[i], NULL);
		vkDestroySemaphore(ctx->device, ctx->render_complete_sequence_semaphores[i], NULL);
	}
	free(ctx->render_complete_sequence_semaphores);
	free(ctx->swapchain_image_views);
	vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);

	vkDestroyImageView(ctx->device, ctx->swapchain_depth_image.view, NULL);
	wrapperVMAdestroyImage(ctx->vma_allocator, ctx->swapchain_depth_image.image, ctx->swapchain_depth_image.allocation);
	ctx->swapchain_depth_image.view = NULL;
}

static void	destroySyncResources(GraphicsContext *ctx)
{
	for (u32 i = 0; i < ctx->frames_in_flight_count; i++) {
		FrameResources	*resource = &ctx->frame_resources[i];

		vkDestroySemaphore(ctx->device, resource->image_acquired_semaphore, NULL);
		vkDestroyCommandPool(ctx->device, resource->cmd_pool, NULL);
		wrapperVMAunmapMemory(ctx->vma_allocator, resource->instance_buffer.allocation);
		wrapperVMAunmapMemory(ctx->vma_allocator, resource->uniform_buffer.allocation);
		wrapperVMAdestroyBuffer(ctx->vma_allocator, resource->instance_buffer.handle, resource->instance_buffer.allocation);
		wrapperVMAdestroyBuffer(ctx->vma_allocator, resource->uniform_buffer.handle, resource->uniform_buffer.allocation);
	}
	vkDestroySemaphore(ctx->device, ctx->timeline_semaphore, NULL);
}

static void	createDescriptorSetLayouts(GraphicsContext *ctx)
{
	VkDescriptorSetLayoutBinding	ubo_layout_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	VkDescriptorSetLayoutBinding	instance_layout_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};
	VkDescriptorSetLayoutBinding	altas_layout_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	VkDescriptorSetLayoutCreateInfo	ubo_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &ubo_layout_binding,
	};

	VkDescriptorSetLayoutCreateInfo	instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &instance_layout_binding,
	};

	VkDescriptorSetLayoutCreateInfo	altas_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &altas_layout_binding,
	};

	if (vkCreateDescriptorSetLayout(ctx->device, &ubo_create_info, NULL, &ctx->ubo_descriptor_layout) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create ubo layout");
	}
	if (vkCreateDescriptorSetLayout(ctx->device, &instance_create_info, NULL, &ctx->instance_descriptor_layout) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create ubo layout");
	}
	if (vkCreateDescriptorSetLayout(ctx->device, &altas_create_info, NULL, &ctx->atlas_descriptor_layout) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create ubo layout");
	}
}

static void	createDescriptorPoolSets(GraphicsContext *ctx)
{
	const	u32	material_descriptor_count = 1000;
	const	u32	atlas_count = 1;
	VkDescriptorPoolSize	pool_sizes[] = {
		// ubo
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT,
		},
		// instances
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT,
		},
		// materials
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = material_descriptor_count,
		},
		// text atlas
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = atlas_count,
		},
	};

	u64	total_sets = 0;
	for (u32 i = 0; i < sizeofarray(pool_sizes); i++) {
		total_sets += pool_sizes[i].descriptorCount;
	}

	VkDescriptorPoolCreateInfo	create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = total_sets,
		.poolSizeCount = sizeofarray(pool_sizes),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(ctx->device, &create_info, NULL, &ctx->descriptor_pool) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create descriptor pool");
	}

	VkDescriptorSetLayout ubo_layouts[MAX_FRAMES_IN_FLIGHT];
	VkDescriptorSetLayout instance_layouts[MAX_FRAMES_IN_FLIGHT];
	for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		ubo_layouts[i] = ctx->ubo_descriptor_layout;
		instance_layouts[i] = ctx->instance_descriptor_layout;
	}

	VkDescriptorSetAllocateInfo	ubo_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->descriptor_pool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = ubo_layouts,
	};
	VkDescriptorSetAllocateInfo	instance_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->descriptor_pool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = instance_layouts,
	};
	VkDescriptorSetAllocateInfo	atlas_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->descriptor_pool,
		.descriptorSetCount = atlas_count,
		.pSetLayouts = &ctx->atlas_descriptor_layout,
	};

	if (vkAllocateDescriptorSets(ctx->device, &ubo_alloc_info, ctx->ubo_descriptor_sets) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to allocate descriptor sets");
		exit(1);
	}
	if (vkAllocateDescriptorSets(ctx->device, &instance_alloc_info, ctx->instance_descriptor_sets) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to allocate descriptor sets");
		exit(1);
	}
	if (vkAllocateDescriptorSets(ctx->device, &atlas_alloc_info, &ctx->atlas_descriptor_set) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to allocate descriptor sets");
		exit(1);
	}

	for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		FrameResources	*resource = &ctx->frame_resources[i];

		VkDescriptorBufferInfo	instance_buf_info = {
			.buffer = resource->instance_buffer.handle,
			.offset = 0,
			.range = sizeof(EntityInstanceData) * MAX_INSTANCES,
		};

		VkWriteDescriptorSet	instance_descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ctx->instance_descriptor_sets[i],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &instance_buf_info,
		};


		VkDescriptorBufferInfo	ubo_buf_info = {
			.buffer = resource->uniform_buffer.handle,
			.offset = 0,
			.range = sizeof(UniformBufferObject),
		};

		VkWriteDescriptorSet	ubo_descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ctx->ubo_descriptor_sets[i],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &ubo_buf_info,
		};

		VkWriteDescriptorSet	writes[] = { ubo_descriptor_write, instance_descriptor_write };
		vkUpdateDescriptorSets(ctx->device, sizeofarray(writes), writes, 0, NULL);
	}

	engine_log(LOG_FILE, "Successfully created descriptor pools and sets");
}

static void	initVulkan(GraphicsContext *ctx)
{
	createInstance(ctx);
	volkLoadInstance(ctx->vk_instance);
	createSurface(ctx);
	createDevice(ctx);
	volkLoadDevice(ctx->device);
	ctx->vma_allocator = initializeVMA(ctx->phys_device, ctx->device, ctx->vk_instance);
	if (ctx->vma_allocator == NULL) {
		engine_error(LOG_FILE, "Failed to create vma allocator\n");
		exit(1);
	}
	createSwapchain(ctx, ctx->window_width, ctx->window_height);
	createDescriptorSetLayouts(ctx);
	createFrameResources(ctx);
	createDescriptorPoolSets(ctx);
	createDefaultTextures(ctx);
	createMaterialDescriptorSetLayout(ctx);
	createPBRPipeline(ctx);
	createGRIDPipeline(ctx);
	createTEXTPipeline(ctx);
	ctx->frame_index = 0;
	ctx->next_signal_value = ctx->frames_in_flight_count + 1;
}

static void	destroyVulkan(GraphicsContext *ctx)
{
	destroySyncResources(ctx);
	vkDestroyPipelineLayout(ctx->device, ctx->pipeline_pbr.layout, NULL);
	vkDestroyPipeline(ctx->device, ctx->pipeline_pbr.handle, NULL);
	destroyShaders(ctx);
	destroySwapchain(ctx);
	destroyVMA(ctx->vma_allocator);
	vkDestroySurfaceKHR(ctx->vk_instance, ctx->surface, NULL);
	vkDestroyDevice(ctx->device, NULL);
	vkDestroyInstance(ctx->vk_instance, NULL);
	volkFinalize();
}

static void	initSdl(GraphicsContext *ctx)
{
	SDL_InitSubSystem(SDL_INIT_VIDEO);

	if ((ctx->window = SDL_CreateWindow("Hello world", ctx->window_width, ctx->window_height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE)) == NULL) {
		engine_error(LOG_FILE, "Couldnt create window");
		exit(1);
	} else {
		engine_log(LOG_FILE, "SDL window created");
	}
}

static inline void	getProjectionMatrix(mat4 dst, Camera *c, float aspect_ratio)
{
	glm_perspective(glm_rad(c->zoom), aspect_ratio, 0.1f, 100.0f, dst);
}

static inline void	getViewMatrix(mat4 dst, Camera *c)
{
	vec3	center;
	glm_vec3_add(c->position, c->front, center);
	glm_lookat(c->position, center, c->up, dst);
}

static void updateUniformBuffer(GraphicsContext *ctx, FrameResources *resource, Camera *cam)
{
	UniformBufferObject ubo = {0}; // Ensure zero initialization

	getViewMatrix(ubo.view, cam);
	getProjectionMatrix(ubo.proj, cam, (float)ctx->swapchain_width / (float)ctx->swapchain_height);

	// Vulkan y shift
	ubo.proj[1][1] *= -1;
	glm_mat4_inv(ubo.proj, ubo.inv_proj);
	glm_mat4_inv(ubo.view, ubo.inv_view);

	glm_vec4_copy((vec4){cam->position[0], cam->position[1], cam->position[2], 1.0f}, ubo.cam_pos);

	// --- Setup Global Sun ---
	// Pointing slightly down and to the side
	vec3 sun_dir = { -0.2f, -1.0f, -0.3f };
	glm_vec3_normalize(sun_dir);
	glm_vec4_copy((vec4){sun_dir[0], sun_dir[1], sun_dir[2], 0.0f}, ubo.sun_direction);

	// Warm sunlight, intensity 5.0
	glm_vec4_copy((vec4){1.0f, 0.95f, 0.8f, 5.0f}, ubo.sun_color);

	ubo.exposure = 4.5f;
	ubo.gamma = 2.2f;

	memcpy(resource->uniform_buffer.mapped, &ubo, sizeof(UniformBufferObject));
}

void	TEXTPass(GraphicsContext *ctx, FrameResources *resource, UiRenderInfo info)
{
	const VkCommandBuffer	cmd = resource->cmd_buf;
	const PipelineObject	pipeline = ctx->pipeline_text;
	const VkDescriptorSet	atlas_descriptor_set = ctx->atlas_descriptor_set;

	memcpy(resource->text_instance_buffer.mapped, info.render_instances, info.upload_size);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_text.handle);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &atlas_descriptor_set, 0, NULL);

	const TextPushConstants	push_constants = {{ctx->window_width, ctx->window_height}, {info.atlas_size[0], info.atlas_size[1]}, info.px_range};
	vkCmdPushConstants(cmd, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants), &push_constants);

	VkDeviceSize	offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &resource->text_instance_buffer.handle, &offset);

	vkCmdDraw(cmd, 6, info.instance_count, 0, 0);
}

void	GRIDPass(GraphicsContext *ctx, FrameResources *resource, u32 frame_idx)
{
	const VkDescriptorSet	ubo_descriptor_set = ctx->ubo_descriptor_sets[frame_idx];

	vkCmdBindPipeline(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_grid.handle);
	VkDescriptorSet	grid_descriptor_sets[] = { ubo_descriptor_set };

	vkCmdPushConstants(resource->cmd_buf, ctx->pipeline_grid.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GridProperties), &ctx->grid_properties);
	vkCmdBindDescriptorSets(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_grid.layout, 0, 1, grid_descriptor_sets, 0, NULL);
	vkCmdDraw(resource->cmd_buf, 3, 1, 0, 0);
}

void	PBRPass(GraphicsContext *ctx, EntityRenderInfo entity_info, FrameResources *resource, u8 frame_idx)
{
	const u32		total_entity_count = entity_info.entity_count;
	const VkDescriptorSet	ubo_descriptor_set = ctx->ubo_descriptor_sets[frame_idx];
	const VkDescriptorSet	instance_descriptor_set = ctx->instance_descriptor_sets[frame_idx];
	EntityInstanceData		*instance_data_buf = (EntityInstanceData *)resource->instance_buffer.mapped;
	VkDeviceSize		offset = 0;
	u32			instance_cursor = 0;

	vkCmdBindPipeline(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_pbr.handle);

	u32	e_idx = 0;
	while (e_idx < total_entity_count) {
		u16		model_idx = entity_info.data[e_idx].model_idx;
		const Model	*model = entity_info.models[model_idx];

		// Find total number of entities using this model
		u32	run_start = e_idx;
		u32	run_end = e_idx + 1;
		while (run_end < total_entity_count && model_idx == entity_info.data[run_end].model_idx) run_end++;
		u32	run_count = run_end - run_start;

		for (u32 n = 0; n < model->node_count; n++) {
			Node		*node = &model->linear_nodes[n];
			for (u32 m = 0; m < node->mesh_count; m++) {
				const Mesh	*mesh = &node->meshes[m];


				if (mesh->vertex_count == 0) continue;

				u32	first_instance = instance_cursor;
				for (u32 r = 0; r < run_count; r++) {
					EntityRenderData	*e_data = &entity_info.data[run_start + r];
					glm_mat4_mul(e_data->instance_data.model_mat, node->world_transform, instance_data_buf[instance_cursor].model_mat);
					instance_cursor++;
				}

				VkDescriptorSet		pbr_descriptor_sets[] = { ubo_descriptor_set, instance_descriptor_set, VK_NULL_HANDLE };

				MaterialProperties push_constants_mat = {
					.base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f},
					.metallic_factor = 0.0f,
					.roughness_factor = 0.8f,
					.basecolor_texture_set = -1,
					.physical_descriptor_texture_set = -1,
					.normal_texture_set = -1,
					.occlusion_texture_set = -1,
					.emissive_texture_set = -1,
					.alpha_mask = 0.0f,
					.alpha_mask_cut_off = 0.5f
				};
				u32		pbr_descriptor_set_count = 2;
				if (mesh->material_index >= 0) {
					Material	*mat = &model->materials[mesh->material_index];

					pbr_descriptor_set_count += 1;
					pbr_descriptor_sets[2] = mat->descriptor_set;

					push_constants_mat.roughness_factor = mat->roughness_factor;
					push_constants_mat.metallic_factor = mat->metallic_factor;
					push_constants_mat.alpha_mask_cut_off = mat->alpha_cutoff;
					push_constants_mat.basecolor_texture_set = 0;
					push_constants_mat.physical_descriptor_texture_set = 1;
					push_constants_mat.normal_texture_set = 2;
					push_constants_mat.occlusion_texture_set = 3;
					push_constants_mat.emissive_texture_set = 4;
					push_constants_mat.alpha_mask = 0.0f;
					glm_vec4_copy(mat->base_color_factor, push_constants_mat.base_color_factor);
				}
				vkCmdPushConstants(resource->cmd_buf, ctx->pipeline_pbr.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MaterialProperties), &push_constants_mat);
				vkCmdBindDescriptorSets(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_pbr.layout, 0, pbr_descriptor_set_count, pbr_descriptor_sets, 0, NULL);
				vkCmdBindVertexBuffers(resource->cmd_buf, 0, 1, &mesh->gpu_vertex_data, &offset);
				vkCmdBindIndexBuffer(resource->cmd_buf, mesh->gpu_index_data, 0, mesh->index_type);
				vkCmdDrawIndexed(resource->cmd_buf, mesh->index_count, run_count, 0, 0, first_instance);

			}
		}

		e_idx = run_end;
	}
}

void	render(GraphicsContext *ctx, Camera *camera, EntityRenderInfo entity_info, UiRenderInfo text_info)
{
	if (ctx->swapchain_require_recreate)
	{
		vkDeviceWaitIdle(ctx->device);
		destroySwapchain(ctx);
		createSwapchain(ctx, ctx->window_width, ctx->window_height);
		ctx->swapchain_require_recreate = false;
	}

	const u8	frame_res_index = ctx->frame_index++ % ctx->frames_in_flight_count;
	const u64	signal_value = ctx->next_signal_value++;
	const u64	wait_value = signal_value - ctx->frames_in_flight_count;

	FrameResources	*resource = &ctx->frame_resources[frame_res_index];

	updateUniformBuffer(ctx, resource, camera);

	VkSemaphoreWaitInfo	wait_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &ctx->timeline_semaphore,
		.pValues = &wait_value
	};
	vkWaitSemaphores(ctx->device, &wait_info, UINT64_MAX);

	vkResetCommandPool(ctx->device, resource->cmd_pool, 0);

	VkSemaphore	image_acquire_semaphore = resource->image_acquired_semaphore;

	u32	img_idx = 0;
	VkResult acquire_result = vkAcquireNextImageKHR(ctx->device,
						 ctx->swapchain,
						 UINT64_MAX,
						 image_acquire_semaphore,
						 VK_NULL_HANDLE,
						 &img_idx);

	if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		ctx->swapchain_require_recreate = true;
		return ;
	}
	else if (acquire_result == VK_SUBOPTIMAL_KHR)
	{
		ctx->swapchain_require_recreate = true;
	}

	VkCommandBufferBeginInfo	cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(resource->cmd_buf, &cmd_begin_info);

	VkImageMemoryBarrier2	layout_barriers[2] = {0};
	layout_barriers[0] = (VkImageMemoryBarrier2){
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.image = ctx->swapchain_images[img_idx],
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	layout_barriers[1] = (VkImageMemoryBarrier2){
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
		.srcAccessMask = 0,
		.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT ,
		.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.image = ctx->swapchain_depth_image.image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};

	VkDependencyInfo	dep_info = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = sizeofarray(layout_barriers),
		.pImageMemoryBarriers = layout_barriers
	};
	vkCmdPipelineBarrier2(resource->cmd_buf, &dep_info);

	VkRenderingAttachmentInfo	color_attach_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = ctx->swapchain_image_views[img_idx],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = {
			.color = {{0.01f, 0.01f, 0.01f, 1}},
		}
	};
	VkRenderingAttachmentInfo	depth_attach_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = ctx->swapchain_depth_image.view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.clearValue = {.depthStencil = {1.0f, 0}}
	};

	VkRenderingInfo	render_info = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = {
			.offset = {0, 0},
			.extent = {.width = ctx->swapchain_width, .height = ctx->swapchain_height}
		},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attach_info,
		.pDepthAttachment = &depth_attach_info,
	};


	vkCmdBeginRendering(resource->cmd_buf, &render_info);
	{
		VkViewport	viewport = {
			.x = 0, .y = 0,
			.width = ctx->swapchain_width,
			.height = ctx->swapchain_height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(resource->cmd_buf, 0, 1, &viewport);

		VkRect2D	scissor = {
			.offset = {0, 0},
			.extent = { .width = ctx->swapchain_width, .height = ctx->swapchain_height, }
		};
		vkCmdSetScissor(resource->cmd_buf, 0, 1, &scissor);

		// ---- GRID Pass ------------------ //
		if (gameStateQuery(game_state, ShowGrid))
			GRIDPass(ctx, resource, frame_res_index);

		// ---- PBR Pass --------------- //
		PBRPass(ctx, entity_info, resource, frame_res_index);

		// ---- TEXT Pass --------------- //
		TEXTPass(ctx, resource, text_info);
	}
	vkCmdEndRendering(resource->cmd_buf);

	ImageTransitionInfo	img_colorattach_present = {
		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.image = ctx->swapchain_images[img_idx],
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.pRange = NULL,
	};
	cmdTransitionImage(resource->cmd_buf, &img_colorattach_present);

	vkEndCommandBuffer(resource->cmd_buf);

	VkSemaphoreSubmitInfo	image_acquired_wait_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = image_acquire_semaphore,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
	};

	VkSemaphoreSubmitInfo	semaphore_signals[2];
	semaphore_signals[0] = (VkSemaphoreSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = ctx->render_complete_sequence_semaphores[img_idx],
		.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
	};
	semaphore_signals[1] = (VkSemaphoreSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = ctx->timeline_semaphore,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.value = signal_value
	};

	VkCommandBufferSubmitInfo	cmd_submit_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = resource->cmd_buf,
	};

	VkSubmitInfo2	submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &image_acquired_wait_info,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmd_submit_info,
		.signalSemaphoreInfoCount = sizeofarray(semaphore_signals),
		.pSignalSemaphoreInfos = semaphore_signals
	};
	vkQueueSubmit2(ctx->queue, 1, &submit_info, VK_NULL_HANDLE);

	VkPresentInfoKHR	present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &ctx->render_complete_sequence_semaphores[img_idx],
		.swapchainCount = 1,
		.pSwapchains = &ctx->swapchain,
		.pImageIndices = &img_idx,
		.pResults = NULL
	};

	vkQueuePresentKHR(ctx->queue, &present_info);
}

void	startGraphics(GraphicsContext *ctx)
{
	const i32	width = 800;
	const i32	height = 600;

	ctx->window_width = width;
	ctx->window_height = height;

	const char	*const requiredExtensions[] = {
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
	};
	ctx->required_extensions = requiredExtensions;
	ctx->required_extension_count = sizeofarray(requiredExtensions);

	const char	*const requestedLayers[] = {
		"VK_LAYER_KHRONOS_validation"
	};
	ctx->required_layers = requestedLayers;
	ctx->required_layer_count = sizeofarray(requestedLayers);

	initSdl(ctx);
	initVulkan(ctx);
}

void	endGraphics(GraphicsContext *ctx)
{
	destroyVulkan(ctx);
	SDL_DestroyWindow(ctx->window);
	SDL_Quit();
}
