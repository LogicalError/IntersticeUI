#include "gpu_test_harness.h"

#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_image.h>

#include <vector>

using namespace GpuTestHarness;

// Smoke test: a real device boots and reports no validation errors. Proves the
// harness (recording callback + sync-validation) is wired correctly.
TEST_F(GpuTest, DeviceInitializesWithoutValidationErrors)
{
    EXPECT_NE(vk->deviceContext.device, static_cast<VkDevice>(VK_NULL_HANDLE));
    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}

// The per-frame writer pushes textures.size() into the single-image layout's
// texture array; more than 16 must not trip
// VUID-VkWriteDescriptorSet-dstArrayElement.
TEST_F(GpuTest, TextureDescriptorWriteBeyondSixteenIsValidationClean)
{
    auto& dc = vk->deviceContext;
    sink.Clear();

    // Descriptors may alias, so one real image backs every write.
    uint32_t pixel = 0xffffffffu;
    GUI::Image* img = dc.CreateImage(&pixel, GUI::uint2{ 1, 1 },
                                     GUI::ImageFormat::R8G8B8A8,
                                     GUI::Sampler::LinearClampToEdge, false);
    ASSERT_NE(img, nullptr);
    VkImageView view = static_cast<Vulkan::VulkanImage*>(img)->imageView;

    constexpr uint32_t kCount = 17;
    std::vector<VkDescriptorImageInfo> infos(
        kCount,
        VkDescriptorImageInfo{ dc.defaultSamplerLinear, view,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

    Vulkan::DescriptorAllocatorGrowable alloc;
    std::vector<Vulkan::DescriptorAllocatorGrowable::PoolSizeRatio> ratios = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256.0f }
    };
    alloc.init(dc.device, 4, ratios, dc.AllocationCallbacks());

    VkDescriptorSet set = alloc.allocate(dc.device, dc.singleImageDescriptorLayout);

    // Mirror the per-frame write in VulkanGraphicsContext::RenderWindowFrame:
    // update_set fills in dstSet for each pushed write.
    Vulkan::DescriptorWriter writer;
    writer.writes.push_back(VkWriteDescriptorSet{
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstBinding      = 0,
        .dstArrayElement = 0,
        .descriptorCount = kCount,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo      = infos.data(),
    });
    writer.update_set(dc.device, set);

    EXPECT_FALSE(sink.HasErrors())
        << "Writing " << kCount << " textures tripped validation — the texture "
           "binding is likely still capped:\n" << sink.Dump();

    alloc.destroy_pools(dc.device);
    dc.DestroyImage(img);
}
