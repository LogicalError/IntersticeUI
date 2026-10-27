
#pragma once 

#include <vulkan/vulkan.h>

namespace tachyon::vulkan 
{
	/// Records a layout transition of `image` into `cmd`.
	/// `afterStage`: for an image coming from UNDEFINED, a stage the transition waits for. A
	/// swapchain image passes its acquire semaphore's wait stage, so the transition is ordered
	/// after the acquire instead of racing the presentation engine.
	void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout,
	                      VkPipelineStageFlags2 afterStage = VK_PIPELINE_STAGE_2_NONE);
	void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination,VkExtent2D srcSize, VkExtent2D dstSize);
	void generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize);
}
