#pragma once

#include <stdint.h>
#include <cstring>
#include <string_view>

namespace tachyon
{
    /// @addtogroup styling
    /// @{

    /// Handle for text registered with the text system (ITextSystem::RegisterText and friends).
    enum class TextID : uint32_t { Invalid = 0 };
	/// Handle for an icon registered with ITextSystem::RegisterIconFromSVG / RegisterIconFromSVGFile.
	enum class IconID : uint32_t { Invalid = 0 };

    /// Handle for a font family registered with ITextSystem::RegisterFontFamily().
    /// Derived from the family name, so styles can name a family before it is registered.
    /// Use FontID::Invalid for the default / family-based font selection.
    enum class FontID : uint32_t { Invalid = 0 };

    /// Case-insensitive, like CSS family names. Returns FontID::Invalid for an empty name.
    FontID FontIDFromName(std::string_view familyName);

    /// A BCP 47 language tag ("ja", "zh-Hant", "ko-KR") stored inline, so text
    /// styles stay plain values. Empty = no language of its own. A tag that
    /// doesn't fit is dropped (empty).
    struct LanguageTag
    {
        char value[16] = {}; ///< null-terminated tag

        LanguageTag() = default;
        /// Copies `tag`; a tag of 16 characters or more leaves it empty.
        explicit LanguageTag(std::string_view tag) noexcept
        {
            if (tag.size() < sizeof(value))
                std::memcpy(value, tag.data(), tag.size());
        }

        /// No tag set.
        bool             empty() const noexcept { return value[0] == '\0'; }
        /// The tag as a null-terminated string.
        const char*      c_str() const noexcept { return value; }
        /// The tag as a string view.
        std::string_view view()  const noexcept { return value; }
        bool operator==(const LanguageTag& other) const noexcept { return view() == other.view(); }
    };

    
	/// Writing direction of text (CSS: direction).
	enum class TextDirection : uint8_t
	{
		Auto,		 ///< inferred from the text
		LeftToRight,
		RightToLeft,
	};

	/// How text wraps in its box (CSS: text-wrap).
	enum class TextWrap : uint8_t
	{
		NoWrap = 0,	///< a single line (nowrap)
		Wrap,		///< at word boundaries; a word longer than the box overflows (wrap)
		WordChar,	///< at word boundaries; a word longer than the box breaks between characters (word-char)
	};

	/// Alignment of text in its box (CSS: text-align, -tachyon-vertical-text-align).
	enum class TextAlign : uint8_t
	{
		Start = 0,	///< Horizontal: the language's start (left for LTR, right for RTL). Vertical: top.
		Center,		///< Horizontal and vertical: center.
		End,		///< Horizontal: the language's end (right for LTR, left for RTL). Vertical: bottom.
		Left,		///< Horizontal: left. Vertical: n/a (behaves like top).
		Right,		///< Horizontal: right. Vertical: n/a (behaves like bottom).
		Top,		///< Horizontal: n/a (behaves like left). Vertical: top.
		Bottom,		///< Horizontal: n/a (behaves like right). Vertical: bottom.
	};

	/// Font baseline that text aligns on (TextLayoutStyle::baseline).
	enum class TextBaseline : uint8_t
	{
		Alphabetic,	  ///< alphabetic (roman) baseline
		Ideographic,  ///< ideographic baseline
		Central,	  ///< ideographic central baseline
		Hanging,	  ///< hanging baseline
		Mathematical, ///< mathematical baseline
		Middle,		  ///< the middle of the x-height
		TextTop,	  ///< top of the text
		TextBottom,	  ///< bottom of the text
		Max,          ///< count, not a baseline
	};

	/// Line style of a TextDecoration.
	enum class DecorationStyle : uint8_t
	{
		Solid = 0,	///< solid line
		Double,		///< double line
		Dotted,		///< dotted line
		Dashed,		///< dashed line
		Wavy,		///< wavy line
	};

	/// Generic font family, used when TextStyle::fontId names no registered family.
	enum class FontFamily : uint8_t
	{
		Default,	///< the text system's default family
		Emoji,		///< emoji; assigned to emoji automatically
		SansSerif,	///< sans-serif
		Serif,		///< serif
		Monospace,	///< monospace
		Math,		///< math
	};

	/// Font weight (CSS: font-weight); the CSS weight is in parentheses.
	enum class FontWeight : uint8_t
	{
		Normal,		///< Normal (400); the zero-initialized default
		Thin,		///< Thin (100)
		ExtraLight,	///< Extra light (200)
		UltraLight,	///< Ultra light (200)
		Light,		///< Light (300)
		Regular,	///< Regular (400)
		Medium,		///< Medium (500)
		DemiBold,	///< Demibold (600)
		SemiBold,	///< Semibold (600)
		Bold,		///< Bold (700)
		ExtraBold,	///< Extra bold (800)
		UltraBold,	///< Ultra bold (800)
		Black,		///< Black (900)
		Heavy,		///< Heavy (900)
		ExtraBlack,	///< Extra black (950)
		UltraBlack,	///< Ultra black (950)
	};

	/// Font slant (CSS: font-style).
	enum class FontStyle : uint8_t
	{
		Normal = 0, ///< upright
		Italic,		///< italic
		Oblique,	///< oblique
	};

	/// Font width (CSS: font-stretch); the width factor is in parentheses.
	enum class FontStretch : uint8_t
	{
		Normal = 0,		///< Normal (1.0); the zero-initialized default
		UltraCondensed,	///< Ultra condensed (0.5)
		ExtraCondensed,	///< Extra condensed (0.625)
		Condensed,		///< Condensed (0.75)
		SemiCondensed,	///< Semi condensed (0.875)
		SemiExpanded,	///< Semi expanded (1.125)
		Expanded,		///< Expanded (1.25)
		ExtraExpanded,	///< Extra expanded (1.5)
		UltraExpanded,	///< Ultra expanded (2.0)
	};

	/// How TextStyle::lineHeight is read.
	enum class LineHeight : uint8_t
	{
		Normal = 0,		  ///< the font's metrics; lineHeight is ignored
		MetricsRelative,  ///< a multiple of the font's metric line height
		FontSizeRelative, ///< a multiple of the font size
		Absolute,		  ///< the value itself
	};

	/// Font metrics that bound a line for vertical alignment (CSS: -tachyon-vertical-trim).
	enum class VerticalTrim : uint8_t
	{
		Default = 0,    ///< ascender to descender; leaves room for accents
		CapToBaseline,  ///< cap height to baseline; tight, visually centered for widgets
	};

	/// Word wrapping mode of TextLayoutStyle::wrapMode. The text system doesn't read it;
	/// wrapping is set by TextLayoutStyle::textWrapping (TextWrap).
	enum class WordWrap : uint8_t
	{
		None,     ///< single line
		Normal,   ///< break at word boundaries
		BreakWord ///< break within words when needed
	};

    /// What happens to text that doesn't fit its box (CSS: text-overflow).
    enum class TextOverflow : uint8_t
    {
        Clip,     ///< clip the text
        Ellipsis  ///< end with "..."
    };

	/// How a text block is laid out in its box; shared by all states of a Style.
	class TextLayoutStyle
	{
	public:
		TextDirection	 textDirection		 = TextDirection::Auto;     ///< CSS: direction
		TextWrap		 textWrapping		 = TextWrap::NoWrap;        ///< CSS: text-wrap
		TextAlign		 horizontalAlignment = TextAlign::Start;        ///< CSS: text-align
		/// Vertical position of the text (+ icon) block in its box; only Start / Center / End
		/// (and their aliases) apply. CSS: `-tachyon-vertical-text-align`.
		TextAlign		 verticalAlignment   = TextAlign::Start;
		/// CSS: -tachyon-text-baseline, dominant-baseline or vertical-align.
		TextBaseline	 baseline			 = TextBaseline::Alphabetic;
		TextOverflow     textOverflow        = TextOverflow::Clip;     ///< CSS: text-overflow
		WordWrap		 wrapMode			 = WordWrap::Normal;       ///< not read by the text system; see textWrapping
		VerticalTrim	 verticalTrim		 = VerticalTrim::Default;  ///< CSS: -tachyon-vertical-trim

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

	/// The decoration lines of a TextStyle; indexes StyleState::decorationColors.
	enum class DecorationLine : uint8_t
	{
		Under,   ///< TextStyle::underLine
		Bottom,  ///< TextStyle::bottomLine
		Over,    ///< TextStyle::overLine
		Through  ///< TextStyle::throughLine
	};

	/// One text decoration line (underline, overline, ...). Drawn only when thickness > 0.
	/// CSS: text-<under|over|through|bottom>line-<thickness|offset|style>; its color is
	/// StyleState::decorationColors.
	class TextDecoration
	{
	public:
		uint16_t		thickness = 0; ///< line thickness in CSS px; 0 = no line
		uint16_t		offset = 0;    ///< CSS px from the default position; grows down for under / bottom lines, up otherwise
		DecorationStyle	style = DecorationStyle::Solid; ///< line style

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
				   offset    == other.offset;
		}
	};

	/// What text is measured and laid out from: font, metrics and decoration lines. One per
	/// state of a Style; the colors it's drawn in are StyleState::textColor and
	/// StyleState::decorationColors, so a state that only recolors keeps the layout. Metrics
	/// are in CSS px until StyleManager::Register (or ToPixels) scales them to the UI.
	class TextStyle
	{
	public:
		float				  fontSize       = 0;              ///< CSS: font-size, in CSS px
		/// CSS: line-height, read per lineHeightType (by default a multiple of fontSize).
		/// A fixed pitch, so a fallback font with tall metrics can't stretch a line.
		float				  lineHeight     = 1.25f;
		float				  letterSpacing  = 0.0f;           ///< CSS px; stored and scaled, not applied by the text system
		float				  wordSpacing    = 0.0f;           ///< CSS px; stored and scaled, not applied by the text system
		FontFamily			  family         = FontFamily::Default; ///< generic family, used when fontId isn't registered
		FontWeight			  weight         = FontWeight::Normal;
		FontStyle			  style          = FontStyle::Normal;
		FontStretch			  stretch        = FontStretch::Normal;
		LineHeight			  lineHeightType = LineHeight::FontSizeRelative; ///< how lineHeight is interpreted
		FontID                fontId         = FontID::Invalid; ///< overrides family when set
		/// Picks regional letterforms (e.g. CJK); empty = the text system's default.
		LanguageTag           language;
							  
		TextDecoration		  throughLine; ///< through the text
		TextDecoration		  bottomLine;  ///< under the descender
		TextDecoration		  underLine;   ///< under the alphabetic baseline
		TextDecoration		  overLine;    ///< over the ascender

		uint64_t GetHashCode() const noexcept;

		bool operator==(const TextStyle& other) const noexcept
		{
			return family         == other.family &&
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
		
		/// Line pitch in the style's units. Normal / MetricsRelative need font metrics,
		/// so they return fontSize.
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

    /// @}
}//namespace tachyon
