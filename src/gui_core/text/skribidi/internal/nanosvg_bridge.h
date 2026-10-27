#pragma once

#include <skribidi/skb_icon_collection.h>

#include <cstddef>

namespace tachyon
{
	// Parses an SVG document via NanoSVG and constructs an icon in the given
	// skribidi icon_collection by driving skb_icon_builder. Strokes are
	// tessellated to filled outlines on this side (skribidi's icon model has
	// no native stroke representation).
	//
	// Returns the new icon's handle, or 0 on parse / allocation failure.
	skb_icon_handle_t RegisterNanoSVG(skb_icon_collection_t* collection,
	                                  const char* name,
	                                  const char* svg_data,
	                                  size_t svg_size);
}
