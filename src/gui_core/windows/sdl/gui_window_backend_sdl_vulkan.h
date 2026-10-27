#pragma once

namespace tachyon
{
    class GraphicsContext;

    /// Lets a VulkanGraphicsContext create surfaces for SDL windows; other contexts are left alone.
    void InstallSDLVulkanSurfaceFactory(GraphicsContext* graphicsContext);
}
