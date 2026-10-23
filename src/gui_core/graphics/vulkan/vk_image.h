#pragma once

#include <gui_core/graphics/gui_image.h>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

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
    };

    class VulkanImageSet : public interstice::ImageSet
    {
    public:
        // Size of the shader's bindless texture array.
        static constexpr uint32_t kCapacity = 256;

        std::vector<VkDescriptorImageInfo> textures;

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