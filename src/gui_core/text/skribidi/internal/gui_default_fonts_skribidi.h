#pragma once

// Font loading for the skribidi text system: the built-in and generic families,
// registered families, and the optional on-demand per-script fallback.

#include <gui_core/text/gui_text_system.h> // FontFaceSource

#include <skribidi/skb_font_collection.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace tachyon
{
	// Owned by the context; the fallback callback reads and updates it.
	struct FontFallbackState
	{
		std::string                     defaultDirectory { "fonts" }; // built-in fonts; empty: none
		std::string                     fallbackDirectory;            // optional fallback fonts (Noto); empty: none
		std::unordered_set<uint64_t>    attempts;     // (family, script) pairs already tried
		std::unordered_set<std::string> missing;      // optional fonts already reported missing
	};

	// A registered face, kept to rebuild the collection: its file, or its data in memory.
	struct StoredFontFace
	{
		std::string                file;
		std::span<const std::byte> data;

		bool operator==(const StoredFontFace& other) const
		{
			return file == other.file && data.data() == other.data.data() && data.size() == other.data.size();
		}
	};

	bool skribidi_load_fonts(skb_font_collection_t* font_collection, FontFallbackState& fonts);
	// The family's faces, then what backs them up for codepoints they lack. False when no face loaded.
	bool skribidi_add_font_family(skb_font_collection_t* font_collection, uint8_t family, std::span<const FontFaceSource> faces,
	                              FontFallbackState& fonts);
}
