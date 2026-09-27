#include "graphics_layer.h"
#include "world.h"

// Extraction Macros
#define STATE_GET_SHADER(s)	((u16)((s >> SHIFT_SHADER) & 0xFFFF))
#define STATE_GET_CULL(s)	((u8) ((s >> SHIFT_CULL)   & 0x000F))
#define STATE_GET_BLEND(s)	((u8) ((s >> SHIFT_BLEND)  & 0x000F))
#define STATE_GET_DEPTH(s)	((u8) ((s >> SHIFT_DEPTH)  & 0x000F))
#define STATE_GET_TOPO(s)	((u8) ((s >> SHIFT_TOPO)   & 0x000F))
#define STATE_GET_TYPE(s)	(((s) >> SHIFT_TYPE) & 1U)
#define STATE_GET_PASS(s)	((u8) (((s) >> SHIFT_PASS) & 0x000F))
#define STATE_GET_COMPARE(s)	((u8) (((s) >> SHIFT_COMPARE) & 0x000F))

VkPipeline	pipelineRegistryGetOrCreate(PipelineRegistry *registry, PipelineState state)
{
	// 1. Check Cache first (Exact same lookup as graphics pipelines)
	for (u32 i = 0; i < registry->pipeline_count; ++i) {
		if (registry->cached_states[i] == state) {
			return registry->cached_pipelines[i];
		}
	}

	u16 shader_id = STATE_GET_SHADER(state);
	ShaderRecord *shader = &registry->shaders[shader_id];
	VkPipeline new_pipeline = VK_NULL_HANDLE;

	// 2. Branch based on Pipeline Type
	if (STATE_GET_TYPE(state) == PIPELINE_TYPE_COMPUTE) {
		
		VkComputePipelineCreateInfo compute_info = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = shader->stages[0],
			.layout = registry->g_layout
		};

		vkCreateComputePipelines(
			registry->device, 
			VK_NULL_HANDLE, 
			1, 
			&compute_info, 
			NULL, 
			&new_pipeline
		);

	} else {
		enum CullMode		cull_mode = STATE_GET_CULL(state);
		enum BlendMode		blend_mode = STATE_GET_BLEND(state);
		enum DepthMode		depth_mode = STATE_GET_DEPTH(state);
		enum CompareMode	compare_mode = STATE_GET_COMPARE(state);
		enum TopologyMode	topo_mode = STATE_GET_TOPO(state);
		enum PassType		pass_type = STATE_GET_PASS(state);


		// --- RASTERIZER ---
		VkPipelineRasterizationStateCreateInfo rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = (cull_mode == CULL_BACK) ? VK_CULL_MODE_BACK_BIT : 
			(cull_mode == CULL_FRONT) ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		if (pass_type == PASS_SHADOW) {
			rasterizer.depthBiasEnable = VK_TRUE;
		}

		// --- DEPTH STENCIL ---
		VkPipelineDepthStencilStateCreateInfo depth_stencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		VkCompareOp vk_compare = VK_COMPARE_OP_LESS;
		switch (compare_mode) {
			case COMPARE_LESS:		vk_compare = VK_COMPARE_OP_LESS; break;
			case COMPARE_LESS_OR_EQUAL:	vk_compare = VK_COMPARE_OP_LESS_OR_EQUAL; break;
			case COMPARE_GREATER:		vk_compare = VK_COMPARE_OP_GREATER; break;
			case COMPARE_GREATER_OR_EQUAL:	vk_compare = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
			case COMPARE_EQUAL:		vk_compare = VK_COMPARE_OP_EQUAL; break;
			case COMPARE_ALWAYS:		vk_compare = VK_COMPARE_OP_ALWAYS; break;
		}
		if (depth_mode == DEPTH_RW) {
			depth_stencil.depthTestEnable = VK_TRUE;
			depth_stencil.depthWriteEnable = VK_TRUE;
			depth_stencil.depthCompareOp = vk_compare;
		} else if (depth_mode == DEPTH_READ) {
			depth_stencil.depthTestEnable = VK_TRUE;
			depth_stencil.depthWriteEnable = VK_FALSE;
			depth_stencil.depthCompareOp = vk_compare;
		} else {
			depth_stencil.depthTestEnable = VK_FALSE;
			depth_stencil.depthWriteEnable = VK_FALSE;
		}
		depth_stencil.stencilTestEnable = VK_FALSE;

		// --- COLOR BLEND ---
		VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
		color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		if (blend_mode == BLEND_ALPHA) {
			color_blend_attachment.blendEnable = VK_TRUE;

			// Color blending math: (SourceColor * SourceAlpha) + (DestColor * (1 - SourceAlpha))
			color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;

			// Alpha channel blending math: (SourceAlpha * 1) + (DestAlpha * 0)
			color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

		} else if (blend_mode == BLEND_ADDITIVE) {
			color_blend_attachment.blendEnable = VK_TRUE;

			// Color blending math: SourceColor + DestColor
			color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;

			// Alpha channel blending math
			color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

		} else {
			color_blend_attachment.blendEnable = VK_FALSE;
		}

		VkPipelineColorBlendStateCreateInfo color_blending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		color_blending.attachmentCount = 1;
		color_blending.pAttachments = &color_blend_attachment;

		// --- TOPOLOGY ---
		VkPipelineInputAssemblyStateCreateInfo input_assembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		if (topo_mode == TOPO_LINES) {
			input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		} else if (topo_mode == TOPO_POINTS) {
			input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		} else if (topo_mode == TOPO_TRIANGLE_STRIP) {
			input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		} else {
			input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}

		// --- BINDLESS VERTEX INPUT (Gloriously Empty!) ---
		VkPipelineVertexInputStateCreateInfo vertex_input = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertex_input.vertexBindingDescriptionCount = 0;
		vertex_input.vertexAttributeDescriptionCount = 0;

		// --- DYNAMIC STATES ---
		u32	dynamic_state_count = 3;
		VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS };
		VkPipelineDynamicStateCreateInfo dynamic_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamic_state.dynamicStateCount = dynamic_state_count;
		dynamic_state.pDynamicStates = dynamic_states;

		// --- VIEWPORT STATE (Using Dynamic States) ---
		// Because we use dynamic viewport and scissor, we don't need to provide the 
		// actual dimensions here, just state that we are using exactly one of each.
		VkPipelineViewportStateCreateInfo viewport_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport_state.viewportCount = 1;
		viewport_state.scissorCount = 1;

		// --- MULTISAMPLING ---
		// If your engine uses MSAA, you would read this from your registry/graphics config.
		// For standard rendering, it defaults to 1 sample.
		VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // e.g., registry->msaa_samples
		multisampling.minSampleShading = 1.0f;
		multisampling.pSampleMask = NULL;
		multisampling.alphaToCoverageEnable = VK_FALSE;
		multisampling.alphaToOneEnable = VK_FALSE;

		// --- FINAL CREATION ---
		VkGraphicsPipelineCreateInfo pipeline_info = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

		// Slang compiled stages (VS, FS)
		pipeline_info.stageCount = shader->stage_count;
		pipeline_info.pStages = shader->stages; 

		// Bindless Vertex Input (Empty arrays)
		pipeline_info.pVertexInputState = &vertex_input;

		// Explicitly assign all the populated state structs
		pipeline_info.pInputAssemblyState = &input_assembly;
		pipeline_info.pViewportState = &viewport_state;
		pipeline_info.pRasterizationState = &rasterizer;
		pipeline_info.pMultisampleState = &multisampling;
		pipeline_info.pDepthStencilState = &depth_stencil;
		pipeline_info.pColorBlendState = &color_blending;
		pipeline_info.pDynamicState = &dynamic_state;

		// Layout and Render Pass
		pipeline_info.layout = registry->g_layout;
		pipeline_info.subpass = 0;

		// Pipeline Derivatives (Optimization for creating similar pipelines, not strictly needed)
		pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
		pipeline_info.basePipelineIndex = -1;

		// --- DYNAMIC RENDERING COLOR/DEPTH FORMATS ---
		VkPipelineRenderingCreateInfo rendering_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

		if (pass_type == PASS_SHADOW) {
			// Depth Only
			rendering_info.colorAttachmentCount = 0;
			rendering_info.pColorAttachmentFormats = NULL;
			rendering_info.depthAttachmentFormat = registry->shadow_depth_format;
		} 
		else if (pass_type == PASS_UI) {
			// Color + Depth
			rendering_info.colorAttachmentCount = 1;
			rendering_info.pColorAttachmentFormats = &registry->main_color_format;
			rendering_info.depthAttachmentFormat = registry->main_depth_format;
		}
		else { // PASS_MAIN
			// Color + Depth
			rendering_info.colorAttachmentCount = 1;
			rendering_info.pColorAttachmentFormats = &registry->main_color_format;
			
			rendering_info.depthAttachmentFormat = registry->main_depth_format;
		}

		pipeline_info.pNext = &rendering_info;

		if (vkCreateGraphicsPipelines(registry->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &new_pipeline) != VK_SUCCESS) {
			engine_error(LOG_FILE, "Failed to create pipeline with shader name: %S\n", shader->filename);
		}

	}

	// 3. Cache and return
	u32 index = registry->pipeline_count++;
	registry->cached_states[index] = state;
	registry->cached_pipelines[index] = new_pipeline;

	return new_pipeline;
}

VkShaderModule	createShaderModule(String shader_name, shaderc_shader_kind kind, VkDevice device)
{

	u8	buf[128] = "shaders/compiled/";
	String	filename = { .data = buf, .count = 17 };
	// full shader path: shaders/compiled/

	memcpy(buf + filename.count, shader_name.data, shader_name.count);
	filename.count += shader_name.count;
	// full shader path: shaders/compiled/{shader_name}

	// TODO: This is going to change of course
	memcpy(buf + filename.count, ".spv", 4);
	filename.count += 4;
	// full shader path: shaders/compiled/{shader_name}.spv

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
	if (vkCreateShaderModule(device, &shader_info, NULL, &module) != VK_SUCCESS) {
		engine_error(LOG_FILE, "Failed to create shader module: %S\n", filename);
		exit(1);
	}
	return module;
}

ShaderHandle	pipelineRegistryRegisterShader(PipelineRegistry *registry, String shader_name, enum PipelineType type, VkShaderStageFlagBits stage_flags)
{
	for (u32 i = 0; i < registry->shader_count; ++i) {
		ShaderRecord *record = &registry->shaders[i];

		// If the filepath and entry points match, return the existing ID
		if (strEq(record->filename, shader_name))
		{
			return (ShaderHandle)i; 
		}
	}

	ShaderRecord	*shader_rec = &registry->shaders[registry->shader_count];
	shader_rec->filename = strDup(shader_name, &g_perm_arena);
	shader_rec->stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_rec->stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shader_rec->stages[0].pName = shader_rec->pNames[0];
	shader_rec->stages[1].pName = shader_rec->pNames[1];

	if (type == PIPELINE_TYPE_COMPUTE) {
		shader_rec->stages[0].stage = VK_SHADER_STAGE_COMPUTE_BIT;
		shader_rec->stages[0].module = createShaderModule(shader_rec->filename, shaderc_compute_shader, registry->device);
		memcpy(shader_rec->pNames[0], "computeMain", 12);
		shader_rec->stage_count = 1;
	} else {

		if (stage_flags & VK_SHADER_STAGE_VERTEX_BIT) {
			shader_rec->stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			shader_rec->stages[0].module = createShaderModule(shader_rec->filename, shaderc_vertex_shader, registry->device);
			memcpy(shader_rec->pNames[0], "vertMain", 9);
			shader_rec->stage_count += 1;
		}
		if (stage_flags & VK_SHADER_STAGE_FRAGMENT_BIT) {
			shader_rec->stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			shader_rec->stages[1].module = createShaderModule(shader_rec->filename, shaderc_fragment_shader, registry->device);
			memcpy(shader_rec->pNames[1], "fragMain", 9);
			shader_rec->stage_count += 1;
		}
	}

	ShaderHandle	handle = registry->shader_count;
	registry->shader_count++;
	return handle;
}

void	initPipelineRegistry(PipelineRegistry **reg_out, GraphicsContext *ctx)
{
	PipelineRegistry	*reg = g_perm_arena.fp_allocation(&g_perm_arena,
						    sizeof(PipelineRegistry),
						    DEFAULT_ALIGN);

	reg->device = ctx->device;
	reg->main_color_format = ctx->swapchain_format.format;
	reg->main_depth_format = ctx->swapchain_depth_format;
	reg->shadow_depth_format = ctx->swapchain_depth_format;
	ctx->pbr_shader_handle = pipelineRegistryRegisterShader(reg, STRING_LIT("pbr"), PIPELINE_TYPE_GRAPHICS, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	ctx->grid_shader_handle = pipelineRegistryRegisterShader(reg, STRING_LIT("grid"), PIPELINE_TYPE_GRAPHICS, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	ctx->ui_shader_handle = pipelineRegistryRegisterShader(reg, STRING_LIT("text"), PIPELINE_TYPE_GRAPHICS, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	ctx->shadow_shader_handle = pipelineRegistryRegisterShader(reg, STRING_LIT("shadows"), PIPELINE_TYPE_GRAPHICS, VK_SHADER_STAGE_VERTEX_BIT);
	ctx->grid_pipeline_state = createPipelineState(ctx->grid_shader_handle,
							CULL_NONE,
							BLEND_ALPHA,
							DEPTH_READ,
							COMPARE_LESS_OR_EQUAL,
							TOPO_TRIANGLES,
							PIPELINE_TYPE_GRAPHICS,
							PASS_MAIN
						);
	ctx->ui_pipeline_state = createPipelineState(ctx->ui_shader_handle,
							CULL_NONE,
							BLEND_ALPHA,
							DEPTH_NONE,
							COMPARE_LESS,
							TOPO_TRIANGLE_STRIP,
							PIPELINE_TYPE_GRAPHICS,
							PASS_UI
						);
	ctx->shadow_pipeline_state = createPipelineState(ctx->shadow_shader_handle,
							CULL_BACK,
							BLEND_ALPHA,
							DEPTH_RW,
							COMPARE_LESS,
							TOPO_TRIANGLES,
							PIPELINE_TYPE_GRAPHICS,
							PASS_SHADOW
						);

	*reg_out = reg;
}
