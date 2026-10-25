#pragma once

#include <gui_core/graphics/gui_image.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace interstice::vulkan
{
    class VulkanImage : public interstice::Image
    {
    public:
        VkImage       image;
        VkImageView   imageView;
        VmaAllocation allocation;
        VkExtent3D    imageExtent;
        VkFormat      imageFormat;
        VkSampler     imageSampler;

        // How it was made, to make it again on a new device after the old one is lost;
        // with its pixels when it was made from them.
        struct Recipe
        {
            VkImageUsageFlags     usage     = 0;
            interstice::Sampler   sampler   = interstice::Sampler::UNDEFINED;
            bool                  mipmapped = false;
            std::vector<uint8_t>  pixels;
        } recipe;
    };

    class VulkanImageSet : public interstice::ImageSet
    {
    public:
        // Size of the shader's bindless texture array.
        static constexpr uint32_t kCapacity = 256;

        std::vector<VkDescriptorImageInfo> textures;
        std::vector<VulkanImage*>          owners;     // the image behind each index, or null

        // Points every index back at its image, after the images were made again.
        void RefreshDescriptors();

    public:
        void Clear() override;
        interstice::ImageIndex Add(interstice::Image* image) override;
        void Remove(interstice::ImageIndex index) override;
        bool SetImage(interstice::ImageIndex index, VulkanImage* image);
        bool SetDescriptor(interstice::ImageIndex index, const VkDescriptorImageInfo& descriptor);
        interstice::ImageIndex ReserveIndex();

    private:
        interstice::ImageIndex Allocate(const VkDescriptorImageInfo& descriptor);
        bool IsInRange(interstice::ImageIndex index) const;

        std::vector<interstice::ImageIndex> m_freeIndices;
    };
}