#pragma once

namespace tachyon::vulkan
{
    constexpr VkFormat drawImageFormat = VK_FORMAT_B8G8R8A8_UNORM; ///< format of the images the GUI draws into
    constexpr VkFormat depthImageFormat = VK_FORMAT_D32_SFLOAT;
}