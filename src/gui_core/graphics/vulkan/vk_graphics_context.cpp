#include <format>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/gui_base_types.h>

#include <gui_core/graphics/gui_image.h>
#include <gui_core/graphics/gui_buffer.h>
#include <gui_core/graphics/gui_graphics_context.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/graphics/vulkan/vk_rhi.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_text_atlas_manager.h>
#include <gui_core/graphics/vulkan/vk_window_context.h>
#include <gui_core/graphics/vulkan/vk_initializers.h>
#include <gui_core/graphics/vulkan/vk_images.h>

#include <gui_core/text/skribidi/gui_text_system_skribidi.h>

#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <gui_core/windows/gui_window_backend.h>
#include <gui_core/windows/gui_window_flags.h>

// A host that links its own VMA builds with TACHYON_VMA_IMPLEMENTATION off.
#if TACHYON_VMA_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#endif
#define VMA_DEBUG_LOG_FORMAT
// VMA's implementation is third-party code; a host's own VMA target doesn't mark
// its headers as external, so their warnings would show here.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <vk_mem_alloc.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <VkBootstrap.h>

#include <fstream>
#include <assert.h>

static constexpr tachyon::Diagnostics::LogCategory kLogVulkan = tachyon::Diagnostics::MakeLogCategory("vulkan");



using namespace tachyon::vulkan;

static size_t BytesPerPixel(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SRGB:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SRGB:
            return 2;
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SRGB:
            return 3;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return 4;
        default:
            TACHYON_LOG_WARNING(kLogVulkan, "Unknown format used in CreateImage, assuming 4 bytes per pixel");
            return 4;
    }
}


void tachyon::vulkan::VulkanGraphicsContext::RePrepareFrame()
{
    if (m_lastStyleManager != nullptr && m_lastGradientManager != nullptr)
        PrepareFrame(*m_lastStyleManager, *m_lastGradientManager, m_lastElapsedTimeSecs);
}

void tachyon::vulkan::VulkanGraphicsContext::PrepareFrame(tachyon::StyleManager& styleManager, tachyon::GradientManager& gradientManager, float elapsedTimeSecs)
{
    m_lastStyleManager    = &styleManager;
    m_lastGradientManager = &gradientManager;
    m_lastElapsedTimeSecs = elapsedTimeSecs;

    // Register every gradient page with the imageSet before resolving the VCS
    // buffer, which needs a physical shader slot for every logical page.
    {
        auto& pageIdx = gradientPageImageIndices;

        const uint32_t pageCount = gradientManager.PageCount();
        for (uint32_t p = 0; p < pageCount; ++p)
        {
            tachyon::Image* img = gradientManager.GetGradientImage(this, p);
            if (p < pageIdx.size())
            {
                vulkanImageSet->SetImage(pageIdx[p], (VulkanImage*)img);
            } else
            {
                tachyon::ImageIndex newIdx = vulkanImageSet->Add(img);
                pageIdx.push_back(newIdx);
                // 0-based shader slot = ((int)newIdx) - 1
                gradientManager.SetPagePhysicalImageIndex(p, int32_t(uint32_t(newIdx)) - 1);
            }
        }
    }

    VulkanBuffer* visualBoxStyleBuffer   = (VulkanBuffer*)PrepareVisualBoxStylesBuffer(styleManager);
    VulkanBuffer* visualColorStyleBuffer = (VulkanBuffer*)PrepareVisualColorStylesBuffer(styleManager, gradientManager);

    VkDeviceAddress visualBoxStyleAddress   = visualBoxStyleBuffer   == nullptr ? 0 : visualBoxStyleBuffer  ->visualBoxStyleBuffers  .styleBufferAddress;
    VkDeviceAddress visualColorStyleAddress = visualColorStyleBuffer == nullptr ? 0 : visualColorStyleBuffer->visualColorStyleBuffers.colorStyleBufferAddress;

    pushConstants =
    {
        .boxStyleBuffer   = visualBoxStyleAddress,
        .colorStyleBuffer = visualColorStyleAddress,
        .elapsedTime      = elapsedTimeSecs,
    };
}

bool tachyon::vulkan::VulkanGraphicsContext::RenderWindowFrame(VkCommandBuffer cmd, FrameData& currentFrame, tachyon::ID windowID, tachyon::uint2 extent)
{
    {
        tachyon::float4x4 scale =
            tachyon::float4x4
            (
                tachyon::float4{ 2.0f / float(extent.x), 0, 0, 0 },
                tachyon::float4{ 0, 2.0f / float(extent.y), 0, 0 },
                tachyon::float4{ 0, 0, 1, 0 },
                tachyon::float4{ 0, 0, 0, 1 }
            );

        tachyon::float4x4 recenter =
            tachyon::float4x4
            (
                tachyon::float4{ 1,  0, 0, 0 },
                tachyon::float4{ 0,  1, 0, 0 },
                tachyon::float4{ 0,  0, 1, 0 },
                tachyon::float4{ -1, -1, 0, 1 }
            );

        pushConstants.worldMatrix = scale * recenter;
        {
            // A primitive's depth is its draw order (transform0.w) a step apart, the first a step
            // above the cleared depth. Column-major, as the shaders read it.
            float m[16];
            static_assert(sizeof(m) == sizeof(pushConstants.worldMatrix));
            std::memcpy(m, &pushConstants.worldMatrix, sizeof(m));
            m[10] = tachyon::kPrimitiveDepthStep;
            m[14] = tachyon::kPrimitiveDepthStep;
            std::memcpy(&pushConstants.worldMatrix, m, sizeof(m));
        }

        VkViewport viewport{ .x = 0, .y = 0, .width = float(extent.x), .height = float(extent.y), .minDepth = 0.f, .maxDepth = 1.f };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = { .offset = {0, 0}, .extent { uint32_t(viewport.width), uint32_t(viewport.height) } };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkDescriptorSet descriptorImageSet = currentFrame.frameDescriptors.allocate(deviceContext.device, deviceContext.singleImageDescriptorLayout);
        {
            // One write per run of filled slots; empty slots stay unwritten (PARTIALLY_BOUND).
            DescriptorWriter writer;
            const auto& textures = vulkanImageSet->textures;
            for (uint32_t first = 0; first < textures.size(); )
            {
                if (textures[first].imageView == VK_NULL_HANDLE) { ++first; continue; }
                uint32_t end = first + 1;
                while (end < textures.size() && textures[end].imageView != VK_NULL_HANDLE) ++end;
                writer.writes.push_back(VkWriteDescriptorSet
                    {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstBinding = 0,
                        .dstArrayElement = first,
                        .descriptorCount = end - first,
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .pImageInfo = textures.data() + first
                    });
                first = end;
            }

            writer.update_set(deviceContext.device, descriptorImageSet);
        }
        VulkanRenderCommandList cmdList(deviceContext, cmd, descriptorImageSet, currentFrame);
        static_cast<tachyon::PrimitiveRenderer*>(guiRenderer)->Render(
            windowID, cmdList, deviceContext.renderContext.pipelines, pushConstants);
    }
    return true;
}

tachyon::ImageSet* VulkanGraphicsContext::GetImageSet() const { return vulkanImageSet; }
tachyon::ImageIndex VulkanGraphicsContext::RegisterImage(tachyon::Image* image) { return vulkanImageSet->Add(image); }
void VulkanGraphicsContext::UnregisterImage(tachyon::ImageIndex index) { vulkanImageSet->Remove(index); }

tachyon::ImageIndex VulkanGraphicsContext::RegisterExternalImage(VkImageView view, VkSampler sampler)
{
    const tachyon::ImageIndex index = vulkanImageSet->ReserveIndex();
    vulkanImageSet->SetDescriptor(index, VkDescriptorImageInfo{
        .sampler = sampler, .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    return index;
}

void VulkanGraphicsContext::UpdateExternalImage(tachyon::ImageIndex index, VkImageView view, VkSampler sampler)
{
    vulkanImageSet->SetDescriptor(index, VkDescriptorImageInfo{
        .sampler = sampler, .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
}

VulkanGraphicsContext::VulkanGraphicsContext()
{
    guiRenderer = new tachyon::PrimitiveRenderer();
}

tachyon::Buffer* VulkanGraphicsContext::PrepareVisualColorStylesBuffer(tachyon::StyleManager& styleManager, tachyon::GradientManager& gradientManager)
{
    // Styles store LOGICAL gradient pages (stable); the shader samples PHYSICAL
    // bindless slots (assigned as pages join the imageSet), resolved here before
    // upload. Re-uploads when the styles or the gradient page table changed.

    const bool vcsModified = styleManager.AreVisualColorStylesModified() || colorStylesMesh == nullptr;
    const bool pageTableChanged = gradientManager.HasPageTableChanged();
    if (!vcsModified && !pageTableChanged)
        return colorStylesMesh;

    if (colorStylesMesh != nullptr)
    {
        DestroyVisualColorStylesAtEnd(colorStylesMesh, tachyon::DeviceScope::Frame);
        colorStylesMesh = nullptr;
    }

    std::span<const tachyon::VisualColorStyle> src = styleManager.GetVisualColorStylesSpan();

    // Resolve into a copy; the stored styles keep the logical page.
    std::vector<tachyon::VisualColorStyle> resolved(src.begin(), src.end());
    for (auto& vcs : resolved)
    {
        int32_t physical = gradientManager.GetPagePhysicalImageIndex(vcs.gradientPage);
        // Registered with the imageSet earlier this frame by PrepareFrame.
        assert(physical >= 0);
        vcs.gradientPage = uint16_t(physical);
    }

    colorStylesMesh = UploadVisualColorStyles(
        std::span<const tachyon::VisualColorStyle>(resolved.data(), resolved.size()));
    styleManager.ResetVisualColorStylesModified();
    gradientManager.ClearPageTableChanged();
    return colorStylesMesh;
}

tachyon::Buffer* VulkanGraphicsContext::PrepareVisualBoxStylesBuffer(tachyon::StyleManager& styleManager)
{
    if (styleManager.AreVisualBoxStylesModified() || visualBoxStylesMesh == nullptr)
    {
        if (visualBoxStylesMesh != nullptr)
        {
            DestroyVisualBoxStylesAtEnd(visualBoxStylesMesh, tachyon::DeviceScope::Frame);
        }
        visualBoxStylesMesh = UploadVisualBoxStyles(styleManager.GetVisualBoxStylesSpan());
        styleManager.ResetVisualBoxStylesModified();
    }
    return visualBoxStylesMesh;
}

tachyon::IWindowContext* VulkanGraphicsContext::CreateWindowContext(void* nativeHandle, tachyon::OSWindowHandle osHandle, 
    tachyon::ID id, tachyon::int2 size, tachyon::WindowFlags windowFlags, tachyon::OnGuiFunc onGui)
{
    auto* context = tachyon::vulkan::VulkanWindowContext::Create(id, nativeHandle, osHandle, size, windowFlags, &deviceContext, std::move(onGui));
    if (context != nullptr)
    {
        m_windowContexts[id] = context;
        if (const auto it = m_imageTargets.find(id); it != m_imageTargets.end())
            context->SetImageTarget(it->second.image, it->second.extent);
    }
    return context;
}

void VulkanGraphicsContext::SetWindowImageTarget(tachyon::ID window, VkImage image, VkExtent2D extent)
{
    if (image == VK_NULL_HANDLE)
        m_imageTargets.erase(window);
    else
        m_imageTargets[window] = { image, extent };
    if (const auto it = m_windowContexts.find(window); it != m_windowContexts.end())
        it->second->SetImageTarget(image, extent);
}

void VulkanGraphicsContext::DestroyWindowContext(tachyon::IWindowContext* context)
{
    if (context == nullptr)
        return;

    auto* vulkanContext = static_cast<tachyon::vulkan::VulkanWindowContext*>(context);

    // The GPU upload arena belongs to the window context. Drop the CPU-side
    // primitive target too so monotonic window IDs cannot accumulate.
    if (guiRenderer != nullptr)
        static_cast<tachyon::PrimitiveRenderer*>(guiRenderer)->ReleaseTarget(vulkanContext->GetID());

    m_windowContexts.erase(vulkanContext->GetID());
    vulkanContext->Destroy();
    delete vulkanContext;
}

tachyon::Result<void> VulkanGraphicsContext::Initialize(tachyon::IWindowBackend* windowBackend)
{
    // A hidden window whose surface picks a GPU that can present to the GUI's windows.
    tachyon::OSWindowHandle bootstrapWindow = windowBackend->CreateWindow(
        "Vulkan bootstrap", tachyon::int2{ 64, 64 }, tachyon::WindowFlags::Hidden, nullptr);
    if (bootstrapWindow == nullptr)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "Failed to create bootstrap window for Vulkan initialization");
        return tachyon::Error{ tachyon::ErrorCode::Failed, "could not create the bootstrap window for Vulkan" };
    }

    VkSurfaceKHR bootstrapSurface = deviceContext.CreateSurface(bootstrapWindow);
    if (!deviceContext.Initialize())
    {
        TACHYON_LOG_ERROR(kLogVulkan, "Failed to initialize device context");
        windowBackend->DestroyWindow(bootstrapWindow);
        return deviceContext.initError.message.empty()
            ? tachyon::Error{ tachyon::ErrorCode::Failed, "could not initialize the Vulkan device" }
            : deviceContext.initError;
    }

    if (vulkanImageSet == nullptr)   // RecreateDevice keeps the indices the app holds
        vulkanImageSet = new tachyon::vulkan::VulkanImageSet();

    if (bootstrapSurface != VK_NULL_HANDLE)
    {
        auto* dc = &deviceContext;
        deviceContext.GetDeletionQueue(tachyon::DeviceScope::Application).
            push_function([dc, bootstrapSurface]()
            {
                vkDestroySurfaceKHR(dc->instance, bootstrapSurface, dc->AllocationCallbacks());
            });
    }

    windowBackend->DestroyWindow(bootstrapWindow);
    return {};
}

tachyon::Result<void> VulkanGraphicsContext::RecreateDevice(tachyon::IWindowBackend* windowBackend)
{
    if (deviceContext.UsesExternalDevice())
        return tachyon::Error{ tachyon::ErrorCode::DeviceLost,
                                  "the host's Vulkan device was lost; the host makes a new one and the GUI again" };

    // Everything made on the lost device goes; the image objects, their indices and the
    // renderer stay. Destroying is allowed on a lost device.
    deviceContext.WaitIdle();
    static_cast<tachyon::PrimitiveRenderer*>(guiRenderer)->ReleaseAllTargets();
    if (colorStylesMesh != nullptr)
    {
        deviceContext.DestroyVisualColorStylesAtEnd(colorStylesMesh, tachyon::DeviceScope::Application);
        colorStylesMesh = nullptr;
    }
    if (visualBoxStylesMesh != nullptr)
    {
        deviceContext.DestroyVisualBoxStylesAtEnd(visualBoxStylesMesh, tachyon::DeviceScope::Application);
        visualBoxStylesMesh = nullptr;
    }
    deviceContext.ReleaseLiveImages();
    deviceContext.Destroy();
    deviceContext.initialized = false;
    tachyon::vulkan::SetDeviceLost(false);

    if (tachyon::Result<void> result = Initialize(windowBackend); !result)
        return result;
    deviceContext.RebuildLiveImages();
    vulkanImageSet->RefreshDescriptors();
    return {};
}

void VulkanGraphicsContext::Cleanup()
{
    if (colorStylesMesh != nullptr)
    {
        deviceContext.DestroyVisualColorStylesAtEnd(colorStylesMesh, tachyon::DeviceScope::Application);
        colorStylesMesh = nullptr;
    }

    if (visualBoxStylesMesh != nullptr)
    {
        deviceContext.DestroyVisualBoxStylesAtEnd(visualBoxStylesMesh, tachyon::DeviceScope::Application);
        visualBoxStylesMesh = nullptr;
    }

    // A failed Initialize leaves no device to wait on or tear down.
    if (deviceContext.initialized)
    {
        deviceContext.WaitIdle();
        static_cast<tachyon::PrimitiveRenderer*>(guiRenderer)->ReleaseAllTargets();

        // mainDeletionQueue captures atlas font textures by pointer - they must
        // outlive Flush/Destroy.
        deviceContext.Flush();
        deviceContext.Destroy();
    }

    delete vulkanImageSet;
    vulkanImageSet = nullptr;

    delete guiRenderer;
    guiRenderer = nullptr;
}

tachyon::Image* VulkanGraphicsContext::CreateImage(tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped)
{
    return deviceContext.CreateImage(size, format, sampler, mipmapped);
}

tachyon::Image* VulkanGraphicsContext::CreateImage(void* data, tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped)
{
    return deviceContext.CreateImage(data, size, format, sampler, mipmapped);
}

void VulkanGraphicsContext::DestroyImage(tachyon::Image* img)
{
    deviceContext.DestroyImage(img);
}

void VulkanGraphicsContext::DestroyImageAtEnd(tachyon::Image* img, tachyon::DeviceScope scope, [[maybe_unused]] const std::source_location& loc)
{
    deviceContext.DestroyImageAtEnd(img, scope, loc);
}

tachyon::Buffer* VulkanGraphicsContext::UploadVisualColorStyles(std::span<const tachyon::VisualColorStyle> colorStyles)
{
    return deviceContext.UploadVisualColorStyles(colorStyles);
}

void VulkanGraphicsContext::DestroyVisualColorStyles(tachyon::Buffer* buffer)
{
    deviceContext.DestroyVisualColorStyles(buffer);
}

void VulkanGraphicsContext::DestroyVisualColorStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope, [[maybe_unused]] const std::source_location& loc)
{
    deviceContext.DestroyVisualColorStylesAtEnd(buffer, scope, loc);
}

tachyon::Buffer* VulkanGraphicsContext::UploadVisualBoxStyles(std::span<const tachyon::VisualBoxStyle> visualBoxStyles)
{
    return deviceContext.UploadVisualBoxStyles(visualBoxStyles);
}

void VulkanGraphicsContext::DestroyVisualBoxStyles(tachyon::Buffer* buffer)
{
    deviceContext.DestroyVisualBoxStyles(buffer);
}

void VulkanGraphicsContext::DestroyVisualBoxStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope, [[maybe_unused]] const std::source_location& loc)
{
    deviceContext.DestroyVisualBoxStylesAtEnd(buffer, scope, loc);
}

tachyon::TextAtlasManager* VulkanGraphicsContext::CreateTextAtlasManager()
{
    return new tachyon::vulkan::VulkanTextAtlasManager();
}

void tachyon::vulkan::VulkanImageSet::Clear()
{
    textures.clear();
    owners.clear();
    m_freeIndices.clear();
}

void VulkanImageSet::RefreshDescriptors()
{
    for (size_t i = 0; i < owners.size(); ++i)
        if (owners[i] != nullptr)
            SetImage(tachyon::ImageIndex(i + 1), owners[i]);
}

tachyon::ImageIndex VulkanImageSet::Allocate(const VkDescriptorImageInfo& descriptor)
{
    if (!m_freeIndices.empty())
    {
        const tachyon::ImageIndex index = m_freeIndices.back();
        m_freeIndices.pop_back();
        textures[uint32_t(index) - 1] = descriptor;
        owners[uint32_t(index) - 1]   = nullptr;
        return index;
    }
    if (textures.size() >= kCapacity)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "texture set is full ({} images)", kCapacity);
        return tachyon::ImageIndex::Invalid;
    }
    textures.push_back(descriptor);
    owners.push_back(nullptr);
    return tachyon::ImageIndex(textures.size());
}

bool VulkanImageSet::IsInRange(tachyon::ImageIndex index) const
{
    if (index != tachyon::ImageIndex::Invalid && uint32_t(index) <= textures.size())
        return true;
    TACHYON_LOG_ERROR(kLogVulkan, "Invalid image index {}", uint32_t(index));
    return false;
}

tachyon::ImageIndex tachyon::vulkan::VulkanImageSet::Add(tachyon::Image* image)
{
    VulkanImage* vulkanImage = static_cast<VulkanImage*>(image);
    const tachyon::ImageIndex index = Allocate(VkDescriptorImageInfo
        {
            .sampler     = vulkanImage->imageSampler,
            .imageView   = vulkanImage->imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
    if (index != tachyon::ImageIndex::Invalid)
        owners[uint32_t(index) - 1] = vulkanImage;
    return index;
}

// An empty slot (no image view) is skipped when the frame's descriptors are written.
tachyon::ImageIndex VulkanImageSet::ReserveIndex()
{
    return Allocate(VkDescriptorImageInfo{});
}

void VulkanImageSet::Remove(tachyon::ImageIndex index)
{
    if (!IsInRange(index) || textures[uint32_t(index) - 1].imageView == VK_NULL_HANDLE)
        return;
    textures[uint32_t(index) - 1] = {};
    owners[uint32_t(index) - 1]   = nullptr;
    m_freeIndices.push_back(index);
}

bool VulkanImageSet::SetImage(tachyon::ImageIndex index, VulkanImage* image)
{
    if (!SetDescriptor(index, VkDescriptorImageInfo
        {
            .sampler     = image->imageSampler,
            .imageView   = image->imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }))
        return false;
    owners[uint32_t(index) - 1] = image;
    return true;
}

bool VulkanImageSet::SetDescriptor(tachyon::ImageIndex index, const VkDescriptorImageInfo& descriptor)
{
    if (!IsInRange(index))
        return false;
    textures[uint32_t(index) - 1] = descriptor;
    owners[uint32_t(index) - 1]   = nullptr;   // a host's descriptor; SetImage sets the owner
    return true;
}

void VulkanGraphicsContext::BeginFrame()
{
    // Runs after all windows' BeginWindowFrame fence-waits for the current
    // frame, so the slot being flushed (FRAME_OVERLAP frames old) is safe.
    deviceContext.AdvanceFrameAndFlushDeletions();
    guiRenderer->Reset();
}

VkSurfaceKHR tachyon::vulkan::VulkanDeviceContext::CreateSurface(tachyon::OSWindowHandle windowHandle)
{
    if (!m_surfaceFactory || windowHandle == nullptr)
        return VK_NULL_HANDLE;

    if (initialized)
    {
        return m_surfaceFactory(instance, AllocationCallbacks(), windowHandle);
    }

    if (m_externalDevice)
    {
        if (!AdoptExternalDevice())
            return VK_NULL_HANDLE;
        if (!InitializeSharedResources())
        {
            if (m_ownsAllocator) vmaDestroyAllocator(allocator);
            allocator = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
        initialized = true;
        return m_surfaceFactory(instance, AllocationCallbacks(), windowHandle);
    }

    vkb::InstanceBuilder instance_builder;
    auto system_info_ret = vkb::SystemInfo::get_system_info();
    if (!system_info_ret)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "failed to get system info");
        initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("failed to get system info") };
        return VK_NULL_HANDLE;
    }

    auto& system_info = system_info_ret.value();

    instance_builder.set_app_name("GUI")
        .require_api_version(1, 3, 0)
        .set_allocation_callbacks(const_cast<VkAllocationCallbacks*>(AllocationCallbacks()));

    // The GPU test harness installs a recording debug callback (and optionally
    // synchronization validation) so validation errors fail tests. Otherwise:
    // default printing messenger + validation layers in debug builds only.
    if (m_instanceDebugConfig.callback != nullptr)
    {
        instance_builder
            .request_validation_layers(true)
            .enable_validation_layers(true)
            .set_debug_callback(m_instanceDebugConfig.callback)
            .set_debug_callback_user_data_pointer(m_instanceDebugConfig.userData);
        if (m_instanceDebugConfig.enableSyncValidation)
            instance_builder.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
    }
#if !defined(NDEBUG)
    else
    {
        instance_builder
            .request_validation_layers(true)
            .use_default_debug_messenger()
            .enable_validation_layers();
    }
#endif

    if (system_info.is_extension_available("VK_EXT_debug_report"))
    {
        instance_builder.enable_extension("VK_EXT_debug_report");
    }

    if (system_info.is_extension_available("VK_EXT_debug_utils"))
    {
        instance_builder.enable_extension("VK_EXT_debug_utils");
    }

    auto inst_ret = instance_builder.build();
    if (!inst_ret)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "failed to create the Vulkan instance: {}", inst_ret.error().message());
        initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("failed to create the Vulkan instance: {}", inst_ret.error().message()) };
        return VK_NULL_HANDLE;
    }
    vkb::Instance vkb_inst = inst_ret.value();
    // Undoes the bring-up so far when a later step fails.
    auto fail = [&](VkSurfaceKHR surfaceToDestroy) -> VkSurfaceKHR
        {
            if (surfaceToDestroy != VK_NULL_HANDLE)
                vkDestroySurfaceKHR(vkb_inst.instance, surfaceToDestroy, AllocationCallbacks());
            vkb::destroy_instance(vkb_inst);
            instance        = VK_NULL_HANDLE;
            debug_messenger = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        };

    instance = vkb_inst.instance;
    debug_messenger = vkb_inst.debug_messenger;

    std::vector<VkPhysicalDevice> physicalDevices;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount > 0) {
        physicalDevices.resize(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
        TACHYON_LOG_INFO(kLogVulkan, "Detected Vulkan GPUs:");
        for (const auto& pd : physicalDevices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(pd, &props);
            TACHYON_LOG_INFO(kLogVulkan, "  - {}", props.deviceName);
        }
    } else {
        TACHYON_LOG_ERROR(kLogVulkan, "No Vulkan GPUs detected!");
        initError = tachyon::Error{ tachyon::ErrorCode::NotFound, std::format("No Vulkan GPUs detected!") };
    }

    const DeviceRequirements requirements = GetDeviceRequirements();
    // Copies: vk-bootstrap fills in each struct's sType.
    VkPhysicalDeviceVulkan13Features features13 = requirements.features13;
    VkPhysicalDeviceVulkan12Features features12 = requirements.features12;
    VkPhysicalDeviceVulkan11Features features11 = requirements.features11;
    VkPhysicalDeviceFeatures         features   = requirements.features;

    VkSurfaceKHR surface = m_surfaceFactory(instance, AllocationCallbacks(), windowHandle);
    if (surface == VK_NULL_HANDLE)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "failed to create surface");
        initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("failed to create surface") };
        return fail(VK_NULL_HANDLE);
    }

    // A Vulkan 1.3 GPU that can present to the surface.
    vkb::PhysicalDeviceSelector selector { vkb_inst };
    auto physicalDeviceOpt = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .set_required_features_11(features11)
        .set_required_features(features)
        .set_surface(surface)
        .select();
    if (!physicalDeviceOpt) {
        TACHYON_LOG_ERROR(kLogVulkan, "No suitable Vulkan GPU found: {}", physicalDeviceOpt.error().message());
        initError = tachyon::Error{ tachyon::ErrorCode::NotFound, std::format("No suitable Vulkan GPU found: {}", physicalDeviceOpt.error().message()) };
        return fail(surface);
    }
    vkb::PhysicalDevice physicalDevice = physicalDeviceOpt.value();

    vkb::DeviceBuilder deviceBuilder { physicalDevice };
    deviceBuilder.set_allocation_callbacks(const_cast<VkAllocationCallbacks*>(AllocationCallbacks()));

    auto deviceRet = deviceBuilder.build();
    if (!deviceRet)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "failed to create the Vulkan device: {}", deviceRet.error().message());
        initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("failed to create the Vulkan device: {}", deviceRet.error().message()) };
        return fail(surface);
    }
    vkb::Device vkbDevice = deviceRet.value();
    auto queueRet      = vkbDevice.get_queue(vkb::QueueType::graphics);
    auto queueIndexRet = vkbDevice.get_queue_index(vkb::QueueType::graphics);
    if (!queueRet || !queueIndexRet)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "the Vulkan device has no graphics queue");
        initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("the Vulkan device has no graphics queue") };
        vkb::destroy_device(vkbDevice);
        return fail(surface);
    }

    device = vkbDevice.device;
    chosenGPU = physicalDevice.physical_device;
    graphicsQueue = queueRet.value();
    graphicsQueueFamily = queueIndexRet.value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = chosenGPU;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.pAllocationCallbacks = AllocationCallbacks();
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "failed to create the GUI allocator");
        initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("failed to create the GUI allocator") };
        vkb::destroy_device(vkbDevice);
        device = VK_NULL_HANDLE;
        return fail(surface);
    }

    if (!InitializeSharedResources())
    {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
        vkb::destroy_device(vkbDevice);
        device = VK_NULL_HANDLE;
        return fail(surface);
    }

    initialized = true;
    return surface;
}

bool tachyon::vulkan::VulkanDeviceContext::AdoptExternalDevice()
{
    const ExternalDevice& external = *m_externalDevice;
    if (external.instance == VK_NULL_HANDLE || external.physicalDevice == VK_NULL_HANDLE
        || external.device == VK_NULL_HANDLE || external.graphicsQueue == VK_NULL_HANDLE)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "External Vulkan device is incomplete (instance, physical device, device and queue are required)");
        initError = tachyon::Error{ tachyon::ErrorCode::Unsupported, std::format("External Vulkan device is incomplete (instance, physical device, device and queue are required)") };
        return false;
    }

    std::string problems;
    if (!CheckPhysicalDeviceSupport(external.physicalDevice, &problems))
    {
        TACHYON_LOG_ERROR(kLogVulkan, "External Vulkan device cannot run the GUI:\n{}", problems);
        initError = tachyon::Error{ tachyon::ErrorCode::Unsupported, std::format("External Vulkan device cannot run the GUI:\n{}", problems) };
        return false;
    }

    instance            = external.instance;
    chosenGPU           = external.physicalDevice;
    device              = external.device;
    graphicsQueue       = external.graphicsQueue;
    graphicsQueueFamily = external.graphicsQueueFamily;
    debug_messenger     = VK_NULL_HANDLE;
    m_ownsInstance      = false;
    m_ownsDevice        = false;
    m_queueMutex        = external.queueMutex != nullptr ? external.queueMutex : &m_ownedQueueMutex;

    if (external.allocator != VK_NULL_HANDLE)
    {
        // VMA has no query for its creation flags; this file compiles the implementation.
        if (!external.allocator->m_UseKhrBufferDeviceAddress)
        {
            TACHYON_LOG_ERROR(kLogVulkan, "The host allocator must be created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT");
            initError = tachyon::Error{ tachyon::ErrorCode::Unsupported, std::format("The host allocator must be created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT") };
            return false;
        }
        allocator       = external.allocator;
        m_ownsAllocator = false;
    }
    else
    {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = chosenGPU;
        allocatorInfo.device = device;
        allocatorInfo.instance = instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        allocatorInfo.pAllocationCallbacks = AllocationCallbacks();
        if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS)
        {
            TACHYON_LOG_ERROR(kLogVulkan, "Failed to create the GUI allocator on the external device");
            initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("Failed to create the GUI allocator on the external device") };
            return false;
        }
        m_ownsAllocator = true;
    }
    return true;
}

void tachyon::vulkan::VulkanDeviceContext::SetAllocationHooks(const tachyon::AllocationHooks& hooks)
{
    m_hooks = hooks;
    m_allocationCallbacks = {};
    m_allocationCallbacks.pUserData = &m_hooks;
    m_allocationCallbacks.pfnAllocation = [](void* user, size_t size, size_t alignment, VkSystemAllocationScope) -> void*
    {
        const auto& h = *static_cast<const tachyon::AllocationHooks*>(user);
        return h.allocate(size, alignment, h.user);
    };
    m_allocationCallbacks.pfnReallocation = [](void* user, void* original, size_t size, size_t alignment, VkSystemAllocationScope) -> void*
    {
        const auto& h = *static_cast<const tachyon::AllocationHooks*>(user);
        if (size == 0)   // Vulkan's realloc to 0 frees
        {
            h.free(original, h.user);
            return nullptr;
        }
        return h.reallocate(original, size, alignment, h.user);
    };
    m_allocationCallbacks.pfnFree = [](void* user, void* memory)
    {
        const auto& h = *static_cast<const tachyon::AllocationHooks*>(user);
        h.free(memory, h.user);
    };
}

tachyon::vulkan::VulkanDeviceContext::DeviceRequirements tachyon::vulkan::VulkanDeviceContext::GetDeviceRequirements()
{
    DeviceRequirements requirements;
    requirements.apiVersion = VK_API_VERSION_1_3;

    requirements.features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    requirements.features13.dynamicRendering = true;
    requirements.features13.synchronization2 = true;
    requirements.features13.robustImageAccess = true;

    requirements.features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    requirements.features12.bufferDeviceAddress = true;
    requirements.features12.descriptorIndexing = true;
    requirements.features12.descriptorBindingPartiallyBound = true;
    requirements.features12.descriptorBindingVariableDescriptorCount = true;
    requirements.features12.runtimeDescriptorArray = true;
    requirements.features12.storageBuffer8BitAccess = true;
    requirements.features12.uniformAndStorageBuffer8BitAccess = true;
    requirements.features12.shaderSampledImageArrayNonUniformIndexing = true;
    requirements.features12.shaderInt8 = true;
    requirements.features12.shaderFloat16 = true;

    requirements.features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    requirements.features11.storageBuffer16BitAccess = true;

    requirements.features.shaderInt16 = true;
    requirements.features.robustBufferAccess = true;

    requirements.deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    return requirements;
}

bool tachyon::vulkan::VulkanDeviceContext::CheckPhysicalDeviceSupport(VkPhysicalDevice physicalDevice, std::string* problems)
{
    bool supported = true;
    const auto missing = [&](const std::string& what)
    {
        supported = false;
        if (problems != nullptr) *problems += "  missing: " + what + "\n";
    };

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    const DeviceRequirements required = GetDeviceRequirements();
    if (properties.apiVersion < required.apiVersion)
        missing("Vulkan 1.3 (device reports " + std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "."
            + std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) + ")");

    VkPhysicalDeviceVulkan13Features havefeatures13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features havefeatures12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &havefeatures13 };
    VkPhysicalDeviceVulkan11Features havefeatures11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, .pNext = &havefeatures12 };
    VkPhysicalDeviceFeatures2 have{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &havefeatures11 };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &have);

#define TACHYON_REQUIRE_FEATURE(group, name) \
    if (required.group.name && !have##group.name) missing(#group "." #name)
    TACHYON_REQUIRE_FEATURE(features13, dynamicRendering);
    TACHYON_REQUIRE_FEATURE(features13, synchronization2);
    TACHYON_REQUIRE_FEATURE(features13, robustImageAccess);
    TACHYON_REQUIRE_FEATURE(features12, bufferDeviceAddress);
    TACHYON_REQUIRE_FEATURE(features12, descriptorIndexing);
    TACHYON_REQUIRE_FEATURE(features12, descriptorBindingPartiallyBound);
    TACHYON_REQUIRE_FEATURE(features12, descriptorBindingVariableDescriptorCount);
    TACHYON_REQUIRE_FEATURE(features12, runtimeDescriptorArray);
    TACHYON_REQUIRE_FEATURE(features12, storageBuffer8BitAccess);
    TACHYON_REQUIRE_FEATURE(features12, uniformAndStorageBuffer8BitAccess);
    TACHYON_REQUIRE_FEATURE(features12, shaderSampledImageArrayNonUniformIndexing);
    TACHYON_REQUIRE_FEATURE(features12, shaderInt8);
    TACHYON_REQUIRE_FEATURE(features12, shaderFloat16);
    TACHYON_REQUIRE_FEATURE(features11, storageBuffer16BitAccess);
#undef TACHYON_REQUIRE_FEATURE
    if (required.features.shaderInt16 && !have.features.shaderInt16) missing("features.shaderInt16");
    if (required.features.robustBufferAccess && !have.features.robustBufferAccess) missing("features.robustBufferAccess");

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());
    for (const char* name : required.deviceExtensions)
    {
        const bool found = std::any_of(extensions.begin(), extensions.end(),
            [name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
        if (!found) missing(std::string("device extension ") + name);
    }
    return supported;
}

bool tachyon::vulkan::VulkanDeviceContext::InitializeSharedResources()
{
    auto failed = [this](const char* what)
    {
        TACHYON_LOG_ERROR(kLogVulkan, "failed to create the {}", what);
        initError = tachyon::Error{ tachyon::ErrorCode::Failed, std::format("failed to create the {}", what) };
        GetDeletionQueue(tachyon::DeviceScope::Application).flush();
        return false;
    };

    // Command pool + buffer for immediate submits; individual buffers can be reset.
    commandPoolInfo = tachyon::vulkan::command_pool_create_info(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    if (vkCreateCommandPool(device, &commandPoolInfo, AllocationCallbacks(), &immCommandPool) != VK_SUCCESS)
        return failed("immediate command pool");
    GetDeletionQueue(tachyon::DeviceScope::Application)
        .push_function([=, this]() { vkDestroyCommandPool(device, immCommandPool, AllocationCallbacks()); });

    VkCommandBufferAllocateInfo cmdAllocInfo = tachyon::vulkan::command_buffer_allocate_info(immCommandPool, 1);
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &immCommandBuffer) != VK_SUCCESS)
        return failed("immediate command buffer");

    // Starts signalled so the first wait on it returns at once.
    fenceCreateInfo = tachyon::vulkan::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    if (vkCreateFence(device, &fenceCreateInfo, AllocationCallbacks(), &immFence) != VK_SUCCESS)
        return failed("immediate fence");
    GetDeletionQueue(tachyon::DeviceScope::Application)
        .push_function([=, this]() { vkDestroyFence(device, immFence, AllocationCallbacks()); });

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.bindings[0].descriptorCount = VulkanImageSet::kCapacity; // bindless texture array

        // PARTIALLY_BOUND: each frame only textures.size() slots are written and
        // only those are indexed by the shader; the rest stay undefined. Relies
        // on the descriptor-indexing features enabled at device creation.
        VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo
        {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .bindingCount  = 1,
            .pBindingFlags = &bindingFlags,
        };
        singleImageDescriptorLayout = builder.build(device, AllocationCallbacks(), VK_SHADER_STAGE_FRAGMENT_BIT, &bindingFlagsInfo);
        if (singleImageDescriptorLayout == VK_NULL_HANDLE)
            return failed("texture descriptor set layout");
    }
    GetDeletionQueue(tachyon::DeviceScope::Application)
        .push_function([&]() { vkDestroyDescriptorSetLayout(device, singleImageDescriptorLayout, AllocationCallbacks()); });

    // Samplers
    const VkSamplerCreateInfo nearestSampler =
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST
    };
    const VkSamplerCreateInfo linearSampler =
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
    };
    const VkSamplerCreateInfo linearSamplerClampToEdge =
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    };
    const std::pair<const VkSamplerCreateInfo*, VkSampler*> samplers[] =
    {
        { &nearestSampler,           &defaultSamplerNearest },
        { &linearSampler,            &defaultSamplerLinear },
        { &linearSamplerClampToEdge, &defaultSamplerLinearClampToEdge },
    };
    for (const auto& [info, sampler] : samplers)
    {
        if (vkCreateSampler(device, info, AllocationCallbacks(), sampler) != VK_SUCCESS)
            return failed("default samplers");
        GetDeletionQueue(tachyon::DeviceScope::Application)
            .push_function([this, sampler]() { vkDestroySampler(device, *sampler, AllocationCallbacks()); });
    }
    return true;
}

void tachyon::vulkan::VulkanDeviceContext::Flush()
{
    WaitForUploads();
    WaitIdle();
    WaitForFence(device, immFence);

    for (auto& q : frameDeletionQueues) q.flush();
}

void tachyon::vulkan::VulkanDeviceContext::Destroy()
{
    WaitForUploads();
    WaitIdle();
    WaitForFence(device, immFence);
    for (auto& q : frameDeletionQueues) q.flush();
    GetDeletionQueue(tachyon::DeviceScope::Application).flush();
    for (const PendingUpload& upload : m_pendingUploads) // never recorded: no frame ran
        DestroyBuffer(upload.staging);
    m_pendingUploads.clear();
    for (const auto& [id, resident] : m_residentBuffers)
    {
        if (!resident.ready)
            DestroyBuffer(resident.staging);
        DestroyBuffer(resident.buffer);
    }
    m_residentBuffers.clear();

    // Dump VMA stats only for live allocations; empty reusable blocks are normal here.
    VmaTotalStatistics totalStats{};
    vmaCalculateStatistics(allocator, &totalStats);
    if (totalStats.total.statistics.allocationCount > 0)
    {
        TACHYON_LOG_WARNING(kLogVulkan, "{} GPU allocations still alive at shutdown; VMA stats follow on stdout",
                    totalStats.total.statistics.allocationCount);
        // The JSON runs to kilobytes, more than a log record holds.
        char* stats = nullptr;
        vmaBuildStatsString(allocator, &stats, VK_TRUE);
        std::fputs(stats, stdout);
        std::fputs("\n", stdout);
        vmaFreeStatsString(allocator, stats);
    }
    // Only destroy what the GUI created. A host-provided instance, device and
    // allocator belong to the host.
    if (m_ownsAllocator) vmaDestroyAllocator(allocator);
    if (m_ownsDevice) vkDestroyDevice(device, AllocationCallbacks());
    if (m_ownsInstance)
    {
        vkb::destroy_debug_utils_messenger(instance, debug_messenger, const_cast<VkAllocationCallbacks*>(AllocationCallbacks()));
        vkDestroyInstance(instance, AllocationCallbacks());
    }
}

tachyon::vulkan::Buffer tachyon::vulkan::VulkanDeviceContext::CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const
{
    VkBufferCreateInfo bufferInfo =
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .size = allocSize,
        .usage = usage
    };
    VmaAllocationCreateInfo vmaallocInfo =
    {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = memoryUsage
    };

    tachyon::vulkan::Buffer newBuffer {};
    TACHYON_VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
    vmaSetAllocationName(allocator, newBuffer.allocation, "CreateBuffer");
    return newBuffer;
}

tachyon::vulkan::Buffer tachyon::vulkan::VulkanDeviceContext::CreateMappedStorageBuffer(const void* data, size_t size, VkDeviceAddress& outAddress) const
{
    if (size == 0)
    {
        outAddress = 0;
        return tachyon::vulkan::Buffer {};
    }

    // Host-visible, persistently mapped (CreateBuffer sets MAPPED_BIT) and read by
    // device address, so a plain memcpy uploads it: no staging copy, no GPU wait.
    tachyon::vulkan::Buffer buf = CreateBuffer(size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    VkBufferDeviceAddressInfo addrInfo { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf.buffer };
    outAddress = vkGetBufferDeviceAddress(device, &addrInfo);

    memcpy(buf.allocation->GetMappedData(), data, size);
    // Only matters for non-coherent memory; writes are visible at the next submit.
    vmaFlushAllocation(allocator, buf.allocation, 0, size);
    return buf;
}

void tachyon::vulkan::VulkanDeviceContext::DestroyBuffer(const Buffer& buffer) const
{
    // The upload worker may still be copying into it.
    if (m_uploads.Busy())
        m_uploads.Wait();
    if (buffer.buffer == nullptr)
        return;
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}

static VkFormat ToVulkanImageFormat(tachyon::ImageFormat format)
{
    switch (format)
    {
    default:                                return VkFormat::VK_FORMAT_UNDEFINED;

    case tachyon::ImageFormat::R8:              return VkFormat::VK_FORMAT_R8_UNORM;
    case tachyon::ImageFormat::R8_SRGB:         return VkFormat::VK_FORMAT_R8_SRGB;
    case tachyon::ImageFormat::R8G8:            return VkFormat::VK_FORMAT_R8G8_UNORM;
    case tachyon::ImageFormat::R8G8_SRGB:       return VkFormat::VK_FORMAT_R8G8_SRGB;
    case tachyon::ImageFormat::R8G8B8:          return VkFormat::VK_FORMAT_R8G8B8_UNORM;
    case tachyon::ImageFormat::R8G8B8_SRGB:     return VkFormat::VK_FORMAT_R8G8B8_SRGB;
    case tachyon::ImageFormat::R8G8B8A8:        return VkFormat::VK_FORMAT_R8G8B8A8_UNORM;
    case tachyon::ImageFormat::R8G8B8A8_SRGB:   return VkFormat::VK_FORMAT_R8G8B8A8_SRGB;
    }
}

static VkSampler ToVulkanSampler(tachyon::Sampler sampler, const tachyon::vulkan::VulkanDeviceContext* context)
{
    switch (sampler)
    {
    default:                                return VkSampler {};
    case tachyon::Sampler::NearestRepeat:       return context->defaultSamplerNearest;
    case tachyon::Sampler::LinearRepeat:        return context->defaultSamplerLinear;
    case tachyon::Sampler::LinearClampToEdge:   return context->defaultSamplerLinearClampToEdge;
    }
}

tachyon::Buffer* tachyon::vulkan::VulkanDeviceContext::UploadVisualBoxStyles(::std::span<const tachyon::VisualBoxStyle> visualBoxStyles) const
{
    VulkanBuffer* buffer = new VulkanBuffer();
    buffer->visualBoxStyleBuffers = UploadMesh(visualBoxStyles);
    return buffer;
}

tachyon::Buffer* tachyon::vulkan::VulkanDeviceContext::UploadVisualColorStyles(::std::span<const tachyon::VisualColorStyle> colorStyles) const
{
    VulkanBuffer* buffer = new VulkanBuffer();
    buffer->visualColorStyleBuffers = UploadMesh(colorStyles);
    return buffer;
}

tachyon::Image* tachyon::vulkan::VulkanDeviceContext::CreateImage(tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped) const
{
    VulkanImage* image = new VulkanImage();
    VkFormat vulkan_format = ToVulkanImageFormat(format);
    VkSampler vulkan_sampler = ToVulkanSampler(sampler, this);
    *image = CreateImage(VkExtent3D { size.x, size.y , 1 }, vulkan_format, VK_IMAGE_USAGE_SAMPLED_BIT, vulkan_sampler, mipmapped);
    image->recipe = { .usage = VK_IMAGE_USAGE_SAMPLED_BIT, .sampler = sampler, .mipmapped = mipmapped };
    m_liveImages.insert(image);
    return image;
}

tachyon::Image* tachyon::vulkan::VulkanDeviceContext::CreateImage(const void* data, tachyon::uint2 size, tachyon::ImageFormat format, tachyon::Sampler sampler, bool mipmapped)
{
    const VkFormat   vulkanFormat = ToVulkanImageFormat(format);
    const VkExtent3D extent { size.x, size.y, 1 };
    const size_t     dataSize = size_t(size.x) * size_t(size.y) * BytesPerPixel(vulkanFormat);

    Buffer staging = CreateBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    VulkanImage* image = new VulkanImage();
    // Mip generation blits from each level to the next, so it also reads the image.
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                  | (mipmapped ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    *image = CreateImage(extent, vulkanFormat, usage, ToVulkanSampler(sampler, this), mipmapped);
    m_pendingUploads.push_back({ staging, image->image, extent, mipmapped });
    image->recipe = { .usage = usage, .sampler = sampler, .mipmapped = mipmapped };
    // The pixels are kept for a device loss; the staging copy reads them on the upload worker
    // (DestroyImage waits for it).
    if (data != nullptr)
        image->recipe.pixels.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + dataSize);
    m_uploads.Copy(staging.info.pMappedData, data != nullptr ? image->recipe.pixels.data() : nullptr, dataSize);
    m_uploads.Then([allocator = allocator, allocation = staging.allocation]() { vmaFlushAllocation(allocator, allocation, 0, VK_WHOLE_SIZE); });
    m_liveImages.insert(image);
    return image;
}

void tachyon::vulkan::VulkanDeviceContext::RecordPendingUploads(VkCommandBuffer cmd)
{
    for (const PendingUpload& upload : m_pendingUploads)
    {
        tachyon::vulkan::transition_image(cmd, upload.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region {};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent                 = upload.extent;
        vkCmdCopyBufferToImage(cmd, upload.staging.buffer, upload.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        if (upload.mipmapped) // leaves every level in SHADER_READ_ONLY_OPTIMAL
            tachyon::vulkan::generate_mipmaps(cmd, upload.image, VkExtent2D{ upload.extent.width, upload.extent.height });
        else
            tachyon::vulkan::transition_image(cmd, upload.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        const Buffer staging = upload.staging;
        GetDeletionQueue(tachyon::DeviceScope::Frame).push_function([this, staging]() { DestroyBuffer(staging); });
    }
    m_pendingUploads.clear();

    // Resident buffers: one barrier makes every copy visible to the shaders that read them.
    bool copied = false;
    for (auto& [id, resident] : m_residentBuffers)
    {
        if (resident.ready)
            continue;
        const VkBufferCopy region{ .srcOffset = 0, .dstOffset = 0, .size = resident.size };
        vkCmdCopyBuffer(cmd, resident.staging.buffer, resident.buffer.buffer, 1, &region);
        const Buffer staging = resident.staging;
        GetDeletionQueue(tachyon::DeviceScope::Frame).push_function([this, staging]() { DestroyBuffer(staging); });
        resident.staging = {};
        resident.ready   = true;
        copied = true;
    }
    if (copied)
    {
        const VkMemoryBarrier2 barrier{
            .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT };
        const VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &barrier };
        vkCmdPipelineBarrier2(cmd, &dependency);
    }
}

tachyon::RHI::ResidentBufferId tachyon::vulkan::VulkanDeviceContext::CreateResidentBuffer(
    std::span<const tachyon::RHI::UploadRequest> requests, std::span<tachyon::DeviceAddress> addresses)
{
    assert(requests.size() == addresses.size());
    std::fill(addresses.begin(), addresses.end(), tachyon::DeviceAddress{ 0 });

    auto align = [](size_t offset, size_t alignment) { alignment = std::max<size_t>(alignment, 1); return (offset + alignment - 1) & ~(alignment - 1); };
    size_t size = 0;
    for (const auto& request : requests)
        if (request.bytes != 0)
            size = align(size, request.alignment) + request.bytes;
    if (size == 0)
        return 0;

    ResidentBuffer resident;
    resident.size    = size;
    resident.buffer  = CreateBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VMA_MEMORY_USAGE_GPU_ONLY);
    resident.staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    const VkBufferDeviceAddressInfo addressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = resident.buffer.buffer };
    const VkDeviceAddress base = vkGetBufferDeviceAddress(device, &addressInfo);

    auto* mapped = static_cast<std::byte*>(resident.staging.info.pMappedData);
    size_t offset = 0;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        if (requests[i].bytes == 0)
            continue;
        offset = align(offset, requests[i].alignment);
        m_uploads.Copy(mapped + offset, requests[i].data, requests[i].bytes);   // the capture keeps them
        addresses[i] = base + offset;
        offset += requests[i].bytes;
    }
    m_uploads.Then([allocator = allocator, allocation = resident.staging.allocation]() { vmaFlushAllocation(allocator, allocation, 0, VK_WHOLE_SIZE); });

    const tachyon::RHI::ResidentBufferId id = m_nextResidentBuffer++;
    m_residentBuffers.emplace(id, resident);
    return id;
}

tachyon::RHI::ResidentState tachyon::vulkan::VulkanDeviceContext::GetResidentBufferState(tachyon::RHI::ResidentBufferId id) const
{
    const auto it = m_residentBuffers.find(id);
    if (it == m_residentBuffers.end())
        return tachyon::RHI::ResidentState::Gone;
    return it->second.ready ? tachyon::RHI::ResidentState::Ready : tachyon::RHI::ResidentState::Pending;
}

void tachyon::vulkan::VulkanDeviceContext::ReleaseResidentBuffer(tachyon::RHI::ResidentBufferId id)
{
    const auto it = m_residentBuffers.find(id);
    if (it == m_residentBuffers.end())
        return;
    const ResidentBuffer resident = it->second;
    m_residentBuffers.erase(it);
    if (!resident.ready)
    {
        // Never copied, so no command buffer refers to it.
        DestroyBuffer(resident.staging);
        DestroyBuffer(resident.buffer);
        return;
    }
    GetDeletionQueue(tachyon::DeviceScope::Frame).push_function([this, buffer = resident.buffer]() { DestroyBuffer(buffer); });
}

void tachyon::vulkan::VulkanDeviceContext::DestroyImage(tachyon::Image* img)
{
    VulkanImage* vulkanImage = (VulkanImage*)img;
    m_liveImages.erase(vulkanImage);
    DestroyImage(*vulkanImage);
}

void tachyon::vulkan::VulkanDeviceContext::ReleaseLiveImages()
{
    for (VulkanImage* image : m_liveImages)
    {
        DestroyImage(*image);
        image->image     = VK_NULL_HANDLE;
        image->imageView = VK_NULL_HANDLE;
        image->allocation = VK_NULL_HANDLE;
    }
}

void tachyon::vulkan::VulkanDeviceContext::RebuildLiveImages()
{
    for (VulkanImage* image : m_liveImages)
    {
        VulkanImage::Recipe recipe = std::move(image->recipe);
        const VkExtent3D extent = image->imageExtent;
        *image = CreateImage(extent, image->imageFormat, recipe.usage, ToVulkanSampler(recipe.sampler, this), recipe.mipmapped);
        image->recipe = std::move(recipe);
        const std::vector<uint8_t>& pixels = image->recipe.pixels;
        if (pixels.empty())
            continue;
        // A busy worker copies later, so it reads the image's own pixels (DestroyImage waits for it).
        Buffer staging = CreateBuffer(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_uploads.Copy(staging.info.pMappedData, pixels.data(), pixels.size());
        m_uploads.Then([allocator = allocator, allocation = staging.allocation]() { vmaFlushAllocation(allocator, allocation, 0, VK_WHOLE_SIZE); });
        m_pendingUploads.push_back({ staging, image->image, extent, image->recipe.mipmapped });
    }
}

void tachyon::vulkan::VulkanDeviceContext::DestroyImageAtEnd(tachyon::Image* img, tachyon::DeviceScope scope,
                                                    [[maybe_unused]] const std::source_location& loc)
{
    m_liveImages.erase((VulkanImage*)img);
    VulkanImage vulkanImage = *(VulkanImage*)img;
    auto func = [this, vulkanImage, img]()
        {
            //TACHYON_LOG_ERROR(kLogVulkan, "{}({}): {}", loc.file_name(), loc.line(), loc.function_name());
            DestroyImage(vulkanImage);
            delete img;
        };
    GetDeletionQueue(scope).push_function(func);
}

void tachyon::vulkan::VulkanDeviceContext::DestroyVisualColorStyles(tachyon::Buffer* buffer)
{
    VulkanBuffer* vulkanBuffer = (VulkanBuffer*)buffer;
    DestroyMesh(vulkanBuffer->visualColorStyleBuffers);
}

void tachyon::vulkan::VulkanDeviceContext::DestroyVisualColorStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope,
                                                     [[maybe_unused]] const std::source_location& loc)
{
    auto func = [this, buffer]()
        {
            //TACHYON_LOG_ERROR(kLogVulkan, "{}({}): {}", loc.file_name(), loc.line(), loc.function_name());
            DestroyVisualColorStyles(buffer);
            delete buffer;
        };
    GetDeletionQueue(scope).push_function(func);
}

void tachyon::vulkan::VulkanDeviceContext::DestroyVisualBoxStyles(tachyon::Buffer* buffer)
{
    VulkanBuffer* vulkanBuffer = (VulkanBuffer*)buffer;
    DestroyMesh(vulkanBuffer->visualBoxStyleBuffers);
}

void tachyon::vulkan::VulkanDeviceContext::DestroyVisualBoxStylesAtEnd(tachyon::Buffer* buffer, tachyon::DeviceScope scope,
                                                     [[maybe_unused]] const std::source_location& loc)
{
    auto func = [this, buffer]()
        {
            //TACHYON_LOG_ERROR(kLogVulkan, "{}({}): {}", loc.file_name(), loc.line(), loc.function_name());
            DestroyVisualBoxStyles(buffer);
            delete buffer;
        };
    GetDeletionQueue(scope).push_function(func);
}

void tachyon::vulkan::VulkanDeviceContext::DestroyImage(const VulkanImage& img)
{
    // Its staging copy may still be filled from its pixels.
    if (m_uploads.Busy())
        m_uploads.Wait();
    // An upload still queued for this image has nothing left to fill.
    std::erase_if(m_pendingUploads, [&](const PendingUpload& upload)
    {
        if (upload.image != img.image) return false;
        DestroyBuffer(upload.staging);
        return true;
    });
    vkDestroyImageView(device, img.imageView, AllocationCallbacks());
    vmaDestroyImage(allocator, img.image, img.allocation);
}

tachyon::vulkan::VulkanImage tachyon::vulkan::VulkanDeviceContext::CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkSampler sampler, bool mipmapped) const
{
    uint32_t mipLevels = !mipmapped ? 1 : static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;

    VkImageAspectFlags aspectFlag = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageCreateInfo img_info = tachyon::vulkan::image_create_info(format, usage, size);
    img_info.mipLevels = mipLevels;

    VmaAllocationCreateInfo allocinfo =
    {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
        .requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    VulkanImage newImage { .imageExtent = size, .imageFormat = format, .imageSampler = sampler };

    TACHYON_VK_CHECK(vmaCreateImage(allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

    static int imageCount = 0;
    static const char* imageNameList[20] =
    {
        "CreateImage1",
        "CreateImage2",
        "CreateImage3",
        "CreateImage4",
        "CreateImage5",
        "CreateImage6",
        "CreateImage7",
        "CreateImage8",
        "CreateImage9",
        "CreateImage10",
        "CreateImage11",
        "CreateImage12",
        "CreateImage13",
        "CreateImage14",
        "CreateImage15",
        "CreateImage16",
        "CreateImage17",
        "CreateImage18",
        "CreateImage19",
        "CreateImage20"
    };

    vmaSetAllocationName(allocator, newImage.allocation, imageNameList[imageCount]);
    imageCount = (imageCount + 1) % 20;

    VkImageViewCreateInfo view_info = tachyon::vulkan::imageview_create_info(format, newImage.image, aspectFlag);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    TACHYON_VK_CHECK(vkCreateImageView(device, &view_info, AllocationCallbacks(), &newImage.imageView));
    return newImage;
}

tachyon::vulkan::VisualColorStyleBuffers tachyon::vulkan::VulkanDeviceContext::UploadMesh(std::span<const tachyon::VisualColorStyle> visualColorStyles) const
{
    const size_t styleBufferSize = visualColorStyles.size() * sizeof(tachyon::VisualColorStyle);

    tachyon::vulkan::VisualColorStyleBuffers styles{};

    styles.colorStyleBuffer = CreateMappedStorageBuffer(visualColorStyles.data(), styleBufferSize, styles.colorStyleBufferAddress);

    return styles;
}

tachyon::vulkan::VisualBoxStyleBuffers tachyon::vulkan::VulkanDeviceContext::UploadMesh(std::span<const tachyon::VisualBoxStyle> visualBoxStyles) const
{
    const size_t styleBufferSize = visualBoxStyles.size() * sizeof(tachyon::VisualBoxStyle);

    tachyon::vulkan::VisualBoxStyleBuffers styles {};

    styles.boxStyleBuffer = CreateMappedStorageBuffer(visualBoxStyles.data(), styleBufferSize, styles.styleBufferAddress);

    return styles;
}

void tachyon::vulkan::VulkanDeviceContext::DestroyMesh(const tachyon::vulkan::VisualBoxStyleBuffers& mesh) const
{
    DestroyBuffer(mesh.boxStyleBuffer);
}

void tachyon::vulkan::VulkanDeviceContext::DestroyMesh(const tachyon::vulkan::VisualColorStyleBuffers& mesh) const
{
    DestroyBuffer(mesh.colorStyleBuffer);
}

VkShaderModule tachyon::vulkan::VulkanDeviceContext::CreateShader(std::span<uint32_t> spirvBuffer) const
{
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;

    // codeSize is in bytes.
    createInfo.codeSize = spirvBuffer.size() * sizeof(uint32_t);
    createInfo.pCode = spirvBuffer.data();

    VkShaderModule shaderModule = {};
    if (vkCreateShaderModule(device, &createInfo, AllocationCallbacks(), &shaderModule) != VK_SUCCESS)
    {
        return nullptr;
    }
    return shaderModule;
}

void tachyon::vulkan::VulkanDeviceContext::DestroyShader(const VkShaderModule& shader) const
{
    vkDestroyShaderModule(device, shader, AllocationCallbacks());
}

void* vmaGetMappedData(VmaAllocation allocation)
{
    return allocation->GetMappedData();
}
