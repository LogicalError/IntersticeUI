#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_graphics_context.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_image.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_text_atlas_manager.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

static constexpr interstice::Diagnostics::LogCategory kLogVulkan = interstice::Diagnostics::MakeLogCategory("vulkan");

bool interstice::vulkan::VulkanTextAtlasManager::Initialize(interstice::GraphicsContext* inGraphicsContext, interstice::ImageSet* inImageSet)
{
	this->graphicsContext = static_cast<interstice::vulkan::VulkanGraphicsContext*>(inGraphicsContext);
	this->imageSet        = static_cast<interstice::vulkan::VulkanImageSet*>(inImageSet);

	graphicsContext->deviceContext.mainDeletionQueue.
		push_function([this]()
		{
			for (size_t f = 0; f < fontTextures.size(); f++)
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

interstice::TextureIndex interstice::vulkan::VulkanTextAtlasManager::CreateTexture(const uint8_t* imageData, interstice::uint2 imageSize, uint8_t bpp)
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
		fontTextures.emplace_back(interstice::vulkan::FontTexture {});
	}

	interstice::vulkan::FontTexture* texture = &fontTextures[tex_index];
	if (texture->image != nullptr)
	{
		graphicsContext->deviceContext.DestroyImageAtEnd(texture->image, interstice::DeviceScope::Frame);
	}

	assert(bpp == 1 || bpp == 4);
	// Glyphs draw at their rasterized size, so pages need no mip chain.
	auto image = graphicsContext->deviceContext.CreateImage(imageData, imageSize,
															 (bpp == 1) ? interstice::ImageFormat::R8 : interstice::ImageFormat::R8G8B8A8,
															 interstice::Sampler::LinearClampToEdge);
	interstice::vulkan::VulkanImage* vulkanImage = (interstice::vulkan::VulkanImage*)image;
	VkDescriptorImageInfo descriptorInfo =
	{
		.sampler = vulkanImage->imageSampler,
		.imageView = vulkanImage->imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	*texture = interstice::vulkan::FontTexture
	{
		.descriptorInfo = descriptorInfo,
		.image = image,
		.size = imageSize,
		.bpp = bpp,
		.imageIndex = texture->imageIndex,
		.used = true,
	};

	int textureIndex = tex_index + 1;
	return interstice::TextureIndex(textureIndex);
}

void interstice::vulkan::VulkanTextAtlasManager::UpdateTexture(interstice::TextureIndex textureIndex, const uint8_t* imageData, interstice::uint2 imageSize)
{
	if (textureIndex == interstice::TextureIndex::Invalid)
	{
		INTERSTICE_LOG_ERROR(kLogVulkan, "UpdateTexture: invalid textureIndex");
		return;
	}

	int tex_index = ((uint32_t)textureIndex) - 1;
	if (tex_index < 0 || size_t(tex_index) >= fontTextures.size())
	{
		INTERSTICE_LOG_ERROR(kLogVulkan, "UpdateTexture: texture out of range");
		return;
	}

	auto* texture = &fontTextures[tex_index];
	if (texture->image != nullptr)
	{
		graphicsContext->deviceContext.DestroyImageAtEnd(texture->image, interstice::DeviceScope::Frame);
	}
    auto bpp = texture->bpp;

	assert(bpp == 1 || bpp == 4);
	// Glyphs draw at their rasterized size, so pages need no mip chain.
	auto image = graphicsContext->deviceContext.CreateImage(imageData, imageSize,
															 (bpp == 1) ? interstice::ImageFormat::R8 : interstice::ImageFormat::R8G8B8A8,
															 interstice::Sampler::LinearClampToEdge);
	interstice::vulkan::VulkanImage* vulkanImage = (interstice::vulkan::VulkanImage*)image;
	VkDescriptorImageInfo descriptorInfo =
	{
		.sampler = vulkanImage->imageSampler,
		.imageView = vulkanImage->imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	*texture = interstice::vulkan::FontTexture
	{
		.descriptorInfo = descriptorInfo,
		.image = image,
		.size = imageSize,
		.bpp = bpp,
		.imageIndex = texture->imageIndex,
		.used = true,
	};
}

interstice::ImageIndex interstice::vulkan::VulkanTextAtlasManager::GetImageIndex(interstice::TextureIndex textureIndex)
{
	if (textureIndex == interstice::TextureIndex::Invalid)
	{
		INTERSTICE_LOG_ERROR(kLogVulkan, "GetImageIndex: invalid textureIndex");
		return interstice::ImageIndex::Invalid;
	}
	uint32_t tex_index = ((uint32_t)textureIndex) - 1;
	if (tex_index >= fontTextures.size())
	{
		INTERSTICE_LOG_ERROR(kLogVulkan, "GetImageIndex: text_id out of range");
		return interstice::ImageIndex::Invalid;
	}

	auto& image_index = fontTextures[tex_index].imageIndex;
	if (!fontTextures[tex_index].used)
	{
		INTERSTICE_LOG_ERROR(kLogVulkan, "GetImageIndex: font texture not used");
		return interstice::ImageIndex::Invalid;
	}

	if (image_index == interstice::ImageIndex::Invalid)
	{
		image_index = imageSet->ReserveIndex();
	}
	return image_index;
}

void interstice::vulkan::VulkanTextAtlasManager::SyncTextureIndices()
{
	for (size_t f = 0; f < fontTextures.size(); ++f)
	{
		interstice::vulkan::FontTexture& fontTexture = fontTextures[f];
		if (fontTexture.imageIndex == interstice::ImageIndex::Invalid)
		{
			fontTexture.imageIndex = imageSet->ReserveIndex();
		}
		imageSet->SetDescriptor(fontTexture.imageIndex, fontTexture.descriptorInfo);
	}
}
