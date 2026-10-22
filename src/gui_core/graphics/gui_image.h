#pragma once

#include <stdint.h>

namespace GUI
{
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

    enum class Sampler
    {
        UNDEFINED,

        NearestRepeat,
        LinearRepeat,
        LinearClampToEdge
    };

	// Opaque handle to an image resource. The actual implementation is provided by the graphics context.
    enum ImageIndex : uint32_t { Invalid = 0 };

	// Base class for images. The actual implementation is provided by the graphics context.
	class Image {};

	// Interface for managing a set of images. The actual implementation is provided by the graphics context.
	class ImageSet
	{
	public:
        virtual ~ImageSet() {}
		virtual void Clear() = 0;
		virtual ImageIndex Add(Image* image) = 0;
		virtual void Remove(ImageIndex index) = 0;
	};
}