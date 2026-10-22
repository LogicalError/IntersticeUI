#include "gpu_test_harness.h"

#include <gui_core/graphics/vulkan/vk_image.h>

using namespace GpuTestHarness;

namespace
{
    Vulkan::VulkanImage FakeImage(uintptr_t view)
    {
        Vulkan::VulkanImage image{};
        image.imageView = reinterpret_cast<VkImageView>(view);
        return image;
    }
}

// The shader's texture array has a fixed size; a slot past it can't be bound.
TEST(VulkanImageSet, RefusesMoreTexturesThanTheShaderArrayHolds)
{
    Vulkan::VulkanImageSet set;
    Vulkan::VulkanImage image = FakeImage(1);
    for (uint32_t i = 0; i < Vulkan::VulkanImageSet::kCapacity; ++i)
        ASSERT_NE(set.Add(&image), GUI::ImageIndex::Invalid);

    EXPECT_EQ(set.Add(&image), GUI::ImageIndex::Invalid);
    EXPECT_EQ(set.ReserveIndex(), GUI::ImageIndex::Invalid);
    EXPECT_EQ(set.textures.size(), Vulkan::VulkanImageSet::kCapacity);
}

TEST(VulkanImageSet, IgnoresIndicesItNeverHandedOut)
{
    Vulkan::VulkanImageSet set;
    Vulkan::VulkanImage image = FakeImage(1);
    const GUI::ImageIndex index = set.Add(&image);

    EXPECT_FALSE(set.SetDescriptor(GUI::ImageIndex(uint32_t(index) + 4), VkDescriptorImageInfo{}));
    EXPECT_FALSE(set.SetImage(GUI::ImageIndex(uint32_t(index) + 4), &image));
    EXPECT_EQ(set.textures.size(), 1u);
}

TEST(VulkanImageSet, RemovedSlotsAreReused)
{
    Vulkan::VulkanImageSet set;
    Vulkan::VulkanImage a = FakeImage(1), b = FakeImage(2), c = FakeImage(3);
    const GUI::ImageIndex ia = set.Add(&a);
    const GUI::ImageIndex ib = set.Add(&b);

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
    auto* ctx = CreateHiddenWindowContext({ 320, 240 }, GUI::WindowFlags::None);
    if (ctx == nullptr)
        GTEST_SKIP() << "no window/swapchain available in this environment";

    auto& dc = vk->deviceContext;
    uint32_t pixel = 0xffffffffu;
    GUI::Image* img = dc.CreateImage(&pixel, GUI::uint2{ 1, 1 }, GUI::ImageFormat::R8G8B8A8,
                                     GUI::Sampler::LinearClampToEdge, false);
    ASSERT_NE(img, nullptr);
    const GUI::ImageIndex index = vk->RegisterImage(img);
    ASSERT_NE(index, GUI::ImageIndex::Invalid);
    RenderOneFrameWithDot(ctx, { 320, 240 });

    vk->UnregisterImage(index);
    dc.DestroyImageAtEnd(img, GUI::DeviceScope::Frame);
    static_cast<Vulkan::VulkanImageSet*>(vk->GetImageSet())->ReserveIndex(); // reserved, never set
    sink.Clear();
    for (int i = 0; i < 3; ++i)
    {
        graphicsContext->BeginFrame();
        RenderOneFrameWithDot(ctx, { 320, 240 });
    }

    EXPECT_FALSE(sink.HasErrors()) << sink.Dump();
}
