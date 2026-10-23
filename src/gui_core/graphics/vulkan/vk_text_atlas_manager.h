#pragma once

#include <gui_core/text/gui_text_atlas_manager.h>

namespace interstice::vulkan
{
	class VulkanGraphicsContext;
	class VulkanImageSet;

	struct FontTexture
	{
		VkDescriptorImageInfo descriptorInfo {};
		interstice::Image*		image { nullptr };
		interstice::uint2		size { 0,0 };
		int32_t			bpp { 0 };
		interstice::ImageIndex	imageIndex { interstice::ImageIndex::Invalid };
		bool			used = false;
	};

	class VulkanTextAtlasManager : public interstice::TextAtlasManager
	{
	public:
		std::vector<FontTexture>       fontTextures;
		interstice::vulkan::VulkanGraphicsContext* graphicsContext = nullptr;
		interstice::vulkan::VulkanImageSet*        imageSet        = nullptr;

	public:
		bool Initialize(interstice::GraphicsContext* graphicsContext, interstice::ImageSet* imageSet) override;

		interstice::TextureIndex CreateTexture(const uint8_t* imageData, interstice::uint2 imageSize, uint8_t bpp) override;
		void UpdateTexture(interstice::TextureIndex textureIndex, const uint8_t* imageData, interstice::uint2 imageSize) override;

		interstice::ImageIndex GetImageIndex(interstice::TextureIndex textureIndex) override;
		void SyncTextureIndices() override;
	};
}
