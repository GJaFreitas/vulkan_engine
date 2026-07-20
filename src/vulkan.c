#include "vulkan_inner.h"
#include "graphics_layer.h"

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
		engine_error("vulkan", "%i - %s: %s\n", callback_data->messageIdNumber, callback_data->pMessageIdName, callback_data->pMessage);
	}
	return VK_FALSE;
}

static void	createInstance(GraphicsContext *ctx)
{
	if (volkInitialize() != VK_SUCCESS) {
		engine_error("vulkan", "Failed to initialized volk\n");
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
	engine_log("vulkan", "Extensions ---------------");
	for (u32 i = 0; i < extensionCount + ctx->required_extension_count; i++) {
		if (i < extensionCount) {
			extensions[i] = strdup(sdl_extensions[i]);
		} else {
			extensions[i] = strdup(ctx->required_extensions[i - extensionCount]);
		}
		engine_log("vulkan", "%s", extensions[i]);
	}
	engine_log("vulkan", "Extensions ---------------");

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
		engine_error("vulkan", "Failed to create vulkan instance\n");
		exit(1);
	}


	engine_log("vulkan", "Successfully created vulkan instance");
}

static void	createSurface(GraphicsContext *ctx)
{
	engine_log("vulkan", "SDL video driver: %s", SDL_GetCurrentVideoDriver());
	if (!SDL_Vulkan_CreateSurface(ctx->window, ctx->vk_instance, NULL, &ctx->surface)) {
		engine_error("vulkan", "Failed to create surface, error: %s\n", SDL_GetError());
		exit(1);
	}
	engine_log("vulkan", "Successfully created surface");
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
	VkPhysicalDeviceFeatures2	supported_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &supported_features12
	};
	vkGetPhysicalDeviceFeatures2(chosen_phys_device, &supported_features);

	if (!supported_features13.dynamicRendering) {
		engine_error("vulkan", "Card doesnt support dynamic rendering\n");
		exit(1);
	}
	if (!supported_features13.synchronization2) {
		engine_error("vulkan", "Card doesnt support sync2\n");
		exit(1);
	}
	if (!supported_features12.timelineSemaphore) {
		engine_error("vulkan", "Card doesnt support timeline semaphore\n");
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
	VkPhysicalDeviceFeatures2	features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &features12,
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
		engine_error("vulkan", "Failed to create logical device");
		exit(1);
	}

	engine_log("vulkan", "Successfully created logical device");

	vkGetDeviceQueue(ctx->device, queue_family_index, 0, &ctx->queue);
}

static void	createSwapchain(GraphicsContext *ctx, u32 width, u32 height)
{
	ctx->swapchain_width = width;
	ctx->swapchain_height = height;

	VkSurfaceCapabilitiesKHR	surface_capabilities = {0};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->phys_device, ctx->surface, &surface_capabilities) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to get surface capabilities\n");
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
		engine_error("vulkan", "Failed to create swapchain\n");
	}
	engine_log("vulkan", "Successfully created swapchain");

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
			engine_error("vulkan", "Failed to create image view nr: %u\n", i);
			exit(1);
		}
	}

	VkSemaphore	*semaphores = malloc(sizeof(VkSemaphore) * image_count);
	for (u32 i = 0; i < image_count; i++) {
		VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};

		if (vkCreateSemaphore(ctx->device, &semaphore_info, NULL, &semaphores[i]) != VK_SUCCESS) {
			engine_error("vulkan", "Failed to create semaphore nr: %u\n", i);
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

	if (wrapperVMAcreateImage(ctx->vma_allocator, &depth_info, &ctx->swapchain_depth_image, &ctx->swapchain_depth_image_allocation) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to allocate depth buffer\n");
		exit(1);
	}

	VkImageViewCreateInfo depth_view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = ctx->swapchain_depth_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = ctx->swapchain_depth_format,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.levelCount = 1,
			.layerCount = 1
		}
	};

	if (vkCreateImageView(ctx->device, &depth_view_info, NULL, &ctx->swapchain_depth_image_view) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to create depth image view\n");
		exit(1);
	}

	engine_log("vulkan", "Successfully created depth buffer");

	ctx->image_count = image_count;
	ctx->swapchain_images = images;
	ctx->swapchain_image_views = image_views;
	ctx->render_complete_sequence_semaphores = semaphores;
}

static VkShaderModule	createShaderModule(String filename, shaderc_shader_kind kind, GraphicsContext *ctx)
{

	StringView	extension = filename;

	// extension is at '.'
	stringViewJumpToChar(&extension, '.');
	// extension after '.'
	stringViewAdvance(&extension, 1);

	String shader_code = readFile(filename);
	engine_log("vulkan", "Compiling shader: %S", filename);

	u64	shader_size;
	u32	*shader_data;

	// Shader is already compiled
	if (stringIsEqual(extension, STRING_LIT("spv"))) {
		shader_data = (u32 *)shader_code.data;
		shader_size = shader_code.count;
	} else {
		char	*filename_cstring = stringToCstr(filename, NULL);

		shaderc_compiler_t		compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t	opts = shaderc_compile_options_initialize();
		shaderc_compile_options_set_target_env(opts, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
		shaderc_compile_options_set_target_spirv(opts, shaderc_spirv_version_1_6);
		shaderc_compile_options_set_optimization_level(opts, shaderc_optimization_level_performance);
		shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, (char *)shader_code.data, shader_code.count, kind, filename_cstring, "main", opts);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) {
			engine_error("vulkan", "Shader compilation error: %s\n", shaderc_result_get_error_message(result));
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
		engine_error("vulkan", "Failed to create shader module: %S\n", filename);
		exit(1);
	}
	return module;
}

static void	createShaders(GraphicsContext *ctx)
{
	ctx->vertex_shader = createShaderModule(STRING_LIT("shaders/compiled/slang.spv"), shaderc_vertex_shader, ctx);
	ctx->frag_shader = ctx->vertex_shader;
}

static void	createGraphicsPipeline(GraphicsContext *ctx)
{
	VkPushConstantRange	push_constant_ranges[] = {
		// Mat push constant
		{
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(MaterialProperties),
		},
		// Model matrix
		{
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			.offset = sizeof(MaterialProperties),
			.size = sizeof(mat4),
		},
	};

	VkDescriptorSetLayout	layouts[] = {
		ctx->ubo_descriptor_layout,
		ctx->material_descriptor_layout
	};

	VkPipelineLayoutCreateInfo	layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeofarray(layouts),
		.pSetLayouts = layouts,
		.pushConstantRangeCount = sizeofarray(push_constant_ranges),
		.pPushConstantRanges = push_constant_ranges,
	};


	if (vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &ctx->pbr_pipeline_layout) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to create pipeline layout\n");
		exit(1);
	}

	VkPipelineShaderStageCreateInfo	shader_stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = ctx->vertex_shader,
			.pName = "vertMain"
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = ctx->frag_shader,
			.pName = "fragMain"
		}
	};


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

	VkPipelineViewportStateCreateInfo	viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = NULL,
		.scissorCount = 1,
		.pScissors = NULL
	};

	VkPipelineRasterizationStateCreateInfo	rasterization_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f
	};

	VkPipelineMultisampleStateCreateInfo	multisample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	VkPipelineColorBlendAttachmentState	color_blend_attach = {
		.blendEnable = VK_TRUE,
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
		.layout = ctx->pbr_pipeline_layout,
		.renderPass = VK_NULL_HANDLE
	};

	if (vkCreateGraphicsPipelines(ctx->device, NULL, 1, &pipeline_info, NULL, &ctx->pbr_pipeline) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to create graphics pipeline\n");
		exit(1);
	}

	engine_log("vulkan", "Successfully created graphics pipeline");
}

static void	createSyncResources(GraphicsContext *ctx)
{
	ctx->frames_in_flight_count = MAX_FRAMES_IN_FLIGHT;
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
		engine_error("vulkan", "Failed to create timeline semaphore");
		exit(1);
	}

	for (u32 i = 0; i < ctx->frames_in_flight_count; i++) {
		VkSemaphoreCreateInfo semaphore_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
		};

		if (vkCreateSemaphore(ctx->device, &semaphore_info, NULL,
			&ctx->frame_resources[i].image_acquired_semaphore) != VK_SUCCESS) {
			engine_error("vulkan", "Failed to create semaphore for frame resources nr: %u\n", i);
			exit(1);
		}
	}

	engine_log("vulkan", "Successfully created sync resources");
}

static void	createCommandBuffers(GraphicsContext *ctx)
{
	for(u32 i = 0; i < ctx->frames_in_flight_count; i++) {
		FrameResources	*resource = &ctx->frame_resources[i];

		VkCommandPoolCreateInfo pool_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = ctx->queue_family_index
		};

		if (vkCreateCommandPool(ctx->device, &pool_info, NULL, &resource->command_pool) != VK_SUCCESS) {
			engine_error("vulkan", "Failed to create command pool nr: %u", i);
			exit(1);
		}

		VkCommandBufferAllocateInfo buffer_alloc_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = resource->command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1
		};

		if (vkAllocateCommandBuffers(ctx->device, &buffer_alloc_info, &resource->command_buffer) != VK_SUCCESS) {
			engine_error("vulkan", "Failed to create command buffer nr: %u", i);
			exit(1);
		}
	}

	engine_log("vulkan", "Successfully created command buffers");
}

static void	destroyShaders(GraphicsContext *ctx)
{
	vkDestroyShaderModule(ctx->device, ctx->vertex_shader, NULL);
	vkDestroyShaderModule(ctx->device, ctx->frag_shader, NULL);
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

	vkDestroyImageView(ctx->device, ctx->swapchain_depth_image_view, NULL);
	wrapperVMAdestroyImage(ctx->vma_allocator, ctx->swapchain_depth_image, ctx->swapchain_depth_image_allocation);
	ctx->swapchain_depth_image_view = NULL;
}

static void	destroySyncResources(GraphicsContext *ctx)
{
	for (u32 i = 0; i < ctx->frames_in_flight_count; i++) {
		vkDestroySemaphore(ctx->device, ctx->frame_resources[i].image_acquired_semaphore, NULL);
		vkDestroyCommandPool(ctx->device, ctx->frame_resources[i].command_pool, NULL);
	}
	vkDestroySemaphore(ctx->device, ctx->timeline_semaphore, NULL);
}

static void	createDescriptorSetLayout(GraphicsContext *ctx)
{
	VkDescriptorSetLayoutBinding	layout_bindings[] = {
		// UBO binding
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	};

	VkDescriptorSetLayoutCreateInfo	create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeofarray(layout_bindings),
		.pBindings = layout_bindings,
	};

	if (vkCreateDescriptorSetLayout(ctx->device, &create_info, NULL, &ctx->ubo_descriptor_layout) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to create ubo layout");
	}
}

static void	createUniformBuffers(GraphicsContext *ctx)
{
	for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkBufferCreateInfo	buf_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.size = sizeof(UniformBufferObject)
		};

		wrapperVMAcreateBuffer(ctx->vma_allocator, &buf_info, &ctx->uniform_buffers[i], &ctx->uniform_buffer_allocations[i], 1);
		wrapperVMAmapMemory(ctx->vma_allocator, ctx->uniform_buffer_allocations[i], &ctx->uniform_buffers_mapped[i]);

		UniformBufferObject	ubo = {};

		glm_vec4_copy((vec4){-10.0f,  10.0f, 10.0f, 1.0f}, ubo.light_positions[0]);
		glm_vec4_copy((vec4){ 10.0f,  10.0f, 10.0f, 1.0f}, ubo.light_positions[1]);
		glm_vec4_copy((vec4){-10.0f, -10.0f, 10.0f, 1.0f}, ubo.light_positions[2]);
		glm_vec4_copy((vec4){ 10.0f, -10.0f, 10.0f, 1.0f}, ubo.light_positions[3]);

		glm_vec4_copy((vec4){300.0f, 300.0f, 300.0f, 1.0f}, ubo.light_colors[0]);
		glm_vec4_copy((vec4){300.0f, 300.0f,   0.0f, 1.0f}, ubo.light_colors[1]);
		glm_vec4_copy((vec4){  0.0f,   0.0f, 300.0f, 1.0f}, ubo.light_colors[2]);
		glm_vec4_copy((vec4){300.0f,   0.0f,   0.0f, 1.0f}, ubo.light_colors[3]);

		memcpy(ctx->uniform_buffers_mapped[i], &ubo, sizeof(UniformBufferObject));
	}
}

static void	createDescriptorPoolSets(GraphicsContext *ctx)
{
	const	u32	material_descriptor_count = 1000;
	VkDescriptorPoolSize	pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = material_descriptor_count,
		}
	};

	VkDescriptorPoolCreateInfo	create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = MAX_FRAMES_IN_FLIGHT + material_descriptor_count,
		.poolSizeCount = sizeofarray(pool_sizes),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(ctx->device, &create_info, NULL, &ctx->descriptor_pool) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to create descriptor pool");
	}

	VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
	for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		layouts[i] = ctx->ubo_descriptor_layout;
	}

	VkDescriptorSetAllocateInfo	alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->descriptor_pool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = layouts,
	};

	if (vkAllocateDescriptorSets(ctx->device, &alloc_info, ctx->ubo_descriptor_sets) != VK_SUCCESS) {
		engine_error("vulkan", "Failed to allocate descriptor sets");
		exit(1);
	}

	for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo	buf_info = {
			.buffer = ctx->uniform_buffers[i],
			.offset = 0,
			.range = sizeof(UniformBufferObject),
		};

		VkWriteDescriptorSet	descriptor_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = ctx->ubo_descriptor_sets[i],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buf_info,
		};

		vkUpdateDescriptorSets(ctx->device, 1, &descriptor_write, 0, NULL);
	}

	engine_log("vulkan", "Successfully created descriptor pools and sets");
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
		engine_error("vulkan", "Failed to create vma allocator\n");
		exit(1);
	}
	createSwapchain(ctx, ctx->window_width, ctx->window_height);
	createDescriptorSetLayout(ctx);
	createSyncResources(ctx);
	createUniformBuffers(ctx);
	createDescriptorPoolSets(ctx);
	createCommandBuffers(ctx);
	createDefaultTextures(ctx);
	createMaterialDescriptorSetLayout(ctx);
	createShaders(ctx);
	createGraphicsPipeline(ctx);
	ctx->frame_index = 0;
	ctx->next_signal_value = ctx->frames_in_flight_count + 1;
}

static void	destroyVulkan(GraphicsContext *ctx)
{
	destroySyncResources(ctx);
	vkDestroyPipelineLayout(ctx->device, ctx->pbr_pipeline_layout, NULL);
	vkDestroyPipeline(ctx->device, ctx->pbr_pipeline, NULL);
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
		engine_error("vulkan", "Couldnt create window");
		exit(1);
	} else {
		engine_log("vulkan", "SDL window created");
	}
}

static inline void	getProjectionMatrix(mat4 dst, Camera *c, float aspect_ratio)
{
	glm_perspective(glm_rad(c->zoom), aspect_ratio, 0.1f, 10.0f, dst);
}

static inline void	getViewMatrix(mat4 dst, Camera *c)
{
	vec3	center;
	glm_vec3_add(c->position, c->front, center);
	glm_lookat(c->position, center, c->up, dst);
}

static void	updateUniformBuffer(GraphicsContext *ctx, u32 frame_idx, Camera *cam)
{

	UniformBufferObject ubo = {};
	glm_mat4_identity(ubo.model);
	getViewMatrix(ubo.view, cam);
	getProjectionMatrix(ubo.proj, cam, (float)ctx->swapchain_width / (float)ctx->swapchain_height);

	// Vulkan y shift
	ubo.proj[1][1] *= -1;

	glm_vec4_copy((vec4){cam->position[0], cam->position[1], cam->position[2], 1.0f}, ubo.cam_pos);
	ubo.exposure = 4.5f;
	ubo.gamma = 2.2f;

	memcpy(ctx->uniform_buffers_mapped[frame_idx], &ubo, sizeof(UniformBufferObject));
}

void	render(GraphicsContext *ctx, Camera *camera)
{
	if (ctx->swapchain_require_recreate)
	{
		vkDeviceWaitIdle(ctx->device);
		destroySwapchain(ctx);
		createSwapchain(ctx, ctx->window_width, ctx->window_height);
		ctx->swapchain_require_recreate = false;
	}

	// engine_log("vulkan", "frame index: %u, next_signal_value: %lu, frames_in_flight_count: %u\n", ctx->frame_index, ctx->next_signal_value, ctx->frames_in_flight_count);
	const u32	frame_res_index = ctx->frame_index++ % ctx->frames_in_flight_count;
	const u64	signal_value = ctx->next_signal_value++;
	const u64	wait_value = signal_value - ctx->frames_in_flight_count;
	// engine_log("vulkan", "res_index: %u, signal_value: %lu, wait_value: %lu\n", frame_res_index, signal_value, wait_value);

	updateUniformBuffer(ctx, frame_res_index, camera);

	VkSemaphoreWaitInfo	wait_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &ctx->timeline_semaphore,
		.pValues = &wait_value
	};
	vkWaitSemaphores(ctx->device, &wait_info, UINT64_MAX);

	FrameResources	resource = ctx->frame_resources[frame_res_index];
	vkResetCommandPool(ctx->device, resource.command_pool, 0);

	VkSemaphore	image_acquire_semaphore = resource.image_acquired_semaphore;

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
	vkBeginCommandBuffer(resource.command_buffer, &cmd_begin_info);

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
		.image = ctx->swapchain_depth_image,
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
	vkCmdPipelineBarrier2(resource.command_buffer, &dep_info);

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
		.imageView = ctx->swapchain_depth_image_view,
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


	vkCmdBeginRendering(resource.command_buffer, &render_info);
	{
		VkViewport	viewport = {
			.x = 0, .y = 0,
			.width = ctx->swapchain_width,
			.height = ctx->swapchain_height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(resource.command_buffer, 0, 1, &viewport);

		VkRect2D	scissor = {
			.offset = {0, 0},
			.extent = { .width = ctx->swapchain_width, .height = ctx->swapchain_height, }
		};
		vkCmdSetScissor(resource.command_buffer, 0, 1, &scissor);

		vkCmdBindPipeline(resource.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pbr_pipeline);
		VkDeviceSize	offset = 0;

		// »speed
		for (u32 i = 0; i < ctx->model.node_count; i++) {
			const Mesh	*mesh = &ctx->model.linear_nodes[i].mesh;
			const Node	*node = &ctx->model.linear_nodes[i];

			if (mesh->vertex_count == 0)
				continue ;

			mat4	node_transform;
			glm_mat4_copy(node->world_transform, node_transform);

			VkDescriptorSet	descriptor_sets[2] = { ctx->ubo_descriptor_sets[frame_res_index], VK_NULL_HANDLE };
			MaterialProperties	push_constants_mat = {
				.basecolor_texture_set = -1,
				.physical_descriptor_texture_set = -1,
				.normal_texture_set = -1,
				.occlusion_texture_set = -1,
				.emissive_texture_set = -1,
			};
			u32		descriptor_set_count = 1;
			Material	*mat;
			if (mesh->material_index >= 0) {
				mat = &ctx->model.materials[mesh->material_index];

				descriptor_set_count += 1;
				descriptor_sets[1] = mat->descriptor_set;

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
			} else {
				mat = NULL;
			}

			vkCmdPushConstants(resource.command_buffer, ctx->pbr_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MaterialProperties), &push_constants_mat);
			vkCmdPushConstants(resource.command_buffer, ctx->pbr_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(MaterialProperties), sizeof(mat4), &node_transform);

			vkCmdBindDescriptorSets(resource.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pbr_pipeline_layout, 0, descriptor_set_count, descriptor_sets, 0, NULL);
			vkCmdBindVertexBuffers(resource.command_buffer, 0, 1, &mesh->gpu_vertex_data, &offset);
			vkCmdBindIndexBuffer(resource.command_buffer, mesh->gpu_index_data, 0, mesh->index_type);

			vkCmdDrawIndexed(resource.command_buffer, mesh->index_count, 1, 0, 0, 0);
		}
	}
	vkCmdEndRendering(resource.command_buffer);

	VkImageMemoryBarrier2	present_layout_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.image = ctx->swapchain_images[img_idx],
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	VkDependencyInfo	present_dep_info = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &present_layout_barrier
	};
	vkCmdPipelineBarrier2(resource.command_buffer, &present_dep_info);

	vkEndCommandBuffer(resource.command_buffer);

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
		.commandBuffer = resource.command_buffer,
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
