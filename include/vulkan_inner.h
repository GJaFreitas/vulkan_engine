#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>

#ifdef __cplusplus

#define EXTERNC extern "C"
#include <cstdlib>
#include <cstdio>

#else
#define EXTERNC
#endif



EXTERNC void	*initializeVMA(VkPhysicalDevice physical_device, VkDevice device, VkInstance instance);
EXTERNC void	destroyVMA(void *vma_allocator);
EXTERNC VkResult	wrapperVMAcreateImage(void *_allocator, VkImageCreateInfo *img_info, VkImage *img, void *img_allocation);
EXTERNC void		wrapperVMAdestroyImage(void *_allocator, VkImage img, void *allocation);
