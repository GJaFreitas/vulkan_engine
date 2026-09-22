#include "vulkan_inner.h"
#include "world.h"
#include "ui.h"
#include "graphics_layer.h"
#include "fonts.h"
#include <stdint.h>

// --- VULKAN API --- //

void	createVkBuffer(GraphicsContext *ctx, const BufferInfo *info, BufferObject *out_buffer)
{
	VkBufferCreateInfo buf_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = info->size,
		// CRITICAL: Automatically inject the BDA flag into ALL buffers
		.usage = info->usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	wrapperVMAcreateBuffer(ctx->vma_allocator, &buf_info, &out_buffer->handle, &out_buffer->allocation, info->cpu_accessible);

	if (info->cpu_accessible) {
		wrapperVMAmapMemory(ctx->vma_allocator, out_buffer->allocation, &out_buffer->mapped);
	} else {
		out_buffer->mapped = NULL;
	}

	VkBufferDeviceAddressInfo bda_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = out_buffer->handle
	};
	out_buffer->device_address = vkGetBufferDeviceAddress(ctx->device, &bda_info);
}

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
		.shaderDemoteToHelperInvocation = VK_TRUE,
		.pNext = &features14,
	};
	VkPhysicalDeviceVulkan12Features	features12 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.timelineSemaphore = VK_TRUE,
		// -- Buffer Device Address (BDA) --
		// Allows us to get 64-bit pointers to GPU memory
		.bufferDeviceAddress = VK_TRUE,

		// -- Descriptor Indexing (Global Texture Heap) --
		// Allows us to leave slots in our 10,000 texture array empty
		.descriptorBindingPartiallyBound = VK_TRUE,
		// Allows us to add textures to the array even while the GPU is rendering
		.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,

		// Allows shaders to index the array using non-constant integers (like from push constants)
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,

		// Allows the array to have a dynamic size (unbounded arrays in shaders)
		.runtimeDescriptorArray = VK_TRUE,

		// Enable unpadded structs to be passed to GPU
		.scalarBlockLayout = VK_TRUE,
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
		.features = {
			.multiDrawIndirect = VK_TRUE,
		},
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

	VkExtent2D	real_extent = { width, height };
	if (surface_capabilities.currentExtent.width != UINT32_MAX) {
		real_extent = surface_capabilities.currentExtent;
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
		.imageExtent = real_extent,
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

// --- PIPELINE CREATION --- //

static void	createShaders(GraphicsContext *ctx, StringView shader_name, VkShaderStageFlagBits shader_stage, VkShaderModule *module)
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

	if (shader_stage == VK_SHADER_STAGE_VERTEX_BIT)
		*module = createShaderModule(shader, shaderc_vertex_shader, ctx);
	else if (shader_stage == VK_SHADER_STAGE_FRAGMENT_BIT)
		*module = createShaderModule(shader, shaderc_fragment_shader, ctx);
}

static void	pipelineShaderCreate(GraphicsContext *ctx, String shader_name, const char *pName, VkShaderStageFlagBits shader_stage, VkPipelineShaderStageCreateInfo *shader_info, VkShaderModule *module)
{
	createShaders(ctx, shader_name, shader_stage, module);

	shader_info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_info->stage = shader_stage;
	shader_info->module = *module;
	shader_info->pName = pName;
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

static VkPipelineMultisampleStateCreateInfo	multisampleCreate(void)
{
	VkPipelineMultisampleStateCreateInfo	multisample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	return multisample_info;
}

// TODO: Create a helper function for depth stencil

static void    createShadowResources(GraphicsContext *ctx)
{
	// 1. Single Sampler for shadow lookup
	VkSamplerCreateInfo    sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};

	if (vkCreateSampler(ctx->device, &sampler_info, NULL, &ctx->shadow_sampler) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create shadow sampler\n");
		exit(1);
	}

	// 2. Per-frame Cascaded Shadow Maps
	for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		CascadedShadowMap    *csm = &ctx->shadow_maps[i];

		VkImageCreateInfo    image_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = ctx->swapchain_depth_format,
			.extent = { .width = SHADOW_MAP_RESOLUTION, .height = SHADOW_MAP_RESOLUTION, .depth = 1 },
			.mipLevels = 1,
			.arrayLayers = SHADOW_MAP_CASCADE_COUNT,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		if (wrapperVMAcreateImage(ctx->vma_allocator, &image_info, &csm->image.image, &csm->image.allocation) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to allocate shadow map image\n");
			exit(1);
		}

		// Create 2D Array View (Sampled by PBR shader across all cascades)
		VkImageViewCreateInfo    array_view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = csm->image.image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
			.format = ctx->swapchain_depth_format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = SHADOW_MAP_CASCADE_COUNT
			}
		};

		if (vkCreateImageView(ctx->device, &array_view_info, NULL, &csm->image.view) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to create shadow map array view\n");
			exit(1);
		}

		// Create individual 2D Layer Views (Used as depth targets during shadow render passes)
		for (u32 j = 0; j < SHADOW_MAP_CASCADE_COUNT; j++) {
			VkImageViewCreateInfo    layer_view_info = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = csm->image.image,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = ctx->swapchain_depth_format,
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = j,
					.layerCount = 1
				}
			};

			if (vkCreateImageView(ctx->device, &layer_view_info, NULL, &csm->cascade_views[j]) != VK_SUCCESS) {
				engine_error(LOG_FILE, "Failed to create shadow map cascade layer view\n");
				exit(1);
			}

		}

		VkDescriptorSetAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = ctx->global_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &ctx->shadow_descriptor_layout
		};

		if (vkAllocateDescriptorSets(ctx->device, &alloc_info, &csm->descriptor_set) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to allocate shadow descriptor set\n");
			exit(1);
		}

		VkDescriptorImageInfo s_image_info = {
			.sampler = ctx->shadow_sampler,
			.imageView = csm->image.view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 
		};

		VkWriteDescriptorSet descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = csm->descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.pImageInfo = &s_image_info
		};

		vkUpdateDescriptorSets(ctx->device, 1, &descriptor_write, 0, NULL);
	}

	engine_log(LOG_FILE, "Successfully created shadow resources");
}

void	createSHADOWPipeline(GraphicsContext *ctx)
{
	VkPipelineShaderStageCreateInfo	shader_stages[1] = {0};
	VkShaderModule	vertex;
	pipelineShaderCreate(ctx, STRING_LIT("shadows"), "vertMain", VK_SHADER_STAGE_VERTEX_BIT, &shader_stages[0], &vertex);

	VkPipelineVertexInputStateCreateInfo    vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	VkPipelineInputAssemblyStateCreateInfo    imput_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkPipelineDepthStencilStateCreateInfo    depth_stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE
	};
	VkPipelineViewportStateCreateInfo	viewport = viewportCreate();
	VkPipelineRasterizationStateCreateInfo	raster_info = rasterizationCreate(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	raster_info.depthBiasEnable = VK_TRUE;
	VkPipelineMultisampleStateCreateInfo	multisample_info = multisampleCreate();
	VkDynamicState    dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_DEPTH_BIAS 
	};
	VkPipelineDynamicStateCreateInfo    dynamic_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeofarray(dynamic_states),
		.pDynamicStates = dynamic_states
	};
	VkPipelineRenderingCreateInfo    render_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 0,
		.pColorAttachmentFormats = NULL,
		.depthAttachmentFormat = ctx->swapchain_depth_format
	};
	VkGraphicsPipelineCreateInfo    pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &render_info,
		.stageCount = sizeofarray(shader_stages), 
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &imput_assembly_info,
		.pViewportState = &viewport,
		.pRasterizationState = &raster_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_stencil_info,
		.pColorBlendState = NULL,
		.pDynamicState = &dynamic_state_info,
		.layout = ctx->global_pipeline_layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pipeline_shadow) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create shadow graphics pipeline\n");
		exit(1);
	}

	vkDestroyShaderModule(ctx->device, vertex, NULL);

	engine_log(LOG_FILE, "Successfully created shadow graphics pipeline");
}

void	createPBRPipeline(GraphicsContext *ctx)
{
	VkPipelineShaderStageCreateInfo	shader_stages[2] = {0};
	VkShaderModule	vertex;
	VkShaderModule	frag;
	pipelineShaderCreate(ctx, STRING_LIT("pbr"), "vertMain", VK_SHADER_STAGE_VERTEX_BIT, &shader_stages[0], &vertex);
	pipelineShaderCreate(ctx, STRING_LIT("pbr"), "fragMain", VK_SHADER_STAGE_FRAGMENT_BIT, &shader_stages[1], &frag);

	VkPipelineVertexInputStateCreateInfo	vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
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

	VkPipelineRasterizationStateCreateInfo	rasterization_info = rasterizationCreate(
		VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	VkPipelineViewportStateCreateInfo	viewport_info = viewportCreate();
	VkPipelineMultisampleStateCreateInfo	multisample_info = multisampleCreate();
	VkPipelineColorBlendAttachmentState	color_blend_attach = colorBlend(true);
	VkPipelineColorBlendStateCreateInfo	blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attach
	};
	VkDynamicState	dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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
		.stageCount = sizeofarray(shader_stages),
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &imput_assembly_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &rasterization_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_stencil_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state_info,
		.layout = ctx->global_pipeline_layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pipeline_pbr) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create graphics pipeline\n");
		exit(1);
	}

	vkDestroyShaderModule(ctx->device, vertex, NULL);
	vkDestroyShaderModule(ctx->device, frag, NULL);

	engine_log(LOG_FILE, "Successfully created graphics pipeline");
}

static void	createTEXTPipeline(GraphicsContext *ctx)
{
	VkPipelineShaderStageCreateInfo	shader_stages[2] = {0};
	VkShaderModule	vertex;
	VkShaderModule	frag;
	pipelineShaderCreate(ctx, STRING_LIT("text"), "vertMain", VK_SHADER_STAGE_VERTEX_BIT, &shader_stages[0], &vertex);
	pipelineShaderCreate(ctx, STRING_LIT("text"), "fragMain", VK_SHADER_STAGE_FRAGMENT_BIT, &shader_stages[1], &frag);

	VkPipelineVertexInputStateCreateInfo	vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
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
	VkPipelineMultisampleStateCreateInfo	multisample_info = multisampleCreate();
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
		.stageCount = sizeofarray(shader_stages),
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &imput_assembly_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &rasterization_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_stencil_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state_info,
		.layout = ctx->global_pipeline_layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pipeline_text) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create text pipeline\n");
		exit(1);
	}

	vkDestroyShaderModule(ctx->device, vertex, NULL);
	vkDestroyShaderModule(ctx->device, frag, NULL);

	engine_log(LOG_FILE, "Successfully created text pipeline");
}

static void	createGRIDPipeline(GraphicsContext *ctx)
{
	VkPipelineShaderStageCreateInfo	shader_stages[2] = {0};
	VkShaderModule	vertex;
	VkShaderModule	frag;
	pipelineShaderCreate(ctx, STRING_LIT("grid"), "vertMain", VK_SHADER_STAGE_VERTEX_BIT, &shader_stages[0], &vertex);
	pipelineShaderCreate(ctx, STRING_LIT("grid"), "fragMain", VK_SHADER_STAGE_FRAGMENT_BIT, &shader_stages[1], &frag);


	VkPipelineVertexInputStateCreateInfo	vertex_input_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	VkPipelineInputAssemblyStateCreateInfo	imput_assembly_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	};
	VkPipelineDepthStencilStateCreateInfo	depth_stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.stencilTestEnable = VK_FALSE
	};
	VkPipelineViewportStateCreateInfo	viewport_info = viewportCreate();
	VkPipelineRasterizationStateCreateInfo	rasterization_info = rasterizationCreate(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	VkPipelineMultisampleStateCreateInfo	multisample_info = multisampleCreate();
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
		.stageCount = sizeofarray(shader_stages),
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_info,
		.pInputAssemblyState = &imput_assembly_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &rasterization_info,
		.pMultisampleState = &multisample_info,
		.pDepthStencilState = &depth_stencil_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state_info,
		.layout = ctx->global_pipeline_layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pipeline_grid) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create graphics pipeline\n");
		exit(1);
	}

	vkDestroyShaderModule(ctx->device, vertex, NULL);
	vkDestroyShaderModule(ctx->device, frag, NULL);

	engine_log(LOG_FILE, "Successfully created graphics pipeline");
}

static void	createFrameResources(GraphicsContext *ctx)
{
	ctx->frames_in_flight_count = MAX_FRAMES_IN_FLIGHT;
	// TODO: Change allocation?
	ctx->frame_resources = calloc(ctx->frames_in_flight_count, sizeof(FrameResources));

	VkSemaphoreTypeCreateInfo semaphore_type_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = ctx->frames_in_flight_count
	};

	VkSemaphoreCreateInfo timeline_semaphore_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &semaphore_type_info
	};

	if (vkCreateSemaphore(ctx->device, &timeline_semaphore_info, NULL, &ctx->timeline_semaphore) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create timeline semaphore");
		exit(1);
	}

	// Single time commands
	VkCommandPoolCreateInfo single_time_pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = ctx->queue_family_index,
	};

	if (vkCreateCommandPool(ctx->device, &single_time_pool_info, NULL, &ctx->single_time_pool) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create single time command pool");
		exit(1);
	}

	for(u32 i = 0; i < ctx->frames_in_flight_count; i++) {
		FrameResources *resource = &ctx->frame_resources[i];

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

		BufferInfo instance_buffer_info = {
			.size = sizeof(EntityInstanceData) * MAX_INSTANCES,
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			.cpu_accessible = true
		};
		createVkBuffer(ctx, &instance_buffer_info, &resource->instance_buffer);

		BufferInfo text_buffer_info = {
			.size = sizeof(UiRenderInstance) * MAX_COMPONENTS,
			// Treat as storage since we read it via raw pointer in the shader
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
			.cpu_accessible = true
		};
		createVkBuffer(ctx, &text_buffer_info, &resource->text_instance_buffer);

		BufferInfo ubo_info = {
			.size = sizeof(UniformBufferObject),
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.cpu_accessible = true
		};
		createVkBuffer(ctx, &ubo_info, &resource->uniform_buffer);
	}

	engine_log(LOG_FILE, "Successfully created frame resources");
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

static void	createGlobalDescriptorLayout(GraphicsContext *ctx)
{
	VkDescriptorSetLayoutBinding	bindings[2] = {0};

	// Binding 0: Textures
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	bindings[0].descriptorCount = BINDLESS_TEXTURE_COUNT;
	bindings[0].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

	// Binding 1: Samplers
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	bindings[1].descriptorCount = GLOBAL_SAMPLER_COUNT; 
	bindings[1].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;

	// The magic flags that make bindless work
	VkDescriptorBindingFlags bindlessFlags = 
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

	VkDescriptorBindingFlags bindingFlags[2] = { bindlessFlags, bindlessFlags };

	VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = 2,
		.pBindingFlags = bindingFlags
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &flagsInfo,
		// CRITICAL: Tells Vulkan we will update this set while the GPU is running
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = 2,
		.pBindings = bindings
	};

	vkCreateDescriptorSetLayout(ctx->device, &layoutInfo, NULL, &ctx->global_descriptor_layout);

	VkDescriptorSetLayoutBinding	shadow_binding = {0};
	shadow_binding.binding = 0;
	shadow_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	shadow_binding.descriptorCount = 1; 
	shadow_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo shadow_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &shadow_binding
	};
	vkCreateDescriptorSetLayout(ctx->device, &shadow_layout_info, NULL, &ctx->shadow_descriptor_layout);

	VkDescriptorSetLayout	layouts[] = {
		ctx->global_descriptor_layout,
		ctx->shadow_descriptor_layout,
	};

	// Optional: Create the Pipeline Layout here too!
	// Every pipeline will use a push constant struct (max 128 bytes usually) + this one layout.
	VkPushConstantRange pushConstant = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = 128 // Adjust to size of your largest RootConstants struct
	};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeofarray(layouts),
		.pSetLayouts = layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstant
	};

	vkCreatePipelineLayout(ctx->device, &pipelineLayoutInfo, NULL, &ctx->global_pipeline_layout);
}

static void	createGlobalDescriptorPoolAndSet(GraphicsContext *ctx)
{
	VkDescriptorPoolSize poolSizes[] = {
		// Textures
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, BINDLESS_TEXTURE_COUNT },
		// Samplers
		{ VK_DESCRIPTOR_TYPE_SAMPLER, BINDLESS_TEXTURE_COUNT },
		// Shadow maps
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT },
	};

	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		// Must match the UPDATE_AFTER_BIND flag from the layout
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = 1 + MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = sizeofarray(poolSizes),
		.pPoolSizes = poolSizes
	};

	vkCreateDescriptorPool(ctx->device, &poolInfo, NULL, &ctx->global_descriptor_pool);

	VkDescriptorSetAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->global_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &ctx->global_descriptor_layout
	};

	vkAllocateDescriptorSets(ctx->device, &allocInfo, &ctx->global_descriptor_set);

	// Initialize an atomic counter for your texture indices
	ctx->next_free_texture_index = 0;
}

static void	createGlobalSamplers(GraphicsContext *ctx)
{
	// 1. Create PBR Sampler (Linear, Mipmapped, Repeating)
	VkSamplerCreateInfo pbr_sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias = 0.0f,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = VK_LOD_CLAMP_NONE,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.anisotropyEnable = VK_FALSE, 
		.maxAnisotropy = 1.0f
	};
	vkCreateSampler(ctx->device, &pbr_sampler_info, NULL, &ctx->default_pbr_sampler);

	// 2. Create UI/Text Sampler (Linear, No Mipmaps, Clamped to Edge)
	VkSamplerCreateInfo ui_sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mipLodBias = 0.0f,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f
	};
	vkCreateSampler(ctx->device, &ui_sampler_info, NULL, &ctx->ui_sampler);

	// 3. Write them directly to the Bindless Heap (Binding 1)
	VkDescriptorImageInfo pbr_info = {
		.sampler = ctx->default_pbr_sampler,
	};
	VkWriteDescriptorSet pbr_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = ctx->global_descriptor_set,
		.dstBinding = 1,
		.dstArrayElement = GLOBAL_SAMPLER_PBR,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
		.pImageInfo = &pbr_info,
	};

	VkDescriptorImageInfo ui_info = {
		.sampler = ctx->ui_sampler,
	};
	VkWriteDescriptorSet ui_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = ctx->global_descriptor_set,
		.dstBinding = 1,
		.dstArrayElement = GLOBAL_SAMPLER_UI,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
		.pImageInfo = &ui_info,
	};

	VkWriteDescriptorSet writes[] = {pbr_write, ui_write};
	vkUpdateDescriptorSets(ctx->device, 2, writes, 0, NULL);
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
	// End of vulkan boilerplate -------

	createGlobalDescriptorLayout(ctx);
	createGlobalDescriptorPoolAndSet(ctx);
	createGlobalSamplers(ctx);

	createShadowResources(ctx);
	createFrameResources(ctx);

	createDefaultTextures(ctx);

	// --- Pipelines --- //
	createSHADOWPipeline(ctx);
	createPBRPipeline(ctx);
	createGRIDPipeline(ctx);
	createTEXTPipeline(ctx);

	ctx->frame_index = 0;
	ctx->next_signal_value = ctx->frames_in_flight_count + 1;
}

static void	destroyVulkan(GraphicsContext *ctx)
{
	destroySyncResources(ctx);
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

static void	calculateShadowCascades(vec3 light_dir, mat4 cam_view, float cam_fov, float aspect_ratio, float near_z, float far_z, UniformBufferObject *ubo)
{
	float cascade_splits[SHADOW_MAP_CASCADE_COUNT];

	// Calculate split depths using a practical logarithmic/linear mix
	float lambda = 0.95f; // Adjust between 0 (pure linear) and 1 (pure logarithmic)
	for (u32 i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
		float p = (i + 1) / (float)SHADOW_MAP_CASCADE_COUNT;
		float log_split = near_z * powf(far_z / near_z, p);
		float lin_split = near_z + (far_z - near_z) * p;
		cascade_splits[i] = log_split * lambda + lin_split * (1.0f - lambda);
	}

	float last_split_dist = near_z;

	for (u32 i = 0; i < SHADOW_MAP_CASCADE_COUNT; i++) {
		float	split_dist	= cascade_splits[i];

		// 1. Get frustum corners for this cascade slice in view space
		vec3	frustum_corners[8] = {
			{-1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
			{-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}
		};

		mat4	cam_proj;
		glm_perspective(cam_fov, aspect_ratio, last_split_dist, split_dist, cam_proj);

		mat4	inv_cam;
		glm_mat4_mul(cam_proj, cam_view, inv_cam);
		glm_mat4_inv(inv_cam, inv_cam);

		// Transform corners to world space and find the center
		vec3	frustum_center	= {0.0f, 0.0f, 0.0f};
		for (u32 j = 0; j < 8; j++) {
			vec4	inv_corner;
			glm_mat4_mulv(inv_cam, (vec4){frustum_corners[j][0], frustum_corners[j][1], frustum_corners[j][2], 1.0f}, inv_corner);
			glm_vec3_scale(inv_corner, 1.0f / inv_corner[3], frustum_corners[j]);
			glm_vec3_add(frustum_center, frustum_corners[j], frustum_center);
		}
		glm_vec3_scale(frustum_center, 1.0f / 8.0f, frustum_center);

		// 2. Calculate the bounding sphere radius of the frustum slice
		float	radius	= 0.0f;
		for (u32 j = 0; j < 8; j++) {
			float	dist	= glm_vec3_distance(frustum_corners[j], frustum_center);
			radius		= glm_max(radius, dist);
		}
		radius	= ceilf(radius);

		// 3. Build a temporary view matrix to transform our frustum center
		vec3	light_up	= {0.0f, 1.0f, 0.0f};
		if (fabs(light_dir[0]) < 0.001f && fabs(light_dir[2]) < 0.001f) {
			light_up[0]	= 1.0f;
			light_up[1]	= 0.0f;
		}
		
		mat4	temp_light_view;
		glm_lookat((vec3){0.0f, 0.0f, 0.0f}, light_dir, light_up, temp_light_view);

		// 4. Transform center to light space and snap to texel grid
		float	shadow_map_res	= 2048.0f; // Make sure this matches your actual texture size
		float	units_per_texel	= (radius * 2.0f) / shadow_map_res;

		vec4	ls_center;
		glm_mat4_mulv(temp_light_view, (vec4){frustum_center[0], frustum_center[1], frustum_center[2], 1.0f}, ls_center);
		
		ls_center[0]	= floorf(ls_center[0] / units_per_texel) * units_per_texel;
		ls_center[1]	= floorf(ls_center[1] / units_per_texel) * units_per_texel;

		// Transform snapped center back to world space
		mat4	inv_temp_light_view;
		glm_mat4_inv(temp_light_view, inv_temp_light_view);
		
		vec4	snapped_center_4;
		glm_mat4_mulv(inv_temp_light_view, ls_center, snapped_center_4);
		
		vec3	snapped_center;
		snapped_center[0]	= snapped_center_4[0];
		snapped_center[1]	= snapped_center_4[1];
		snapped_center[2]	= snapped_center_4[2];

		// 5. Create final Light View Matrix looking at the snapped center
		vec3	light_pos;
		glm_vec3_scale(light_dir, -100.0f, light_pos); 
		glm_vec3_add(snapped_center, light_pos, light_pos);

		mat4	light_view;
		glm_lookat(light_pos, snapped_center, light_up, light_view);

		// 6. Create Orthographic Projection using the static sphere radius
		float	minX	= -radius;
		float	maxX	=  radius;
		float	minY	= -radius;
		float	maxY	=  radius;
		float	minZ	= -2000.0f;
		float	maxZ	=  radius + 200.0f;

		mat4	light_ortho;
		glm_ortho(minX, maxX, minY, maxY, minZ, maxZ, light_ortho);

		// Fix Vulkan's inverted Y-axis
		light_ortho[1][1]	*= -1.0f;

		// Manually convert Z from [-1, 1] (OpenGL) to [0, 1] (Vulkan)
		light_ortho[2][2]	= light_ortho[2][2] * 0.5f;
		light_ortho[3][2]	= light_ortho[3][2] * 0.5f + 0.5f;

		// 7. Final Light Space Matrix (Proj * View)
		glm_mat4_mul(light_ortho, light_view, ubo->light_space_matrices[i]);
		ubo->cascade_split_depths[i]	= split_dist;

		last_split_dist	= split_dist;
	}
}

static inline void	addPointLight(UniformBufferObject *ubo, vec4 p_light_pos, vec4 p_light_color)
{
	glm_vec4_copy(p_light_pos, ubo->point_lights[ubo->point_light_count].position);
	glm_vec4_copy(p_light_color, ubo->point_lights[ubo->point_light_count].color);
	ubo->point_light_count += 1;
}

static inline void	getProjectionMatrix(mat4 dst, Camera *c, float aspect_ratio, f32 near_z, f32 far_z)
{
	glm_perspective(glm_rad(c->fov), aspect_ratio, near_z, far_z, dst);
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

	f32	aspect_ratio = (float)ctx->swapchain_width / (float)ctx->swapchain_height;

	getViewMatrix(ubo.view, cam);
	getProjectionMatrix(ubo.proj, cam, aspect_ratio, cam->near_z, cam->far_z);

	// Vulkan y shift
	ubo.proj[1][1] *= -1;
	glm_mat4_inv(ubo.proj, ubo.inv_proj);
	glm_mat4_inv(ubo.view, ubo.inv_view);

	glm_vec4_copy((vec4){cam->position[0], cam->position[1], cam->position[2], 1.0f}, ubo.cam_pos);

	// --- Setup Global Sun ---
	// Pointing slightly down and to the side
	vec3 sun_dir = { 0.1f, -1.0f, -0.2f };
	glm_vec3_normalize(sun_dir);
	glm_vec4_copy((vec4){sun_dir[0], sun_dir[1], sun_dir[2], 0.0f}, ubo.sun_direction);

	// Warm sunlight, intensity 5.0
	glm_vec4_copy((vec4){1.0f, 0.95f, 0.8f, 2.0f}, ubo.sun_color);

	ubo.exposure = 1.0f;
	ubo.gamma = 2.2f;

	calculateShadowCascades(sun_dir, ubo.view, glm_rad(cam->fov), aspect_ratio, cam->near_z, cam->far_z, &ubo);

	memcpy(resource->uniform_buffer.mapped, &ubo, sizeof(UniformBufferObject));
}

// »speed
// Cache instance data instead of recalculating in pbr pass
void SHADOWPass(GraphicsContext *ctx, EntityRenderInfo entity_info, FrameResources *resource, u8 frame_idx)
{
	// --- 1. PRE-PASS PIPELINE BARRIER ---
	ImageTransitionInfo pre_transition = {
		.image = ctx->shadow_maps[frame_idx].image.image,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
		.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		.srcAccessMask = VK_ACCESS_2_NONE,
		.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.pRange = NULL
	};
	cmdTransitionImage(resource->cmd_buf, &pre_transition);

	// --- 2. RENDER SETUP ---
	const u32 total_entity_count = entity_info.entity_count;
	EntityInstanceData *instance_data_buf = (EntityInstanceData *)resource->instance_buffer.mapped;

	// Fetch BDA Pointers
	const u64 ubo_addr = resource->uniform_buffer.device_address;
	const u64 instance_addr = resource->instance_buffer.device_address;

	vkCmdBindPipeline(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_shadow);

	VkViewport viewport = {0.0f, 0.0f, (float)SHADOW_MAP_RESOLUTION, (float)SHADOW_MAP_RESOLUTION, 0.0f, 1.0f};
	VkRect2D scissor = {{0, 0}, {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION}};
	vkCmdSetViewport(resource->cmd_buf, 0, 1, &viewport);
	vkCmdSetScissor(resource->cmd_buf, 0, 1, &scissor);
	vkCmdSetDepthBias(resource->cmd_buf, 1.25f, 0.0f, 1.75f);

	// --- 3. CASCADE DRAW LOOP ---
	for (u32 c = 0; c < SHADOW_MAP_CASCADE_COUNT; c++) {

		VkRenderingAttachmentInfo depth_attachment = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = ctx->shadow_maps[frame_idx].cascade_views[c],
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue.depthStencil = {1.0f, 0}
		};

		VkRenderingInfo render_info = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = scissor,
			.layerCount = 1,
			.colorAttachmentCount = 0,
			.pDepthAttachment = &depth_attachment,
		};

		vkCmdBeginRendering(resource->cmd_buf, &render_info);

		u32 instance_cursor = 0;
		u32 e_idx = 0;

		while (e_idx < total_entity_count) {
			u16 model_idx = entity_info.data[e_idx].model_idx;
			const Model *model = entity_info.models[model_idx];

			u32 run_start = e_idx;
			u32 run_end = e_idx + 1;
			while (run_end < total_entity_count && model_idx == entity_info.data[run_end].model_idx) run_end++;
			u32 run_count = run_end - run_start;

			for (u32 n = 0; n < model->node_count; n++) {
				Node *node = &model->linear_nodes[n];
				for (u32 m = 0; m < node->mesh_count; m++) {
					const Mesh *mesh = &node->meshes[m];

					if (mesh->vertex_count == 0) continue;

					u32 first_instance = instance_cursor;

					for (u32 r = 0; r < run_count; r++) {
						EntityRenderData *e_data = &entity_info.data[run_start + r];
						glm_mat4_mul(e_data->instance_data.model_mat, node->world_transform, instance_data_buf[instance_cursor].model_mat);
						instance_cursor++;
					}

					// Populate BDA Push Constants
					ShadowRootConstants push_constants = {
						.ubo_addr = ubo_addr,
						.instance_addr = instance_addr,
						.vertex_addr = mesh->gpu_vertex_data.device_address,
						.cascade_index = c
					};

					vkCmdPushConstants(resource->cmd_buf, ctx->global_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShadowRootConstants), &push_constants);
					vkCmdBindIndexBuffer(resource->cmd_buf, mesh->gpu_index_data.handle, 0, mesh->index_type);
					vkCmdDrawIndexed(resource->cmd_buf, mesh->index_count, run_count, 0, 0, first_instance);
				}
			}

			e_idx = run_end;
		}

		vkCmdEndRendering(resource->cmd_buf);
	}

	// --- 4. POST-PASS PIPELINE BARRIER ---
	ImageTransitionInfo post_transition = {
		.image = ctx->shadow_maps[frame_idx].image.image,
		.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
		.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 
		.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
		.pRange = NULL
	};
	cmdTransitionImage(resource->cmd_buf, &post_transition);
}

void	TEXTPass(GraphicsContext *ctx, FrameResources *resource, UiRenderInfo info)
{
	const VkCommandBuffer cmd = resource->cmd_buf;

	// CPU Upload
	memcpy(resource->text_instance_buffer.mapped, info.render_instances, info.upload_size);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_text);

	// Populate Push Constants
	TextRootConstants push_constants = {
		.window_size = {ctx->window_width, ctx->window_height},
		.px_range = info.px_range,
		.instance_addr = resource->text_instance_buffer.device_address,
		.atlas_tex_idx = ctx->font_atlas_global_index // Set when you load the font!
	};

	vkCmdPushConstants(cmd, ctx->global_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TextRootConstants), &push_constants);

	// Draw without binding vertex buffers or descriptors
	vkCmdDraw(cmd, 6, info.instance_count, 0, 0);
}

void	GRIDPass(GraphicsContext *ctx, FrameResources *resource)
{
	vkCmdBindPipeline(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_grid);

	// Populate Push Constants
	GridRootConstants push_constants = {
		.ubo_addr = resource->uniform_buffer.device_address,
		.properties = ctx->grid_properties,
		.show_grid = gameStateQuery(g_game_state, ShowGrid),
	};

	// Need both Vertex (for UBO ptr) and Fragment (for properties) stages
	vkCmdPushConstants(resource->cmd_buf, ctx->global_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GridRootConstants), &push_constants);

	// Grid is usually drawn as a full-screen quad or large plane without vertex buffers
	vkCmdDraw(resource->cmd_buf, 3, 1, 0, 0);
}

void	PBRPass(GraphicsContext *ctx, EntityRenderInfo entity_info, FrameResources *resource, u8 frame_idx)
{
	const u32 total_entity_count = entity_info.entity_count;
	EntityInstanceData *instance_data_buf = (EntityInstanceData *)resource->instance_buffer.mapped;
	u32 instance_cursor = 0;

	// Fetch the 64-bit pointers for this frame's global buffers
	const u64 ubo_addr = resource->uniform_buffer.device_address;
	const u64 instance_addr = resource->instance_buffer.device_address;

	vkCmdBindPipeline(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_pbr);
	vkCmdBindDescriptorSets(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->global_pipeline_layout, 1, 1, &ctx->shadow_maps[frame_idx].descriptor_set, 0, NULL);

	u32 e_idx = 0;
	while (e_idx < total_entity_count) {
		u16 model_idx = entity_info.data[e_idx].model_idx;
		const Model *model = entity_info.models[model_idx];

		// Find total number of entities using this model
		u32 run_start = e_idx;
		u32 run_end = e_idx + 1;
		while (run_end < total_entity_count && model_idx == entity_info.data[run_end].model_idx) run_end++;
		u32 run_count = run_end - run_start;

		for (u32 n = 0; n < model->node_count; n++) {
			Node *node = &model->linear_nodes[n];
			for (u32 m = 0; m < node->mesh_count; m++) {
				const Mesh *mesh = &node->meshes[m];

				if (mesh->vertex_count == 0) continue;

				u32 first_instance = instance_cursor;
				for (u32 r = 0; r < run_count; r++) {
					EntityRenderData *e_data = &entity_info.data[run_start + r];
					glm_mat4_mul(e_data->instance_data.model_mat, node->world_transform, instance_data_buf[instance_cursor].model_mat);
					instance_cursor++;
				}

				// 1. Initialize the Root Constants with BDA pointers and default material values
				PBRRootConstants push_constants = {
					.ubo_addr = ubo_addr,
					.instance_addr = instance_addr,
					.vertex_addr = mesh->gpu_vertex_data.device_address,

					.base_color_tex = -1,
					.metallic_roughness_tex = -1,
					.normal_tex = -1,
					.occlusion_tex = -1,
					.emissive_tex = -1,

					.base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f},
					.metallic_factor = 0.0f,
					.roughness_factor = 0.8f,
					.alpha_cutoff = 0.5f
				};

				// 2. Populate Bindless Texture Indices and Factors
				if (mesh->material_index >= 0) {
					Material *mat = &model->materials[mesh->material_index];

					push_constants.base_color_tex = mat->base_color_tex_idx;
					push_constants.metallic_roughness_tex = mat->metallic_roughness_tex_idx;
					push_constants.normal_tex = mat->normal_tex_idx;
					push_constants.occlusion_tex = mat->occlusion_tex_idx;
					push_constants.emissive_tex = mat->emissive_tex_idx;

					push_constants.roughness_factor = mat->roughness_factor;
					push_constants.metallic_factor = mat->metallic_factor;
					push_constants.alpha_cutoff = mat->alpha_cutoff;
					glm_vec4_ucopy(mat->base_color_factor, push_constants.base_color_factor);
				}

				// 3. Push to BOTH Vertex (for pointers) and Fragment (for textures/factors) stages
				vkCmdPushConstants(resource->cmd_buf, ctx->global_pipeline_layout, 
						VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
						0, sizeof(PBRRootConstants), &push_constants);

				// 4. Bind Index Buffer and Draw
				vkCmdBindIndexBuffer(resource->cmd_buf, mesh->gpu_index_data.handle, 0, mesh->index_type);
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


	// ---- Shadow Pass ------------------ //
	vkCmdBindDescriptorSets(resource->cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, 
			 ctx->global_pipeline_layout, // (See note below!)
			 0, 1, &ctx->global_descriptor_set, 
			 0, NULL);

	SHADOWPass(ctx, entity_info, resource, frame_res_index);
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
		GRIDPass(ctx, resource);

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
