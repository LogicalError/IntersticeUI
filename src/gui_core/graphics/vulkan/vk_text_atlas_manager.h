#pragma once

#include <gui_core/text/gui_text_atlas_manager.h>

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

	class VulkanGraphicsContext;
	class VulkanImageSet;

	struct FontTexture
	{
		VkDescriptorImageInfo descriptorInfo {};
		tachyon::Image*		image { nullptr };
		tachyon::uint2		size { 0,0 };
		int32_t			bpp { 0 };
		tachyon::ImageIndex	imageIndex { tachyon::ImageIndex::Invalid };
		bool			used = false;
	};

	class VulkanTextAtlasManager : public tachyon::TextAtlasManager
	{
	public:
		std::vector<FontTexture>       fontTextures;
		tachyon::vulkan::VulkanGraphicsContext* graphicsContext = nullptr;
		tachyon::vulkan::VulkanImageSet*        imageSet        = nullptr;

	public:
		bool Initialize(tachyon::GraphicsContext* graphicsContext, tachyon::ImageSet* imageSet) override;

		tachyon::TextureIndex CreateTexture(const uint8_t* imageData, tachyon::uint2 imageSize, uint8_t bpp) override;
		void UpdateTexture(tachyon::TextureIndex textureIndex, const uint8_t* imageData, tachyon::uint2 imageSize) override;

		tachyon::ImageIndex GetImageIndex(tachyon::TextureIndex textureIndex) override;
		void SyncTextureIndices() override;
	};
    /// @}
}
