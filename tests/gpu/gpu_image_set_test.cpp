#include "gpu_test_harness.h"

#include <gui_core/graphics/vulkan/vk_image.h>

using namespace GpuTestHarness;

namespace
{
    interstice::vulkan::VulkanImage FakeImage(uintptr_t view)
    {
        interstice::vulkan::VulkanImage image{};
        image.imageView = reinterpret_cast<VkImageView>(view);
        return image;
    }
}

// The shader's texture array has a fixed size; a slot past it can't be bound.
TEST(VulkanImageSet, RefusesMoreTexturesThanTheShaderArrayHolds)
{
    interstice::vulkan::VulkanImageSet set;
    interstice::vulkan::VulkanImage image = FakeImage(1);
    for (uint32_t i = 0; i < interstice::vulkan::VulkanImageSet::kCapacity; ++i)
        ASSERT_NE(set.Add(&image), interstice::ImageIndex::Invalid);

    EXPECT_EQ(set.Add(&image), interstice::ImageIndex::Invalid);
    EXPECT_EQ(set.ReserveIndex(), interstice::ImageIndex::Invalid);
    EXPECT_EQ(set.textures.size(), interstice::vulkan::VulkanImageSet::kCapacity);
}

TEST(VulkanImageSet, IgnoresIndicesItNeverHandedOut)
{
    interstice::vulkan::VulkanImageSet set;
    interstice::vulkan::VulkanImage image = FakeImage(1);
    const interstice::ImageIndex index = set.Add(&image);

    EXPECT_FALSE(set.SetDescriptor(interstice::ImageIndex(uint32_t(index) + 4), VkDescriptorImageInfo{}));
    EXPECT_FALSE(set.SetImage(interstice::ImageIndex(uint32_t(index) + 4), &image));
    EXPECT_EQ(set.textures.size(), 1u);
}

TEST(VulkanImageSet, RemovedSlotsAreReused)
{
    interstice::vulkan::VulkanImageSet set;
    interstice::vulkan::VulkanImage a = FakeImage(1), b = FakeImage(2), c = FakeImage(3);
    const interstice::ImageIndex ia = set.Add(&a);
    const interstice::ImageIndex ib = set.Add(&b);

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
    interstice::Image* img = dc.CreateImage(&pixel, interstice::uint2{ 1, 1 }, interstice::ImageFormat::R8G8B8A8,
                                     interstice::Sampler::LinearClampToEdge, false);
    ASSERT_NE(img, nullptr);
    const interstice::ImageIndex index = vk->RegisterImage(img);
    ASSERT_NE(index, interstice::ImageIndex::Invalid);
    RenderOneFrameWithDot(ctx, { 320, 240 });

    vk->UnregisterImage(index);
    dc.DestroyImageAtEnd(img, interstice::DeviceScope::Frame);
    static_cast<interstice::vulkan::VulkanImageSet*>(vk->GetImageSet())->ReserveIndex(); // reserved, never set
    sink.Clear();
    for (int i = 0; i < 3; ++i)
    {
        graphicsContext->BeginFrame();
        RenderOneFrameWithDot(ctx, { 320, 240 });
    }

    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}
