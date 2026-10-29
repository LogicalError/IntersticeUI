#include <functional>
#include <deque>
#include <chrono>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Undefine Windows macro that conflicts with our CreateWindow method
#undef CreateWindow
#endif

#include <VkBootstrap.h>
#define VMA_DEBUG_LOG_FORMAT
#include <vk_mem_alloc.h>

#include <gui_core/gui_base_types.h>
#include <gui_core/diagnostics/gui_profiler.h>

#include <gui_core/input/gui_platform_input_handler.h>

#include <gui_core/graphics/vulkan/vk_types.h>
#include <gui_core/graphics/vulkan/vk_initializers.h>
#include <gui_core/graphics/vulkan/vk_image.h>
#include <gui_core/graphics/vulkan/vk_images.h>
#include <gui_core/graphics/vulkan/vk_pipelines.h>
#include <gui_core/graphics/vulkan/vk_descriptors.h>
#include <gui_core/graphics/vulkan/vk_device_context.h>
#include <gui_core/graphics/vulkan/vk_graphics_context.h>
#include <gui_core/graphics/vulkan/vk_window_context.h>
#include <gui_core/graphics/vulkan/vk_defaults.h>
#include <gui_core/graphics/gui_graphics_context.h>

using namespace tachyon::vulkan;



FrameData& VulkanWindowContext::GetCurrentFrame()
{
    return state.frames[frameNumber % FRAME_OVERLAP];
}

FrameData& VulkanWindowContext::GetLastFrame()
{
    return state.frames[(frameNumber + FRAME_OVERLAP - 1) % FRAME_OVERLAP];
}

void VulkanWindowContext::CreateDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage, VkFormat colorFormat)
{
    VkExtent3D extent { windowExtent.width, windowExtent.height, 1 };

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    drawImage.imageFormat = colorFormat;
    drawImage.imageExtent = extent;
    VkImageUsageFlags drawUsages =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo drawInfo = tachyon::vulkan::image_create_info(colorFormat, drawUsages, extent);
    vmaCreateImage(deviceContext->allocator, &drawInfo, &allocInfo, &drawImage.image, &drawImage.allocation, nullptr);
    vmaSetAllocationName(deviceContext->allocator, drawImage.allocation, "Window::DrawImage::CreateImage");
    VkImageViewCreateInfo drawView = tachyon::vulkan::imageview_create_info(colorFormat, drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    TACHYON_VK_CHECK(vkCreateImageView(deviceContext->device, &drawView, deviceContext->AllocationCallbacks(), &drawImage.imageView));

    depthImage.imageFormat = depthImageFormat;
    depthImage.imageExtent = extent;
    VkImageCreateInfo depthInfo =
        tachyon::vulkan::image_create_info(depthImageFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, extent);
    vmaCreateImage(deviceContext->allocator, &depthInfo, &allocInfo, &depthImage.image, &depthImage.allocation, nullptr);
    vmaSetAllocationName(deviceContext->allocator, depthImage.allocation, "Window::DepthImage::CreateImage");
    VkImageViewCreateInfo depthView = tachyon::vulkan::imageview_create_info(depthImageFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    TACHYON_VK_CHECK(vkCreateImageView(deviceContext->device, &depthView, deviceContext->AllocationCallbacks(), &depthImage.imageView));
}

void VulkanWindowContext::DestroyDrawDepthImages(VulkanImage& drawImage, VulkanImage& depthImage)
{
    vkDestroyImageView(deviceContext->device, drawImage.imageView, deviceContext->AllocationCallbacks());
    vmaDestroyImage(deviceContext->allocator, drawImage.image, drawImage.allocation);
    drawImage = {};

    vkDestroyImageView(deviceContext->device, depthImage.imageView, deviceContext->AllocationCallbacks());
    vmaDestroyImage(deviceContext->allocator, depthImage.image, depthImage.allocation);
    depthImage = {};
}

void VulkanWindowContext::CreateSwapchain()
{
    uint32_t width = windowExtent.width;
    uint32_t height = windowExtent.height;

    needsReadback = false;
    
    // Layered windows (Windows) and render-to-image windows have no surface and no swapchain.
    if (surface == VK_NULL_HANDLE)
    {
        needsReadback = !renderToImage;   // layered windows read back every frame
        
        // Extent is still used for size checks.
        swapchain.swapchainExtent.width = width;
        swapchain.swapchainExtent.height = height;
        
        // BGRA8 matches the readback buffer, so readback needs no conversion.
        VkFormat layeredWindowFormat = VK_FORMAT_B8G8R8A8_UNORM;
        
        for (auto& frame : state.frames)
            CreateDrawDepthImages(frame.drawImage, frame.depthImage, layeredWindowFormat);

        VkDeviceSize bufferSize = width * height * 4;
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        
        vmaCreateBuffer(deviceContext->allocator, &bufferInfo, &allocInfo,
                       &readbackBuffer.buffer, &readbackBuffer.allocation, nullptr);
        vmaSetAllocationName(deviceContext->allocator, readbackBuffer.allocation, "Window::ReadbackBuffer");
        
        cachedBitmap.resize(bufferSize);
        return;
    }
    
    vkb::SwapchainBuilder swapchainBuilder{ deviceContext->chosenGPU, deviceContext->device, surface };
    swapchainBuilder.set_allocation_callbacks(const_cast<VkAllocationCallbacks*>(deviceContext->AllocationCallbacks()));

    // Prefer alpha compositing modes that allow transparent windows.
    VkSurfaceCapabilitiesKHR surfaceCaps{};
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(deviceContext->chosenGPU, surface, &surfaceCaps) == VK_SUCCESS)
    {
        const VkCompositeAlphaFlagsKHR supported = surfaceCaps.supportedCompositeAlpha;

        if ((supported & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if ((supported & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        else if ((supported & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if ((supported & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0)
            compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        // Opaque-only surfaces composite transparent windows via CPU readback.
        const bool isTransparent = (windowFlags & tachyon::WindowFlags::Transparent) != tachyon::WindowFlags::None;
        needsReadback = isTransparent && (compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
    }

    swapchain.swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    // Prefer MAILBOX: no tearing, and the newest frame replaces the waiting one,
    // so present doesn't block. The loop is then NOT paced to the refresh; it
    // runs as fast as build + render allow. It needs triple buffering. Otherwise
    // FIFO, double-buffered (the only guaranteed mode, blocking to the refresh)
    // rather than vk-bootstrap's larger default.
    VkPresentModeKHR presentMode   = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t         desiredImages = vkb::SwapchainBuilder::DOUBLE_BUFFERING;
    {
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(deviceContext->chosenGPU, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        if (modeCount > 0)
            vkGetPhysicalDeviceSurfacePresentModesKHR(deviceContext->chosenGPU, surface, &modeCount, modes.data());
        for (VkPresentModeKHR m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode   = VK_PRESENT_MODE_MAILBOX_KHR;
                desiredImages = vkb::SwapchainBuilder::TRIPLE_BUFFERING;
                break;
            }
    }

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = swapchain.swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_composite_alpha_flags(compositeAlpha)
        .set_desired_present_mode(presentMode)
        .set_desired_min_image_count(desiredImages)
        .set_desired_extent(width, height)
        .set_old_swapchain(swapchain.swapchain)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    swapchain.presentMode = vkbSwapchain.present_mode;

    swapchain.swapchainExtent = vkbSwapchain.extent;
    swapchain.swapchain = vkbSwapchain.swapchain;
    swapchain.swapchainImages = vkbSwapchain.get_images().value();
    swapchain.swapchainImageViews = vkbSwapchain.get_image_views().value();

    const VkSemaphoreCreateInfo semaphoreCreateInfo = tachyon::vulkan::semaphore_create_info();
    swapchain.presentSemaphores.resize(swapchain.swapchainImages.size());
    for (VkSemaphore& semaphore : swapchain.presentSemaphores)
        TACHYON_VK_CHECK(vkCreateSemaphore(deviceContext->device, &semaphoreCreateInfo, deviceContext->AllocationCallbacks(), &semaphore));

    // Per-slot draw + depth images so the FRAME_OVERLAP in-flight frames don't
    // share them (frame N's blit-read must not race frame N+1's transition-write).
    for (auto& frame : state.frames)
        CreateDrawDepthImages(frame.drawImage, frame.depthImage, drawImageFormat);

    if (needsReadback)
    {
        VkDeviceSize bufferSize = windowExtent.width * windowExtent.height * 4; // BGRA
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        
        vmaCreateBuffer(deviceContext->allocator, &bufferInfo, &allocInfo,
                       &readbackBuffer.buffer, &readbackBuffer.allocation, nullptr);
        vmaSetAllocationName(deviceContext->allocator, readbackBuffer.allocation, "Window::ReadbackBuffer");
        
        cachedBitmap.resize(bufferSize);
    }
}

void VulkanWindowContext::DestroySwapchain()
{
    for (size_t i = 0; i < swapchain.swapchainImageViews.size(); i++)
    {
        vkDestroyImageView(deviceContext->device, swapchain.swapchainImageViews[i], deviceContext->AllocationCallbacks());
        swapchain.swapchainImageViews[i] = VK_NULL_HANDLE;
    }
    swapchain.swapchainImageViews.clear();
    swapchain.swapchainImages.clear();

    for (VkSemaphore semaphore : swapchain.presentSemaphores)
        vkDestroySemaphore(deviceContext->device, semaphore, deviceContext->AllocationCallbacks());
    swapchain.presentSemaphores.clear();

    vkDestroySwapchainKHR(deviceContext->device, swapchain.swapchain, deviceContext->AllocationCallbacks());
    swapchain.swapchain = VK_NULL_HANDLE;

    for (auto& frame : state.frames)
        DestroyDrawDepthImages(frame.drawImage, frame.depthImage);

    if (readbackBuffer.buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(deviceContext->allocator, readbackBuffer.buffer, readbackBuffer.allocation);
        readbackBuffer.buffer = VK_NULL_HANDLE;
        readbackBuffer.allocation = VK_NULL_HANDLE;
    }
    cachedBitmap.clear();
}

void VulkanWindowContext::ResizeSwapchain(tachyon::int2 size)
{
    WaitForFence(deviceContext->device, deviceContext->immFence);

    for (auto& frame : state.frames)
    {
        WaitForFence(deviceContext->device, frame.renderFence);
        frame.flushQueue.flush();
    }

    // No fence covers a present's semaphore wait; drain the queue so the old
    // swapchain's present semaphores can be destroyed below.
    {
        auto queueLock = deviceContext->LockQueue();
        TACHYON_VK_CHECK(vkQueueWaitIdle(deviceContext->graphicsQueue));
    }

    VkSwapchainKHR oldSwapchain = swapchain.swapchain;
    std::vector<VkImageView> oldSwapchainImageViews = std::move(swapchain.swapchainImageViews);
    for (VkSemaphore semaphore : swapchain.presentSemaphores)
        vkDestroySemaphore(deviceContext->device, semaphore, deviceContext->AllocationCallbacks());
    swapchain.presentSemaphores.clear();
    std::array<VulkanImage, FRAME_OVERLAP> oldDrawImages;
    std::array<VulkanImage, FRAME_OVERLAP> oldDepthImages;
    for (unsigned int i = 0; i < FRAME_OVERLAP; ++i)
    {
        oldDrawImages[i]  = state.frames[i].drawImage;
        oldDepthImages[i] = state.frames[i].depthImage;
    }
    AllocatedBuffer oldReadbackBuffer = readbackBuffer;

    if (size.x <= 0 || size.y <= 0)
    {
        // Minimize: don't recreate the swapchain, but still queue the old
        // resources for deletion below. Null the live handles (copied into the
        // old* locals above) so a later DestroySwapchain doesn't double-free.
        freeze_rendering = true;
        swapchain.swapchain = VK_NULL_HANDLE;
        for (auto& frame : state.frames)
        {
            frame.drawImage  = {};
            frame.depthImage = {};
        }
        readbackBuffer   = {};
    }
    else
    {
        freeze_rendering = false;
        windowExtent.width = size.x;
        windowExtent.height = size.y;

        CreateSwapchain();
    }

    GetCurrentFrame().flushQueue.push_function([
        device = deviceContext->device,
        callbacks = deviceContext->AllocationCallbacks(),
        allocator = deviceContext->allocator,
        oldSwapchain,
        oldSwapchainImageViews = std::move(oldSwapchainImageViews),
        oldDrawImages,
        oldDepthImages,
        oldReadbackBuffer]()
        {
            for (auto imageView : oldSwapchainImageViews)
            {
                vkDestroyImageView(device, imageView, callbacks);
            }

            for (const auto& img : oldDrawImages)
            {
                if (img.imageView != VK_NULL_HANDLE)
                    vkDestroyImageView(device, img.imageView, callbacks);
                if (img.image != VK_NULL_HANDLE)
                    vmaDestroyImage(allocator, img.image, img.allocation);
            }
            for (const auto& img : oldDepthImages)
            {
                if (img.imageView != VK_NULL_HANDLE)
                    vkDestroyImageView(device, img.imageView, callbacks);
                if (img.image != VK_NULL_HANDLE)
                    vmaDestroyImage(allocator, img.image, img.allocation);
            }

            if (oldReadbackBuffer.buffer != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(allocator, oldReadbackBuffer.buffer, oldReadbackBuffer.allocation);
            }

            if (oldSwapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(device, oldSwapchain, callbacks);
            }
        });
}

void VulkanWindowContext::FlushWindowContext()
{
    deviceContext->WaitIdle();
    WaitForFence(deviceContext->device, deviceContext->immFence);

    for (auto& frame : state.frames)
    {
        WaitForFence(deviceContext->device, frame.renderFence);
        frame.flushQueue.flush();
    }
}


VkCommandBuffer VulkanWindowContext::BeginWindowFrame(FrameData& currentFrame)
{
    WaitForFence(deviceContext->device, currentFrame.renderFence);
    currentFrame.flushQueue.flush();
    if (currentFrame.timestampsWritten)
    {
        // That frame is done (its fence), so its timestamps are there.
        uint64_t ticks[2] = {};
        if (vkGetQueryPoolResults(deviceContext->device, currentFrame.timestamps, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            lastGpuFrameSeconds = double(ticks[1] - ticks[0]) * double(timestampPeriodNs) * 1e-9;
        currentFrame.timestampsWritten = false;
    }
    currentFrame.primitiveUploadOffset = 0;
    currentFrame.frameDescriptors.clear_pools(deviceContext->device);
    
    if (swapchain.swapchain != VK_NULL_HANDLE)
    {
        // The swapchain image is acquired late, in EndWindowFrame; drawExtent
        // only needs the swapchain extent.
        drawExtent.height = uint32_t(std::min(swapchain.swapchainExtent.height, currentFrame.drawImage.imageExtent.height) * 1.f);
        drawExtent.width  = uint32_t(std::min(swapchain.swapchainExtent.width,  currentFrame.drawImage.imageExtent.width) * 1.f);
    }
    else
    {
        drawExtent.width = currentFrame.drawImage.imageExtent.width;
        drawExtent.height = currentFrame.drawImage.imageExtent.height;
    }

    TACHYON_VK_CHECK(vkResetFences(deviceContext->device, 1, &currentFrame.renderFence));
    TACHYON_VK_CHECK(vkResetCommandBuffer(currentFrame.mainCommandBuffer, 0));

    state.commandBuffer = currentFrame.mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = tachyon::vulkan::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    TACHYON_VK_CHECK(vkBeginCommandBuffer(state.commandBuffer, &cmdBeginInfo));

    if (currentFrame.timestamps != VK_NULL_HANDLE)
    {
        vkCmdResetQueryPool(state.commandBuffer, currentFrame.timestamps, 0, 2);
        vkCmdWriteTimestamp2(state.commandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, currentFrame.timestamps, 0);
    }

    // Queued texture uploads (e.g. glyph atlas pages) run ahead of this frame's drawing.
    deviceContext->RecordPendingUploads(state.commandBuffer);

    tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    // Layered and render-to-image windows clear to transparent, others to opaque black.
    float clearAlpha = (needsReadback || renderToImage) ? 0.0f : 1.0f;
    VkClearValue clearValue = { .color = { { 0.0f, 0.0f, 0.0f, clearAlpha } } };

    VkRenderingAttachmentInfo colorAttachment = tachyon::vulkan::attachment_info(currentFrame.drawImage.imageView, &clearValue, VK_IMAGE_LAYOUT_GENERAL);
    VkRenderingAttachmentInfo depthAttachment = tachyon::vulkan::depth_attachment_info(currentFrame.depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = tachyon::vulkan::rendering_info(drawExtent, &colorAttachment, &depthAttachment);

    vkCmdBeginRendering(state.commandBuffer, &renderInfo);


    return state.commandBuffer;
}


void VulkanWindowContext::EndWindowFrame(FrameData& currentFrame)
{
    // What the frame reads from mapped memory was copied on the upload worker while its
    // commands were recorded; the submit below needs it done. Part of building the frame.
    deviceContext->WaitForUploads();

    // Build time = CPU work since the previous present returned, captured
    // before this frame blocks on its swapchain image and present.
    if (havePresentEnd)
    {
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - lastPresentEnd).count();
        lastBuildValid = (dt > 0.0 && dt < 0.25);
        if (lastBuildValid)
            lastBuildSeconds = dt;
    }
    vkCmdEndRendering(state.commandBuffer);
    if (currentFrame.timestamps != VK_NULL_HANDLE)
    {
        vkCmdWriteTimestamp2(state.commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, currentFrame.timestamps, 1);
        currentFrame.timestampsWritten = true;
    }

    if (swapchain.swapchain != VK_NULL_HANDLE)
    {
        // Late acquire, after the offscreen frame is recorded: lower display latency.
        VkResult acq = vkAcquireNextImageKHR(deviceContext->device, swapchain.swapchain, 1000000000, currentFrame.swapchainSemaphore, nullptr, &swapchain.swapchainImageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_TIMEOUT || acq == VK_NOT_READY)
        {
            // No image: still submit the offscreen work so renderFence (already
            // reset by BeginWindowFrame) signals for the next frame's wait.
            if (acq == VK_ERROR_OUT_OF_DATE_KHR)
                resize_requested = true;
            TACHYON_VK_CHECK(vkEndCommandBuffer(state.commandBuffer));
            VkCommandBufferSubmitInfo abortCmd = tachyon::vulkan::command_buffer_submit_info(state.commandBuffer);
            VkSubmitInfo2 abortSubmit = tachyon::vulkan::submit_info(&abortCmd, nullptr, nullptr);
            {
                auto queueLock = deviceContext->LockQueue();
                TACHYON_VK_CHECK(vkQueueSubmit2(deviceContext->graphicsQueue, 1, &abortSubmit, currentFrame.renderFence));
            }
            frameNumber++;
            return;   // nothing to present
        }
        if (acq == VK_SUBOPTIMAL_KHR)
            resize_requested = true;
        else
            TACHYON_VK_CHECK(acq);

        tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        // After the acquire semaphore's wait (ALL_COMMANDS, below), not racing it.
        tachyon::vulkan::transition_image(state.commandBuffer, swapchain.swapchainImages[swapchain.swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

        tachyon::vulkan::copy_image_to_image(state.commandBuffer, currentFrame.drawImage.image, swapchain.swapchainImages[swapchain.swapchainImageIndex], drawExtent, swapchain.swapchainExtent);

        tachyon::vulkan::transition_image(state.commandBuffer, swapchain.swapchainImages[swapchain.swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    else if (renderToImage)
    {
        // Ready to read back, and the source of the copy into the host's image.
        tachyon::vulkan::transition_image(state.commandBuffer, currentFrame.drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        if (targetImage != VK_NULL_HANDLE)
        {
            // The host reads the image in its fragment shaders after this submission,
            // and moves it out of SHADER_READ_ONLY only after those reads.
            TransitionTarget(state.commandBuffer, targetWritten ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                             VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            tachyon::vulkan::copy_image_to_image(state.commandBuffer, currentFrame.drawImage.image, targetImage, drawExtent, targetExtent);
            TransitionTarget(state.commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            targetWritten = true;
        }
    }
    
    TACHYON_VK_CHECK(vkEndCommandBuffer(state.commandBuffer));

    // Wait on the acquire semaphore, signal the image's present semaphore.
    VkCommandBufferSubmitInfo cmdinfo = tachyon::vulkan::command_buffer_submit_info(state.commandBuffer);

    VkSemaphoreSubmitInfo waitInfo{};
    VkSemaphoreSubmitInfo signalInfo{};
    VkSubmitInfo2 submit;
    if (swapchain.swapchain != VK_NULL_HANDLE)
    {
        // Wait on the acquire semaphore at ALL_COMMANDS: the swapchain image's
        // first use this frame is the TRANSFER-stage transition/blit above, not a
        // color attachment, so waiting at COLOR_ATTACHMENT_OUTPUT lets the
        // transition race the acquire (sync-validation WRITE_AFTER_READ).
        waitInfo = tachyon::vulkan::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, currentFrame.swapchainSemaphore);
        signalInfo = tachyon::vulkan::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                                   swapchain.presentSemaphores[swapchain.swapchainImageIndex]);
        submit = tachyon::vulkan::submit_info(&cmdinfo, &signalInfo, &waitInfo);
    }
    else
    {
        submit = tachyon::vulkan::submit_info(&cmdinfo, nullptr, nullptr);
    }

    {
        auto queueLock = deviceContext->LockQueue();
        TACHYON_VK_CHECK(vkQueueSubmit2(deviceContext->graphicsQueue, 1, &submit, currentFrame.renderFence));
    }


    presentPending = swapchain.swapchain != VK_NULL_HANDLE;
    readbackPending = needsReadback ? &currentFrame : nullptr;
    frameNumber++;
}

void VulkanWindowContext::PresentWindowFrame()
{
    // Present waits on the image's present semaphore (rendering finished).
    if (presentPending)
    {
        presentPending = false;
        VkPresentInfoKHR presentInfo = tachyon::vulkan::present_info();

        presentInfo.pSwapchains = &swapchain.swapchain;
        presentInfo.swapchainCount = 1;

        presentInfo.pWaitSemaphores = &swapchain.presentSemaphores[swapchain.swapchainImageIndex];
        presentInfo.waitSemaphoreCount = 1;

        presentInfo.pImageIndices = &swapchain.swapchainImageIndex;

        VkResult presentResult;
        {
            // Under FIFO present usually blocks until a vsync frees an image (some
            // drivers stall in the acquire or frame fence instead); MAILBOX doesn't
            // block. Timed as a wait, not work.
            TACHYON_PROFILE_SCOPE_WAIT("Present (vsync wait)");
            auto queueLock = deviceContext->LockQueue();
            presentResult = vkQueuePresentKHR(deviceContext->graphicsQueue, &presentInfo);
        }
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
            resize_requested = true;
        else
            TACHYON_VK_CHECK(presentResult);
    }

    if (FrameData* frame = std::exchange(readbackPending, nullptr))
    {
        WaitForFence(deviceContext->device, frame->renderFence);
        PerformReadback(frame->drawImage);
    }

    // Layered windows don't redraw on SDL expose events, so update them explicitly.
    if ((windowFlags & tachyon::WindowFlags::Transparent) != tachyon::WindowFlags::None)
        UpdateTransparentWindow(recordedPosition);

    // Build time is measured from here to the next frame's submit.
    lastPresentEnd = std::chrono::steady_clock::now();
    havePresentEnd = true;
}

void VulkanWindowContext::PerformReadback(const VulkanImage& drawImage)
{
    VkCommandBuffer cmd = deviceContext->immCommandBuffer;
    TACHYON_VK_CHECK(vkResetFences(deviceContext->device, 1, &deviceContext->immFence));
    TACHYON_VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo beginInfo = tachyon::vulkan::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    TACHYON_VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // With a swapchain or as a render-to-image window, EndWindowFrame already left
    // drawImage in TRANSFER_SRC_OPTIMAL; layered windows still have it in GENERAL.
    if (swapchain.swapchain == VK_NULL_HANDLE && !renderToImage)
    {
        tachyon::vulkan::transition_image(cmd, drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {drawExtent.width, drawExtent.height, 1};

    vkCmdCopyImageToBuffer(cmd, drawImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          readbackBuffer.buffer, 1, &region);

    // Make the copy visible to the host read below (the fence alone doesn't).
    VkMemoryBarrier2 toHost { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    toHost.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toHost.dstStageMask  = VK_PIPELINE_STAGE_2_HOST_BIT;
    toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers    = &toHost;
    vkCmdPipelineBarrier2(cmd, &dependency);

    TACHYON_VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo = tachyon::vulkan::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = tachyon::vulkan::submit_info(&cmdInfo, nullptr, nullptr);

    {
        auto queueLock = deviceContext->LockQueue();
        TACHYON_VK_CHECK(vkQueueSubmit2(deviceContext->graphicsQueue, 1, &submit, deviceContext->immFence));
    }
    WaitForFence(deviceContext->device, deviceContext->immFence);

    // Map and copy to CPU cache; invalidate first in case the memory is cached but not coherent.
    TACHYON_VK_CHECK(vmaInvalidateAllocation(deviceContext->allocator, readbackBuffer.allocation, 0, VK_WHOLE_SIZE));
    void* mapped;
    vmaMapMemory(deviceContext->allocator, readbackBuffer.allocation, &mapped);
    memcpy(cachedBitmap.data(), mapped, cachedBitmap.size());
    vmaUnmapMemory(deviceContext->allocator, readbackBuffer.allocation);
}

bool VulkanWindowContext::ReadPixels(std::span<uint8_t> bgra)
{
    if (!renderToImage || frameNumber == 0 || freeze_rendering)
        return false;
    FrameData& last = GetLastFrame();
    if (bgra.size() < size_t(drawExtent.width) * drawExtent.height * 4 || readbackBuffer.buffer == VK_NULL_HANDLE)
        return false;
    WaitForFence(deviceContext->device, last.renderFence);
    PerformReadback(last.drawImage);
    std::memcpy(bgra.data(), cachedBitmap.data(), size_t(drawExtent.width) * drawExtent.height * 4);
    return true;
}

void VulkanWindowContext::SetImageTarget(VkImage image, VkExtent2D extent)
{
    targetImage   = image;
    targetExtent  = extent;
    targetWritten = false;
}

void VulkanWindowContext::TransitionTarget(VkCommandBuffer cmd, VkImageLayout from, VkImageLayout to,
                                           VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                           VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask        = srcStage;
    barrier.srcAccessMask       = srcAccess;
    barrier.dstStageMask        = dstStage;
    barrier.dstAccessMask       = dstAccess;
    barrier.oldLayout           = from;
    barrier.newLayout           = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = targetImage;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkDependencyInfo dependency{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

void VulkanWindowContext::UpdateLayeredWindowFromCache([[maybe_unused]] int screenPosX, [[maybe_unused]] int screenPosY)
{
#ifdef _WIN32
    if (drawExtent.width == 0 || drawExtent.height == 0)
        return;

    HWND hwnd = (HWND)nativeHandle;
    if (!hwnd)
        return;
    
    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen)
        return;
    
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem)
    {
        ReleaseDC(NULL, hdcScreen);
        return;
    }
    
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = drawExtent.width;
    bmi.bmiHeader.biHeight = -(int)drawExtent.height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbmp)
    {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return;
    }
    
    if (!bits)
    {
        DeleteObject(hbmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return;
    }
    
    // RGB is already premultiplied by the shader.
    uint32_t pixelCount = drawExtent.width * drawExtent.height;
    uint8_t* src = cachedBitmap.data();
    uint8_t* dst = (uint8_t*)bits;
    
    for (uint32_t i = 0; i < pixelCount; i++)
    {
        uint8_t b = src[i * 4 + 0];
        uint8_t g = src[i * 4 + 1];
        uint8_t r = src[i * 4 + 2];
        uint8_t a = src[i * 4 + 3];
        
        dst[i * 4 + 0] = b;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = r;
        dst[i * 4 + 3] = a;
    }
    
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hbmp);
    if (!hOldBmp || hOldBmp == HGDI_ERROR)
    {
        DeleteObject(hbmp);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return;
    }
    
    POINT ptSrc = {0, 0};
    SIZE szWnd = {(LONG)drawExtent.width, (LONG)drawExtent.height};
    POINT ptDst = { screenPosX, screenPosY };
    
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &szWnd, hdcMem, &ptSrc, 0, &blend, ULW_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
#endif
}

void VulkanWindowContext::UpdateTransparentWindow(tachyon::int2 screenPos)
{
    if (needsReadback && !cachedBitmap.empty())
    {
        UpdateLayeredWindowFromCache(screenPos.x, screenPos.y);
    }
}

void VulkanWindowContext::RenderWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing)
{
    RecordFrame(graphicsContext, rect, resizing);
    PresentWindowFrame();
}

void VulkanWindowContext::RecordWindowFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect)
{
    RecordFrame(graphicsContext, rect, false);
}

void VulkanWindowContext::RecordFrame(tachyon::GraphicsContext* graphicsContext, tachyon::IntRect rect, bool resizing)
{
    // A frame recorded and not presented yet (a live-resize redraw in between) goes first,
    // or its acquired image is never given back.
    if (presentPending || readbackPending)
        PresentWindowFrame();
    recordedPosition = rect.position;
    if (onGui && graphicsContext != nullptr)
    {
        uint32_t requestedWidth  = rect.size.x > 0 ? static_cast<uint32_t>(rect.size.x) : 0u;
        uint32_t requestedHeight = rect.size.y > 0 ? static_cast<uint32_t>(rect.size.y) : 0u;

        // All FRAME_OVERLAP draw images share one extent, so any slot's is representative.
        if (requestedWidth > swapchain.swapchainExtent.width ||
            requestedHeight > swapchain.swapchainExtent.height ||
            requestedWidth > GetCurrentFrame().drawImage.imageExtent.width ||
            requestedHeight > GetCurrentFrame().drawImage.imageExtent.height)
        {
            resize_requested = true;
        }

		tachyon::vulkan::VulkanGraphicsContext* vulkanGraphicsContext = dynamic_cast<tachyon::vulkan::VulkanGraphicsContext*>(graphicsContext);

        if (resizing)
        {
            bool hasResizeTarget = vulkanGraphicsContext->StartResizing(id);

            // Resize must update layout immediately, even without a resize target.
            vulkanGraphicsContext->Reset();

            onGui();

            // onGui() may have changed VisualColorStyles; re-upload before rendering.
            vulkanGraphicsContext->RePrepareFrame();

            if (hasResizeTarget)
            {
                vulkanGraphicsContext->StopResizing();
            }
        }

        // Resize must also run while frozen: restoring a minimized window is
        // the operation that recreates its swapchain and unfreezes rendering.
        if (resize_requested)
        {
            TACHYON_PROFILE_SCOPE("ResizeSwapchain");
            ResizeSwapchain(rect.size);
            resize_requested = false;
        }

        // A zero-size resize (minimize) freezes rendering and clears the live
        // draw/depth images. Do not record this frame against cleared handles.
        if (!freeze_rendering)
        {
            TACHYON_PROFILE_SCOPE("Window render");

            FrameData* currentFrame;
            {
                TACHYON_PROFILE_SCOPE("BeginWindowFrame");
                currentFrame = &GetCurrentFrame();
            }
            // BeginWindowFrame sets drawExtent.
            VkCommandBuffer cmd;
            {
                TACHYON_PROFILE_SCOPE("BeginWindowFrame");
                cmd = BeginWindowFrame(*currentFrame);
            }
            // A zero-extent frame still begins the command buffer, so it must still be ended.
            if (cmd != nullptr)
            {
                if (drawExtent.width != 0 && drawExtent.height != 0)
                {
                    TACHYON_PROFILE_SCOPE("Record commands");
                    vulkanGraphicsContext->RenderWindowFrame(cmd, *currentFrame, id, { drawExtent.width, drawExtent.height });
                }
                {
                    TACHYON_PROFILE_SCOPE("EndWindowFrame");
                    EndWindowFrame(*currentFrame);
                }
                if (lastBuildValid)
                    vulkanGraphicsContext->SetFrameBuildSeconds(static_cast<float>(lastBuildSeconds));
            }
        }
    }
}

VulkanWindowContext* VulkanWindowContext::Create(tachyon::ID id, void* nativeHandle, tachyon::OSWindowHandle osHandle, tachyon::int2 size, tachyon::WindowFlags windowFlags, VulkanDeviceContext* deviceContext, tachyon::OnGuiFunc onGUI)
{
    const bool renderToImage = (windowFlags & tachyon::WindowFlags::RenderToImage) != tachyon::WindowFlags::None;
    if (osHandle == nullptr && !renderToImage)
        return nullptr;

    auto context = new tachyon::vulkan::VulkanWindowContext();
    context->id = id;
    context->deviceContext = deviceContext;
    context->onGui = std::move(onGUI);
    context->windowFlags = windowFlags;
    context->osHandle = osHandle;
    context->nativeHandle = nativeHandle;
    context->renderToImage = renderToImage;

    // On Windows, layered (WS_EX_LAYERED) windows get no surface and present via
    // UpdateLayeredWindow; elsewhere transparent windows use surface alpha compositing.
    bool shouldCreateSurface = !context->renderToImage;
#ifdef _WIN32
    if (nativeHandle)
    {
        HWND hwnd = (HWND)nativeHandle;
        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_LAYERED)
        {
            shouldCreateSurface = false;
            context->needsReadback = true;
        }
    }
#endif
    
    if (shouldCreateSurface)
    {
        context->surface = context->deviceContext->CreateSurface(context->osHandle);
    } else
    {
        context->surface = VK_NULL_HANDLE;
    }
    
    context->windowExtent = VkExtent2D(size.x, size.y);
    context->drawExtent = VkExtent2D(size.x, size.y);
        
    context->CreateSwapchain();


    // Commands

    for (uint32_t i = 0; i < tachyon::vulkan::FRAME_OVERLAP; i++)
    {
        TACHYON_VK_CHECK(vkCreateCommandPool(context->deviceContext->device, &context->deviceContext->commandPoolInfo, context->deviceContext->AllocationCallbacks(), &context->state.frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = tachyon::vulkan::command_buffer_allocate_info(context->state.frames[i].commandPool, 1);

        TACHYON_VK_CHECK(vkAllocateCommandBuffers(context->deviceContext->device, &cmdAllocInfo, &context->state.frames[i].mainCommandBuffer));

        context->state.frames[i].deletionQueue.push_function([=]()
                                                    {
                                                        vkDestroyCommandPool(context->deviceContext->device, context->state.frames[i].commandPool, context->deviceContext->AllocationCallbacks());
                                                    });
    }

    // GPU frame timing, where the graphics queue can time.
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context->deviceContext->chosenGPU, &properties);
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(context->deviceContext->chosenGPU, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(context->deviceContext->chosenGPU, &familyCount, families.data());
        const uint32_t family = context->deviceContext->graphicsQueueFamily;
        if (family < familyCount && families[family].timestampValidBits != 0 && properties.limits.timestampPeriod > 0.0f)
            context->timestampPeriodNs = properties.limits.timestampPeriod;
    }

    // Per frame data

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++)
    {
        auto& frame = context->state.frames[i];
        if (context->timestampPeriodNs > 0.0f)
        {
            const VkQueryPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
            TACHYON_VK_CHECK(vkCreateQueryPool(context->deviceContext->device, &poolInfo, context->deviceContext->AllocationCallbacks(), &frame.timestamps));
            frame.deletionQueue.push_function([=]()
                                              {
                                                  vkDestroyQueryPool(context->deviceContext->device, frame.timestamps, context->deviceContext->AllocationCallbacks());
                                              });
        }
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes =
        {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        };

        frame.frameDescriptors = DescriptorAllocatorGrowable{};
        frame.frameDescriptors.init(context->deviceContext->device, 1000, frame_sizes, context->deviceContext->AllocationCallbacks());

        TACHYON_VK_CHECK(vkCreateFence(context->deviceContext->device, &context->deviceContext->fenceCreateInfo, context->deviceContext->AllocationCallbacks(), &frame.renderFence));

        VkSemaphoreCreateInfo semaphoreCreateInfo = tachyon::vulkan::semaphore_create_info();

        TACHYON_VK_CHECK(vkCreateSemaphore(context->deviceContext->device, &semaphoreCreateInfo, context->deviceContext->AllocationCallbacks(), &frame.swapchainSemaphore));

        frame.deletionQueue.push_function([=]()
                                          {
                                              vkDestroySemaphore(context->deviceContext->device, frame.swapchainSemaphore, context->deviceContext->AllocationCallbacks());

                                              vkDestroyFence(context->deviceContext->device, frame.renderFence, context->deviceContext->AllocationCallbacks());

                                              context->state.frames[i].frameDescriptors.clear_pools(context->deviceContext->device);
                                              context->state.frames[i].frameDescriptors.destroy_pools(context->deviceContext->device);
                                          });
    }
    return context;
}

void VulkanWindowContext::Destroy()
{
    // Frames still in flight use this window's fences, semaphores and command
    // pools; no fence covers a present's semaphore wait, so drain the queue too.
    for (auto& frame : state.frames)
        WaitForFence(deviceContext->device, frame.renderFence);
    {
        auto queueLock = deviceContext->LockQueue();
        TACHYON_VK_CHECK(vkQueueWaitIdle(deviceContext->graphicsQueue));
    }

    for (auto& frame : state.frames)
    {
        frame.flushQueue.flush();
        if (frame.primitiveUploadBuffer.buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(deviceContext->allocator,
                             frame.primitiveUploadBuffer.buffer,
                             frame.primitiveUploadBuffer.allocation);
            frame.primitiveUploadBuffer = {};
            frame.primitiveUploadAddress = 0;
            frame.primitiveUploadCapacity = 0;
            frame.primitiveUploadOffset = 0;
        }
        frame.deletionQueue.flush();
    }

    DestroySwapchain();

    vkDestroySurfaceKHR(deviceContext->instance, surface, deviceContext->AllocationCallbacks());
}
