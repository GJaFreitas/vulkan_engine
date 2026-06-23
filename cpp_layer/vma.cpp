#define VK_NO_PROTOTYPES
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_USE_VULKAN_H_FROM_VOLK

#include <volk.h>
#include "vk_mem_alloc.h"

extern "C" void	*initializeVMA(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance)
{

	VmaVulkanFunctions vma_func_info {};
	vma_func_info.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vma_func_info.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo vma_alloc_info {
			.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
			.physicalDevice = physical_device,
			.device = device,
			.pVulkanFunctions = &vma_func_info,
			.instance = instance,
			.vulkanApiVersion = VK_API_VERSION_1_4
		};

	vmaImportVulkanFunctionsFromVolk(&vma_alloc_info, &vma_func_info);

	VmaAllocator	vma_allocator = VK_NULL_HANDLE;
	if (vmaCreateAllocator(&vma_alloc_info, &vma_allocator) != VK_SUCCESS) {
		return NULL;
	}

	return vma_allocator;
}

extern "C" void	destroyVMA(void *vma_allocator)
{
	VmaAllocator	vma = (VmaAllocator)vma_allocator;

	vmaDestroyAllocator(vma);
}

extern "C" VkResult	wrapperVMAcreateImage(void *_allocator, VkImageCreateInfo *img_info, VkImage *img, void *img_allocation)
{
	VkResult	result;
	VmaAllocator	allocator = (VmaAllocator)_allocator;

	VmaAllocationCreateInfo alloc_info = {
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO
	};
	result = vmaCreateImage(allocator, img_info, &alloc_info, img, (VmaAllocation *)img_allocation, NULL);
	return result;
}

extern "C" void		wrapperVMAdestroyImage(void *_allocator, VkImage img, void *allocation)
{
	vmaDestroyImage((VmaAllocator)_allocator, img, (VmaAllocation)allocation);
}
