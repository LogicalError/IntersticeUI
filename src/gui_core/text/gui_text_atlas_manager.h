#pragma once

#include <gui_core/gui_base_types.h>

namespace tachyon
{
    /// @addtogroup text
    /// @{

	enum ImageIndex : uint32_t; ///< defined in gui_image.h
	class GraphicsContext;
	class ImageSet;

	enum class TextureIndex : uint32_t { Invalid = 0 };

	class TextAtlasManager
	{
	public:
		virtual ~TextAtlasManager() {}

		virtual bool Initialize(GraphicsContext* graphicsContext, ImageSet* imageSet) = 0;

		virtual TextureIndex CreateTexture(const uint8_t* imageData, uint2 imageSize, uint8_t bpp) = 0;
		virtual void UpdateTexture(TextureIndex textureIndex, const uint8_t* imageData, uint2 imageSize) = 0;

		virtual ImageIndex GetImageIndex(TextureIndex textureIndex) = 0;
		virtual void SyncTextureIndices() = 0;
	};
    /// @}
}
