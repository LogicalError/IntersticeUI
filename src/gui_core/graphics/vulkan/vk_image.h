#pragma once

#include <gui_core/graphics/gui_image.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

    class VulkanImage : public tachyon::Image
    {
    public:
        VkImage       image;
        VkImageView   imageView;
        VmaAllocation allocation;
        VkExtent3D    imageExtent;
        VkFormat      imageFormat;
        VkSampler     imageSampler;

        /// How it was made, to make it again on a new device after the old one is lost;
        /// with its pixels when it was made from them.
        struct Recipe
        {
            VkImageUsageFlags     usage     = 0;
            tachyon::Sampler   sampler   = tachyon::Sampler::UNDEFINED;
            bool                  mipmapped = false;
            std::vector<uint8_t>  pixels;
        } recipe;
    };

    class VulkanImageSet : public tachyon::ImageSet
    {
    public:
        /// Size of the shader's bindless texture array.
        static constexpr uint32_t kCapacity = 256;

        std::vector<VkDescriptorImageInfo> textures;
        std::vector<VulkanImage*>          owners;     ///< the image behind each index, or null

        /// Points every index back at its image, after the images were made again.
        void RefreshDescriptors();

    public:
        void Clear() override;
        tachyon::ImageIndex Add(tachyon::Image* image) override;
        void Remove(tachyon::ImageIndex index) override;
        bool SetImage(tachyon::ImageIndex index, VulkanImage* image);
        bool SetDescriptor(tachyon::ImageIndex index, const VkDescriptorImageInfo& descriptor);
        tachyon::ImageIndex ReserveIndex();

    private:
        tachyon::ImageIndex Allocate(const VkDescriptorImageInfo& descriptor);
        bool IsInRange(tachyon::ImageIndex index) const;

        std::vector<tachyon::ImageIndex> m_freeIndices;
    };
    /// @}
}