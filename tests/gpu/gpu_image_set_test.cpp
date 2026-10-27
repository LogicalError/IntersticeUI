#include "gpu_test_harness.h"

#include <gui_core/graphics/vulkan/vk_image.h>

using namespace GpuTestHarness;

namespace
{
    tachyon::vulkan::VulkanImage FakeImage(uintptr_t view)
    {
        tachyon::vulkan::VulkanImage image{};
        image.imageView = reinterpret_cast<VkImageView>(view);
        return image;
    }
}

// The shader's texture array has a fixed size; a slot past it can't be bound.
TEST(VulkanImageSet, RefusesMoreTexturesThanTheShaderArrayHolds)
{
    tachyon::vulkan::VulkanImageSet set;
    tachyon::vulkan::VulkanImage image = FakeImage(1);
    for (uint32_t i = 0; i < tachyon::vulkan::VulkanImageSet::kCapacity; ++i)
        ASSERT_NE(set.Add(&image), tachyon::ImageIndex::Invalid);

    EXPECT_EQ(set.Add(&image), tachyon::ImageIndex::Invalid);
    EXPECT_EQ(set.ReserveIndex(), tachyon::ImageIndex::Invalid);
    EXPECT_EQ(set.textures.size(), tachyon::vulkan::VulkanImageSet::kCapacity);
}

TEST(VulkanImageSet, IgnoresIndicesItNeverHandedOut)
{
    tachyon::vulkan::VulkanImageSet set;
    tachyon::vulkan::VulkanImage image = FakeImage(1);
    const tachyon::ImageIndex index = set.Add(&image);

    EXPECT_FALSE(set.SetDescriptor(tachyon::ImageIndex(uint32_t(index) + 4), VkDescriptorImageInfo{}));
    EXPECT_FALSE(set.SetImage(tachyon::ImageIndex(uint32_t(index) + 4), &image));
    EXPECT_EQ(set.textures.size(), 1u);
}

TEST(VulkanImageSet, RemovedSlotsAreReused)
{
    tachyon::vulkan::VulkanImageSet set;
    tachyon::vulkan::VulkanImage a = FakeImage(1), b = FakeImage(2), c = FakeImage(3);
    const tachyon::ImageIndex ia = set.Add(&a);
    const tachyon::ImageIndex ib = set.Add(&b);

    set.Remove(ia);
    EXPECT_EQ(set.Add(&c), ia);
    EXPECT_EQ(set.textures[uint32_t(ia) - 1].imageView, c.imageView);
    EXPECT_EQ(set.textures[uint32_t(ib) - 1].imageView, b.imageView);
    EXPECT_EQ(set.textures.size(), 2u);
}

// An unregistered image's slot (or a reserved one never set) must not be written into the frame's descriptor
// set: its view is gone, and a null view needs the nullDescriptor feature.
TEST_F(GpuTest, UnregisteredImageSlotIsNotBoundNextFrame)
{
    auto* ctx = CreateImageWindowContext({ 320, 240 });
    if (ctx == nullptr)
        GTEST_SKIP() << "no Vulkan device available";

    auto& dc = vk->deviceContext;
    uint32_t pixel = 0xffffffffu;
    tachyon::Image* img = dc.CreateImage(&pixel, tachyon::uint2{ 1, 1 }, tachyon::ImageFormat::R8G8B8A8,
                                     tachyon::Sampler::LinearClampToEdge, false);
    ASSERT_NE(img, nullptr);
    const tachyon::ImageIndex index = vk->RegisterImage(img);
    ASSERT_NE(index, tachyon::ImageIndex::Invalid);
    RenderOneFrameWithDot(ctx, { 320, 240 });

    vk->UnregisterImage(index);
    dc.DestroyImageAtEnd(img, tachyon::DeviceScope::Frame);
    static_cast<tachyon::vulkan::VulkanImageSet*>(vk->GetImageSet())->ReserveIndex(); // reserved, never set
    sink.Clear();
    for (int i = 0; i < 3; ++i)
    {
        graphicsContext->BeginFrame();
        RenderOneFrameWithDot(ctx, { 320, 240 });
    }

    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}
