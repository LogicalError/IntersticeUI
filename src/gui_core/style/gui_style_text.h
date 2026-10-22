#pragma once

#include <stdint.h>
#include <cstring>
#include <string_view>

namespace GUI
{
    enum class TextID : uint32_t { Invalid = 0 };
	enum class IconID : uint32_t { Invalid = 0 };

    // Handle for a font family registered with ITextSystem::RegisterFontFamily().
    // Derived from the family name, so styles can name a family before it is registered.
    // Use FontID::Invalid for the default / family-based font selection.
    enum class FontID : uint32_t { Invalid = 0 };

    // Case-insensitive, like CSS family names. Returns FontID::Invalid for an empty name.
    FontID FontIDFromName(std::string_view familyName);

    // A BCP 47 language tag ("ja", "zh-Hant", "ko-KR") stored inline, so text
    // styles stay plain values. Empty = no language of its own. A tag that
    // doesn't fit is dropped (empty).
    struct LanguageTag
    {
        char value[16] = {};

        LanguageTag() = default;
        explicit LanguageTag(std::string_view tag) noexcept
        {
            if (tag.size() < sizeof(value))
                std::memcpy(value, tag.data(), tag.size());
        }

        bool             empty() const noexcept { return value[0] == '\0'; }
        const char*      c_str() const noexcept { return value; }
        std::string_view view()  const noexcept { return value; }
        bool operator==(const LanguageTag& other) const noexcept { return view() == other.view(); }
    };

    
	// Enum describing text writing direction.
	enum class TextDirection : uint8_t
	{
		Auto,		 // Auto, infer from the text.		
		LeftToRight,
		RightToLeft,
	};

	// Enum describing how text is wrapped to layout box.
	enum class TextWrap : uint8_t
	{
		NoWrap = 0,	// No text wrapping (CSS: nowrap).
		Wrap,		// Wrap text at word boundaries. If a single word is longer than the layout box, it will overflow (CSS: wrap).
		WordChar,	// Wrap text at word boundaries. If a single word is longer than the layout box, it will be wrapped at a character (CSS: word-char).
	};

	// Enum describing alignment of text relative to the layout box.
	// Values match skribidi's skb_align_t.
	enum class TextAlign : uint8_t
	{
		Start = 0,	// Horizontal: the language's start (left for LTR, right for RTL). Vertical: top.
		Center,		// Horizontal and vertical: center.
		End,		// Horizontal: the language's end (right for LTR, left for RTL). Vertical: bottom.
		Left,		// Horizontal: left. Vertical: n/a (behaves like top).
		Right,		// Horizontal: right. Vertical: n/a (behaves like bottom).
		Top,		// Horizontal: n/a (behaves like left). Vertical: top.
		Bottom,		// Horizontal: n/a (behaves like right). Vertical: bottom.
	};

	// Enum describing specific font baseline.
	enum class TextBaseline : uint8_t
	{
		Alphabetic,	  // Alphabetic, or roman baseline.		
		Ideographic,  // Ideographic baseline.		
		Central,	  // Ideographic central baseline		
		Hanging,	  // Hanging baseline.		
		Mathematical, // Mathematic baseline.		
		Middle,		  // Baseline positioned at the middle of x-height.		
		TextTop,	  // Text top baseline.		
		TextBottom,	  // Text bottom baseline.
		Max,
	};

	// Enum describing how the decoration should be drawn.
	enum class DecorationStyle : uint8_t
	{
		Solid = 0,	// Solid line.		
		Double,		// Double line.		
		Dotted,		// Dotted line.		
		Dashed,		// Dashed line.		
		Wavy,		// Wave line.
	};

	// Enum describing some generic font families.
	enum class FontFamily : uint8_t
	{
		Default,	// Default family, which is used if no family is specified.		
		Emoji,		// Font family automatically assigned for emojis		
		SansSerif,	// Generic sans-serif family.		
		Serif,		// Generic serif family		
		Monospace,	// Generic monospace family.		
		Math,		// Generic math family.
	};

	// Enum describing for weight.
	enum class FontWeight : uint8_t
	{
		Normal,		// Normal (400), (Default, when zero initialized)		
		Thin,		// Thin (100)		
		ExtraLight,	// Extra light (200)		
		UltraLight,	// Ultra light (200)		
		Light,		// Light (300)		
		Regular,	// Regular (400)		
		Medium,		// Medium (500)		
		DemiBold,	// Demibold (600)		
		SemiBold,	// Semibold (600)		
		Bold,		// Bold (700)		
		ExtraBold,	// Extra bold (800)		
		UltraBold,	// Ultra bold (800)		
		Black,		// Black (900)
		Heavy,		// Heavy (900)		
		ExtraBlack,	// Extra black (950)		
		UltraBlack,	// Ultra black (950)
	};

	// Enum describing font style.
	enum class FontStyle : uint8_t
	{
		Normal = 0, // Normal font		
		Italic,		// Italic font		
		Oblique,	// Oblique font
	};

	// Enum describing font stretch.
	enum class FontStretch : uint8_t
	{
		Normal = 0,		// Normal (1.0), (Default, when zero initialized)		
		UltraCondensed,	// Ultra condensed (0.5)		
		ExtraCondensed,	// Extra condensed (0.625)		
		Condensed,		// Condensed (0.75)		
		SemiCondensed,	// Semi condensed (0.875)		
		SemiExpanded,	// Semi expanded (1.125)		
		Expanded,		// Expanded (1.25)		
		ExtraExpanded,	// Extra expanded (1.5)		
		UltraExpanded,	// Ultra expanded (2.0)
	};

	// Enum describing how line height is calculated
	enum class LineHeight : uint8_t
	{
		Normal = 0,		  // Line height comes from font metrics. No scaling applied.
		MetricsRelative,  // Line height is multiple of line height specified in font metrics.
		FontSizeRelative, // Line height is multiple of font size.
		Absolute,		  // Line height is the specified value.
	};

	// Enum describing which font metrics define the line bounds for vertical alignment.
	enum class VerticalTrim : uint8_t
	{
		Default = 0,    // Ascender to descender — preserves room for accents/diacritics.
		CapToBaseline,  // Cap height to baseline — tight, visually centered for UI widgets.
	};

	enum class WordWrap : uint8_t
	{
		None,     // Single line, no wrapping
		Normal,   // Break at word boundaries
		BreakWord // Break within words if necessary
	};

    enum class TextOverflow : uint8_t
    {
        Clip,     // Simply clip the text
        Ellipsis  // Show "..." at the end
    };

	class TextLayoutStyle
	{
	public:
		TextDirection	 textDirection		 = TextDirection::Auto;
		TextWrap		 textWrapping		 = TextWrap::NoWrap;
		TextAlign		 horizontalAlignment = TextAlign::Start;
		// Vertical position of the text (+ icon) block in its box; only Start / Center / End
		// (and their aliases) apply. CSS: `-interstice-vertical-text-align`.
		TextAlign		 verticalAlignment   = TextAlign::Start;
		TextBaseline	 baseline			 = TextBaseline::Alphabetic;
		TextOverflow     textOverflow        = TextOverflow::Clip;
		WordWrap		 wrapMode			 = WordWrap::Normal;
		VerticalTrim	 verticalTrim		 = VerticalTrim::Default;

		uint64_t GetHashCode() const noexcept;

		inline bool operator==(const TextLayoutStyle& other) const noexcept
		{
            return textDirection       == other.textDirection &&
				   textWrapping        == other.textWrapping &&
				   horizontalAlignment == other.horizontalAlignment &&
				   verticalAlignment   == other.verticalAlignment &&
				   baseline            == other.baseline &&
				   verticalTrim        == other.verticalTrim &&
				   textOverflow        == other.textOverflow &&
				   wrapMode            == other.wrapMode;
		}
	};

	class TextDecoration
	{
	public:
		GUI::color4b	color = GUI::color4b{ 0,0,0,0 };
		uint16_t		thickness = 0;
		uint16_t		offset = 0;
		DecorationStyle	style = DecorationStyle::Solid;
		
		uint64_t GetHashCode() const noexcept;

		inline bool operator==(const TextDecoration& other) const noexcept
		{
			if (thickness > 0)
			{
				if (other.thickness == 0) return false;
			} else
			{
				return other.thickness == 0; // if both are inactive other values are irrelevant
			}
			return style     == other.style &&
				   thickness == other.thickness &&
				   offset    == other.offset &&
				   color     == other.color;
		}
	};

	class TextStyle
	{
	public:
		GUI::color4b		  color          = GUI::color4b{ 0,0,0,0 };
		float				  fontSize       = 0;
		// A fixed pitch, so a fallback font with tall metrics on a line can't stretch it.
		float				  lineHeight     = 1.25f;
		float				  letterSpacing  = 0.0f;
		float				  wordSpacing    = 0.0f;
		FontFamily			  family         = FontFamily::Default;
		FontWeight			  weight         = FontWeight::Normal;
		FontStyle			  style          = FontStyle::Normal;
		FontStretch			  stretch        = FontStretch::Normal;
		LineHeight			  lineHeightType = LineHeight::FontSizeRelative;
		FontID                fontId         = FontID::Invalid; // overrides family when set
		LanguageTag           language;                         // picks regional letterforms (e.g. CJK); empty = the text system's default
							  
		TextDecoration		  throughLine;
		TextDecoration		  bottomLine;
		TextDecoration		  underLine;
		TextDecoration		  overLine;

		uint64_t GetHashCode() const noexcept;

		bool operator==(const TextStyle& other) const noexcept
		{
			return family         == other.family &&
				   color          == other.color &&
				   fontSize       == other.fontSize &&
				   weight         == other.weight &&
				   style          == other.style &&
				   stretch        == other.stretch &&
				   lineHeightType == other.lineHeightType &&
				   lineHeight     == other.lineHeight &&
				   letterSpacing  == other.letterSpacing &&
				   wordSpacing    == other.wordSpacing &&
				   fontId         == other.fontId &&
				   language       == other.language &&
				   throughLine    == other.throughLine &&
				   bottomLine     == other.bottomLine &&
				   underLine      == other.underLine &&
				   overLine       == other.overLine;
		}
		
		inline float CalcLineHeight() const
		{
			switch (lineHeightType)
			{
				case LineHeight::Absolute:         return lineHeight;
				case LineHeight::FontSizeRelative: return fontSize * lineHeight;
				case LineHeight::MetricsRelative:
				case LineHeight::Normal:
				default:                           return fontSize;
			}
		}
	};

}//namespace GUI
