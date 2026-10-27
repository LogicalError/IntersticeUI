#pragma once

#include <stdint.h>

namespace tachyon
{
    /// @addtogroup graphics
    /// @{

    /// Pixel formats for GraphicsContext::CreateImage, 8 bits per channel. The _SRGB
    /// formats decode sRGB to linear when sampled.
    enum class ImageFormat
    {
        UNDEFINED,
        R8,
        R8_SRGB,
        R8G8,
        R8G8_SRGB,
        R8G8B8,
        R8G8B8_SRGB,
        R8G8B8A8,
        R8G8B8A8_SRGB
    };

    /// How an image is filtered and addressed when sampled.
    enum class Sampler
    {
        UNDEFINED,

        NearestRepeat,
        LinearRepeat,
        LinearClampToEdge
    };

	/// Index of an image in the graphics context's image set, as the shaders sample it.
	/// Invalid (0) is no image.
    enum ImageIndex : uint32_t { Invalid = 0 };

	/// An image made by GraphicsContext::CreateImage; the backend defines its contents.
	class Image {};

	/// The images the shaders can sample, each at an ImageIndex. Implemented by the backend.
	class ImageSet
	{
	public:
        virtual ~ImageSet() {}
		/// Removes every image, freeing all indices.
		virtual void Clear() = 0;
		/// Adds an image (not owned) and returns its index.
		virtual ImageIndex Add(Image* image) = 0;
		/// Frees the index for reuse; the image itself stays.
		virtual void Remove(ImageIndex index) = 0;
	};
    /// @}
}