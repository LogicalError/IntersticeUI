#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_graphics_context.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_image.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_text_atlas_manager.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

static constexpr GUI::Diagnostics::LogCategory kLogVulkan = GUI::Diagnostics::MakeLogCategory("vulkan");

bool Vulkan::VulkanTextAtlasManager::Initialize(GUI::GraphicsContext* inGraphicsContext, GUI::ImageSet* inImageSet)
{
	this->graphicsContext = static_cast<Vulkan::VulkanGraphicsContext*>(inGraphicsContext);
	this->imageSet        = static_cast<Vulkan::VulkanImageSet*>(inImageSet);

	graphicsContext->deviceContext.mainDeletionQueue.
		push_function([this]()
		{
			for (auto f = 0; f < fontTextures.size(); f++)
			{
				if (fontTextures[f].used)
				{
					graphicsContext->deviceContext.DestroyImage(fontTextures[f].image);
					delete fontTextures[f].image;
					fontTextures[f].image = nullptr;
					fontTextures[f].used = false;
				}
			}
			fontTextures.clear();
		});
    return true;
}

GUI::TextureIndex Vulkan::VulkanTextAtlasManager::CreateTexture(const uint8_t* imageData, GUI::uint2 imageSize, uint8_t bpp)
{
	int tex_index = -1;
	for (int i = 0; i < (int)fontTextures.size(); i++)
	{
		if (fontTextures[i].used)
			continue;
		tex_index = i;
		break;
	}

	if (tex_index == -1)
	{
		tex_index = (int)fontTextures.size();
		fontTextures.emplace_back(Vulkan::FontTexture {});
	}

	Vulkan::FontTexture* texture = &fontTextures[tex_index];
	if (texture->image != nullptr)
	{
		graphicsContext->deviceContext.DestroyImageAtEnd(texture->image, GUI::DeviceScope::Frame);
	}

	assert(bpp == 1 || bpp == 4);
	// Glyphs draw at their rasterized size, so pages need no mip chain.
	auto image = graphicsContext->deviceContext.CreateImage(imageData, imageSize,
															 (bpp == 1) ? GUI::ImageFormat::R8 : GUI::ImageFormat::R8G8B8A8,
															 GUI::Sampler::LinearClampToEdge);
	Vulkan::VulkanImage* vulkanImage = (Vulkan::VulkanImage*)image;
	VkDescriptorImageInfo descriptorInfo =
	{
		.sampler = vulkanImage->imageSampler,
		.imageView = vulkanImage->imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	*texture = Vulkan::FontTexture
	{
		.descriptorInfo = descriptorInfo,
		.image = image,
		.size = imageSize,
		.bpp = bpp,
		.imageIndex = texture->imageIndex,
		.used = true,
	};

	int textureIndex = tex_index + 1;
	return GUI::TextureIndex(textureIndex);
}

void Vulkan::VulkanTextAtlasManager::UpdateTexture(GUI::TextureIndex textureIndex, const uint8_t* imageData, GUI::uint2 imageSize)
{
	if (textureIndex == GUI::TextureIndex::Invalid)
	{
		LOG_ERROR(kLogVulkan, "UpdateTexture: invalid textureIndex");
		return;
	}

	int tex_index = ((uint32_t)textureIndex) - 1;
	if (tex_index >= fontTextures.size())
	{
		LOG_ERROR(kLogVulkan, "UpdateTexture: texture out of range");
		return;
	}

	auto* texture = &fontTextures[tex_index];
	if (texture->image != nullptr)
	{
		graphicsContext->deviceContext.DestroyImageAtEnd(texture->image, GUI::DeviceScope::Frame);
	}
    auto bpp = texture->bpp;

	assert(bpp == 1 || bpp == 4);
	// Glyphs draw at their rasterized size, so pages need no mip chain.
	auto image = graphicsContext->deviceContext.CreateImage(imageData, imageSize,
															 (bpp == 1) ? GUI::ImageFormat::R8 : GUI::ImageFormat::R8G8B8A8,
															 GUI::Sampler::LinearClampToEdge);
	Vulkan::VulkanImage* vulkanImage = (Vulkan::VulkanImage*)image;
	VkDescriptorImageInfo descriptorInfo =
	{
		.sampler = vulkanImage->imageSampler,
		.imageView = vulkanImage->imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	*texture = Vulkan::FontTexture
	{
		.descriptorInfo = descriptorInfo,
		.image = image,
		.size = imageSize,
		.bpp = bpp,
		.imageIndex = texture->imageIndex,
		.used = true,
	};
}

GUI::ImageIndex Vulkan::VulkanTextAtlasManager::GetImageIndex(GUI::TextureIndex textureIndex)
{
	if (textureIndex == GUI::TextureIndex::Invalid)
	{
		LOG_ERROR(kLogVulkan, "GetImageIndex: invalid textureIndex");
		return GUI::ImageIndex::Invalid;
	}
	uint32_t tex_index = ((uint32_t)textureIndex) - 1;
	if (tex_index >= fontTextures.size())
	{
		LOG_ERROR(kLogVulkan, "GetImageIndex: text_id out of range");
		return GUI::ImageIndex::Invalid;
	}

	auto& image_index = fontTextures[tex_index].imageIndex;
	if (!fontTextures[tex_index].used)
	{
		LOG_ERROR(kLogVulkan, "GetImageIndex: font texture not used");
		return GUI::ImageIndex::Invalid;
	}

	if (image_index == GUI::ImageIndex::Invalid)
	{
		image_index = imageSet->ReserveIndex();
	}
	return image_index;
}

void Vulkan::VulkanTextAtlasManager::SyncTextureIndices()
{
	for (size_t f = 0; f < fontTextures.size(); ++f)
	{
		Vulkan::FontTexture& fontTexture = fontTextures[f];
		if (fontTexture.imageIndex == GUI::ImageIndex::Invalid)
		{
			fontTexture.imageIndex = imageSet->ReserveIndex();
		}
		imageSet->SetDescriptor(fontTexture.imageIndex, fontTexture.descriptorInfo);
	}
}
