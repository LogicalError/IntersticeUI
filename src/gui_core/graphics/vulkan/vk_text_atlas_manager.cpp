#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/graphics/gui_graphics_context.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_image.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_text_atlas_manager.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>

static constexpr tachyon::Diagnostics::LogCategory kLogVulkan = tachyon::Diagnostics::MakeLogCategory("vulkan");

bool tachyon::vulkan::VulkanTextAtlasManager::Initialize(tachyon::GraphicsContext* inGraphicsContext, tachyon::ImageSet* inImageSet)
{
	this->graphicsContext = static_cast<tachyon::vulkan::VulkanGraphicsContext*>(inGraphicsContext);
	this->imageSet        = static_cast<tachyon::vulkan::VulkanImageSet*>(inImageSet);

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

tachyon::TextureIndex tachyon::vulkan::VulkanTextAtlasManager::CreateTexture(const uint8_t* imageData, tachyon::uint2 imageSize, uint8_t bpp)
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
		fontTextures.emplace_back(tachyon::vulkan::FontTexture {});
	}

	tachyon::vulkan::FontTexture* texture = &fontTextures[tex_index];
	if (texture->image != nullptr)
	{
		graphicsContext->deviceContext.DestroyImageAtEnd(texture->image, tachyon::DeviceScope::Frame);
	}

	assert(bpp == 1 || bpp == 4);
	// Glyphs draw at their rasterized size, so pages need no mip chain.
	auto image = graphicsContext->deviceContext.CreateImage(imageData, imageSize,
															 (bpp == 1) ? tachyon::ImageFormat::R8 : tachyon::ImageFormat::R8G8B8A8,
															 tachyon::Sampler::LinearClampToEdge);
	tachyon::vulkan::VulkanImage* vulkanImage = (tachyon::vulkan::VulkanImage*)image;
	VkDescriptorImageInfo descriptorInfo =
	{
		.sampler = vulkanImage->imageSampler,
		.imageView = vulkanImage->imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	*texture = tachyon::vulkan::FontTexture
	{
		.descriptorInfo = descriptorInfo,
		.image = image,
		.size = imageSize,
		.bpp = bpp,
		.imageIndex = texture->imageIndex,
		.used = true,
	};

	int textureIndex = tex_index + 1;
	return tachyon::TextureIndex(textureIndex);
}

void tachyon::vulkan::VulkanTextAtlasManager::UpdateTexture(tachyon::TextureIndex textureIndex, const uint8_t* imageData, tachyon::uint2 imageSize)
{
	if (textureIndex == tachyon::TextureIndex::Invalid)
	{
		TACHYON_LOG_ERROR(kLogVulkan, "UpdateTexture: invalid textureIndex");
		return;
	}

	int tex_index = ((uint32_t)textureIndex) - 1;
	if (tex_index < 0 || size_t(tex_index) >= fontTextures.size())
	{
		TACHYON_LOG_ERROR(kLogVulkan, "UpdateTexture: texture out of range");
		return;
	}

	auto* texture = &fontTextures[tex_index];
	if (texture->image != nullptr)
	{
		graphicsContext->deviceContext.DestroyImageAtEnd(texture->image, tachyon::DeviceScope::Frame);
	}
    auto bpp = texture->bpp;

	assert(bpp == 1 || bpp == 4);
	// Glyphs draw at their rasterized size, so pages need no mip chain.
	auto image = graphicsContext->deviceContext.CreateImage(imageData, imageSize,
															 (bpp == 1) ? tachyon::ImageFormat::R8 : tachyon::ImageFormat::R8G8B8A8,
															 tachyon::Sampler::LinearClampToEdge);
	tachyon::vulkan::VulkanImage* vulkanImage = (tachyon::vulkan::VulkanImage*)image;
	VkDescriptorImageInfo descriptorInfo =
	{
		.sampler = vulkanImage->imageSampler,
		.imageView = vulkanImage->imageView,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	};
	*texture = tachyon::vulkan::FontTexture
	{
		.descriptorInfo = descriptorInfo,
		.image = image,
		.size = imageSize,
		.bpp = bpp,
		.imageIndex = texture->imageIndex,
		.used = true,
	};
}

tachyon::ImageIndex tachyon::vulkan::VulkanTextAtlasManager::GetImageIndex(tachyon::TextureIndex textureIndex)
{
	if (textureIndex == tachyon::TextureIndex::Invalid)
	{
		TACHYON_LOG_ERROR(kLogVulkan, "GetImageIndex: invalid textureIndex");
		return tachyon::ImageIndex::Invalid;
	}
	uint32_t tex_index = ((uint32_t)textureIndex) - 1;
	if (tex_index >= fontTextures.size())
	{
		TACHYON_LOG_ERROR(kLogVulkan, "GetImageIndex: text_id out of range");
		return tachyon::ImageIndex::Invalid;
	}

	auto& image_index = fontTextures[tex_index].imageIndex;
	if (!fontTextures[tex_index].used)
	{
		TACHYON_LOG_ERROR(kLogVulkan, "GetImageIndex: font texture not used");
		return tachyon::ImageIndex::Invalid;
	}

	if (image_index == tachyon::ImageIndex::Invalid)
	{
		image_index = imageSet->ReserveIndex();
	}
	return image_index;
}

void tachyon::vulkan::VulkanTextAtlasManager::SyncTextureIndices()
{
	for (size_t f = 0; f < fontTextures.size(); ++f)
	{
		tachyon::vulkan::FontTexture& fontTexture = fontTextures[f];
		if (fontTexture.imageIndex == tachyon::ImageIndex::Invalid)
		{
			fontTexture.imageIndex = imageSet->ReserveIndex();
		}
		imageSet->SetDescriptor(fontTexture.imageIndex, fontTexture.descriptorInfo);
	}
}
