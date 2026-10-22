#pragma once

// Font loading for the skribidi text system: the default and generic families,
// registered families, and the on-demand per-script fallback.

#include <skribidi/skb_font_collection.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace GUI
{
	// Owned by the context; the fallback callback reads and updates it.
	struct FontFallbackState
	{
		std::string                  fontDirectory { "fonts/Noto-otf" }; // the default fonts
		std::unordered_set<uint64_t> attempts; // (family, script) pairs already tried
	};

	bool skribidi_load_fonts(skb_font_collection_t* font_collection, FontFallbackState& fallback);
	// fontDirectory: where the base fonts that back up the faces are.
	bool skribidi_add_font_family(skb_font_collection_t* font_collection, uint8_t family, std::span<const std::string_view> faceFiles,
	                              std::string_view fontDirectory);
}
