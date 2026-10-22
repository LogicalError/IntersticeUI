#pragma once

#include <gui_core/graphics/gui_image.h>

namespace Vulkan
{
    class VulkanImage : public GUI::Image
    {
    public:
        VkImage       image;
        VkImageView   imageView;
        VmaAllocation allocation;
        VkExtent3D    imageExtent;
        VkFormat      imageFormat;
        VkSampler     imageSampler;
    };

    class VulkanImageSet : public GUI::ImageSet
    {
    public:
        // Size of the shader's bindless texture array.
        static constexpr uint32_t kCapacity = 256;

        std::vector<VkDescriptorImageInfo> textures;

    public:
        void Clear() override;
        GUI::ImageIndex Add(GUI::Image* image) override;
        void Remove(GUI::ImageIndex index) override;
        bool SetImage(GUI::ImageIndex index, VulkanImage* image);
        bool SetDescriptor(GUI::ImageIndex index, const VkDescriptorImageInfo& descriptor);
        GUI::ImageIndex ReserveIndex();

    private:
        GUI::ImageIndex Allocate(const VkDescriptorImageInfo& descriptor);
        bool IsInRange(GUI::ImageIndex index) const;

        std::vector<GUI::ImageIndex> m_freeIndices;
    };
}