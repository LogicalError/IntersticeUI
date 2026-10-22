#pragma once

#include <gui_core/text/gui_text_atlas_manager.h>

namespace Vulkan
{
	class VulkanGraphicsContext;
	class VulkanImageSet;

	struct FontTexture
	{
		VkDescriptorImageInfo descriptorInfo {};
		GUI::Image*		image { nullptr };
		GUI::uint2		size { 0,0 };
		int32_t			bpp { 0 };
		GUI::ImageIndex	imageIndex { GUI::ImageIndex::Invalid };
		bool			used = false;
	};

	class VulkanTextAtlasManager : public GUI::TextAtlasManager
	{
	public:
		std::vector<FontTexture>       fontTextures;
		Vulkan::VulkanGraphicsContext* graphicsContext = nullptr;
		Vulkan::VulkanImageSet*        imageSet        = nullptr;

	public:
		bool Initialize(GUI::GraphicsContext* graphicsContext, GUI::ImageSet* imageSet) override;

		GUI::TextureIndex CreateTexture(const uint8_t* imageData, GUI::uint2 imageSize, uint8_t bpp) override;
		void UpdateTexture(GUI::TextureIndex textureIndex, const uint8_t* imageData, GUI::uint2 imageSize) override;

		GUI::ImageIndex GetImageIndex(GUI::TextureIndex textureIndex) override;
		void SyncTextureIndices() override;
	};
}
