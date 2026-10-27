#include <gui_core/diagnostics/gui_logging.h>
#include <gui_core/text/skribidi/internal/gui_default_fonts_skribidi.h>

#include <skribidi/skb_common.h>
#include <skribidi/skb_font_collection.h>
#include <skribidi/skb_rasterizer.h>
#include <skribidi/skb_layout.h>
#include <skribidi/skb_attributes.h>
#include <skribidi/skb_editor.h>
#include <skribidi/skb_rich_layout.h>
#include <skribidi/skb_image_atlas.h>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace tachyon
{

static constexpr Diagnostics::LogCategory k_log_text = Diagnostics::MakeLogCategory("text");

static std::string font_path(std::string_view directory, const char* filename)
{
	std::string path(directory);
	if (!path.empty() && path.back() != '/' && path.back() != '\\')
		path += '/';
	return path + filename;
}

// Maps ISO-15924 uint32_t tag -> list of font filenames (in the fallback directory).
// Built once on first use. Used by the font fallback callback.
static const std::unordered_map<uint32_t, std::vector<const char*>>& script_font_map()
{
	static const std::unordered_map<uint32_t, std::vector<const char*>> map = {
		// Common / inherited scripts - triggered for math/symbol chars not in NotoSansSymbols
		{ SKB_TAG_STR("Zyyy"), { "NotoSansMath-Regular.otf", "NotoMusic-Regular.otf" } },

		// Scripts in use today, sorted by ISO-15924 tag. Historic, epigraphic and
		// constructed scripts are left out; their text renders as missing glyphs.
		{ SKB_TAG_STR("Adlm"), { "NotoSansAdlam-Regular.otf", "NotoSansAdlamUnjoined-Regular.otf" } },
		{ SKB_TAG_STR("Arab"), { "NotoSansArabicUI-Regular.otf", "NotoSansArabic-Regular.otf", "NotoNaskhArabic-Regular.otf", "NotoNaskhArabicUI-Regular.otf", "NotoNastaliqUrdu-Regular.otf" } },
		{ SKB_TAG_STR("Armn"), { "NotoSansArmenian-Regular.otf" } },
		{ SKB_TAG_STR("Bali"), { "NotoSansBalinese-Regular.otf" } },
		{ SKB_TAG_STR("Bamu"), { "NotoSansBamum-Regular.otf" } },
		{ SKB_TAG_STR("Bass"), { "NotoSansBassaVah-Regular.otf" } },
		{ SKB_TAG_STR("Batk"), { "NotoSansBatak-Regular.otf" } },
		{ SKB_TAG_STR("Beng"), { "NotoSansBengaliUI-Regular.otf", "NotoSansBengali-Regular.otf" } },
		{ SKB_TAG_STR("Bopo"), { "NotoSansCJKsc-Regular.otf" } },
		{ SKB_TAG_STR("Bugi"), { "NotoSansBuginese-Regular.otf" } },
		{ SKB_TAG_STR("Buhd"), { "NotoSansBuhid-Regular.otf" } },
		{ SKB_TAG_STR("Cakm"), { "NotoSansChakma-Regular.otf" } },
		{ SKB_TAG_STR("Cans"), { "NotoSansCanadianAboriginal-Regular.otf" } },
		{ SKB_TAG_STR("Cham"), { "NotoSansCham-Regular.otf" } },
		{ SKB_TAG_STR("Cher"), { "NotoSansCherokee-Regular.otf" } },
		{ SKB_TAG_STR("Copt"), { "NotoSansCoptic-Regular.otf" } },
		{ SKB_TAG_STR("Deva"), { "NotoSansDevanagariUI-Regular.otf", "NotoSansDevanagari-Regular.otf" } },
		{ SKB_TAG_STR("Ethi"), { "NotoSansEthiopic-Regular.otf" } },
		{ SKB_TAG_STR("Geor"), { "NotoSansGeorgian-Regular.otf" } },
		{ SKB_TAG_STR("Gong"), { "NotoSansGunjalaGondi-Regular.otf" } },
		{ SKB_TAG_STR("Gonm"), { "NotoSansMasaramGondi-Regular.otf" } },
		{ SKB_TAG_STR("Gujr"), { "NotoSansGujaratiUI-Regular.otf", "NotoSansGujarati-Regular.otf" } },
		{ SKB_TAG_STR("Guru"), { "NotoSansGurmukhiUI-Regular.otf", "NotoSansGurmukhi-Regular.otf" } },
		{ SKB_TAG_STR("Hang"), { "NotoSansCJKsc-Regular.otf" } },
		// Full (not subset) Noto Sans CJK: one font with every region's letterforms,
		// picked per run by the text's language via OpenType locl.
		{ SKB_TAG_STR("Hani"), { "NotoSansCJKsc-Regular.otf" } },
		{ SKB_TAG_STR("Hano"), { "NotoSansHanunoo-Regular.otf" } },
		{ SKB_TAG_STR("Hebr"), { "NotoSansHebrew-Regular.otf", "NotoRashiHebrew-Regular.otf" } },
		{ SKB_TAG_STR("Hira"), { "NotoSansCJKsc-Regular.otf" } },
		{ SKB_TAG_STR("Hmnp"), { "NotoSerifNPHmong-Regular.otf" } },
		{ SKB_TAG_STR("Hmng"), { "NotoSansPahawhHmong-Regular.otf" } },
		{ SKB_TAG_STR("Java"), { "NotoSansJavanese-Regular.otf" } },
		{ SKB_TAG_STR("Kali"), { "NotoSansKayahLi-Regular.otf" } },
		{ SKB_TAG_STR("Kana"), { "NotoSansCJKsc-Regular.otf" } },
		{ SKB_TAG_STR("Khmr"), { "NotoSansKhmerUI-Regular.otf", "NotoSansKhmer-Regular.otf" } },
		{ SKB_TAG_STR("Knda"), { "NotoSansKannadaUI-Regular.otf", "NotoSansKannada-Regular.otf" } },
		{ SKB_TAG_STR("Lana"), { "NotoSansTaiTham-Regular.otf" } },
		{ SKB_TAG_STR("Laoo"), { "NotoSansLaoUI-Regular.otf", "NotoSansLao-Regular.otf", "NotoSansLaoLooped-Regular.otf" } },
		{ SKB_TAG_STR("Lepc"), { "NotoSansLepcha-Regular.otf" } },
		{ SKB_TAG_STR("Limb"), { "NotoSansLimbu-Regular.otf" } },
		{ SKB_TAG_STR("Lisu"), { "NotoSansLisu-Regular.otf" } },
		{ SKB_TAG_STR("Mand"), { "NotoSansMandaic-Regular.otf" } },
		{ SKB_TAG_STR("Medf"), { "NotoSansMedefaidrin-Regular.otf" } },
		{ SKB_TAG_STR("Mend"), { "NotoSansMendeKikakui-Regular.otf" } },
		{ SKB_TAG_STR("Mlym"), { "NotoSansMalayalamUI-Regular.otf", "NotoSansMalayalam-Regular.otf" } },
		{ SKB_TAG_STR("Mong"), { "NotoSansMongolian-Regular.otf" } },
		{ SKB_TAG_STR("Mroo"), { "NotoSansMro-Regular.otf" } },
		{ SKB_TAG_STR("Mtei"), { "NotoSansMeeteiMayek-Regular.otf" } },
		{ SKB_TAG_STR("Mymr"), { "NotoSansMyanmarUI-Regular.otf", "NotoSansMyanmar-Regular.otf" } },
		{ SKB_TAG_STR("Nagm"), { "NotoSansNagMundari-Regular.otf" } },
		{ SKB_TAG_STR("Newa"), { "NotoSansNewa-Regular.otf" } },
		{ SKB_TAG_STR("Nkoo"), { "NotoSansNKo-Regular.otf", "NotoSansNKoUnjoined-Regular.otf" } },
		{ SKB_TAG_STR("Olck"), { "NotoSansOlChiki-Regular.otf" } },
		{ SKB_TAG_STR("Orya"), { "NotoSansOriyaUI-Regular.otf", "NotoSansOriya-Regular.otf" } },
		{ SKB_TAG_STR("Osge"), { "NotoSansOsage-Regular.otf" } },
		{ SKB_TAG_STR("Plrd"), { "NotoSansMiao-Regular.otf" } },
		{ SKB_TAG_STR("Rohg"), { "NotoSansHanifiRohingya-Regular.otf" } },
		{ SKB_TAG_STR("Samr"), { "NotoSansSamaritan-Regular.otf" } },
		{ SKB_TAG_STR("Saur"), { "NotoSansSaurashtra-Regular.otf" } },
		{ SKB_TAG_STR("Sgnw"), { "NotoSansSignWriting-Regular.otf" } },
		{ SKB_TAG_STR("Sinh"), { "NotoSansSinhalaUI-Regular.otf", "NotoSansSinhala-Regular.otf" } },
		{ SKB_TAG_STR("Sora"), { "NotoSansSoraSompeng-Regular.otf" } },
		{ SKB_TAG_STR("Sund"), { "NotoSansSundanese-Regular.otf" } },
		{ SKB_TAG_STR("Sylo"), { "NotoSansSylotiNagri-Regular.otf" } },
		{ SKB_TAG_STR("Syrc"), { "NotoSansSyriac-Regular.otf", "NotoSansSyriacEastern-Regular.otf", "NotoSansSyriacWestern-Regular.otf" } },
		{ SKB_TAG_STR("Tagb"), { "NotoSansTagbanwa-Regular.otf" } },
		{ SKB_TAG_STR("Tale"), { "NotoSansTaiLe-Regular.otf" } },
		{ SKB_TAG_STR("Talu"), { "NotoSansNewTaiLue-Regular.otf" } },
		{ SKB_TAG_STR("Taml"), { "NotoSansTamilUI-Regular.otf", "NotoSansTamil-Regular.otf", "NotoSansTamilSupplement-Regular.otf" } },
		{ SKB_TAG_STR("Tavt"), { "NotoSansTaiViet-Regular.otf" } },
		{ SKB_TAG_STR("Telu"), { "NotoSansTeluguUI-Regular.otf", "NotoSansTelugu-Regular.otf" } },
		{ SKB_TAG_STR("Tfng"), { "NotoSansTifinagh-Regular.otf", "NotoSansTifinaghAdrar-Regular.otf", "NotoSansTifinaghAgrawImazighen-Regular.otf", "NotoSansTifinaghAhaggar-Regular.otf", "NotoSansTifinaghAir-Regular.otf", "NotoSansTifinaghAPT-Regular.otf", "NotoSansTifinaghAzawagh-Regular.otf", "NotoSansTifinaghGhat-Regular.otf", "NotoSansTifinaghHawad-Regular.otf", "NotoSansTifinaghRhissaIxa-Regular.otf", "NotoSansTifinaghSIL-Regular.otf", "NotoSansTifinaghTawellemmet-Regular.otf" } },
		{ SKB_TAG_STR("Thaa"), { "NotoSansThaana-Regular.otf" } },
		{ SKB_TAG_STR("Thai"), { "NotoSansThaiUI-Regular.otf", "NotoSansThai-Regular.otf", "NotoSansThaiLooped-Regular.otf", "NotoLoopedThai-Regular.otf" } },
		{ SKB_TAG_STR("Tibt"), { "NotoSerifTibetan-Regular.otf" } },
		{ SKB_TAG_STR("Tglg"), { "NotoSansTagalog-Regular.otf" } },
		{ SKB_TAG_STR("Tnsa"), { "NotoSansTangsa-Regular.otf" } },
		{ SKB_TAG_STR("Toto"), { "NotoSerifToto-Regular.otf" } },
		{ SKB_TAG_STR("Vaii"), { "NotoSansVai-Regular.otf" } },
		{ SKB_TAG_STR("Wara"), { "NotoSansWarangCiti-Regular.otf" } },
		{ SKB_TAG_STR("Wcho"), { "NotoSansWancho-Regular.otf" } },
		{ SKB_TAG_STR("Yiii"), { "NotoSansYi-Regular.otf" } },
	};
	return map;
}

// Synthetic bold: re-register a face with embolden params so the font matcher
// has a 700-weight candidate for `font-weight: bold`.
static const skb_font_create_params_t k_synthetic_bold = { SKB_DEFAULT_EMBOLDEN_X, SKB_DEFAULT_EMBOLDEN_Y, 0.f };

static bool add_font(skb_font_collection_t* font_collection, const std::string& path, uint8_t family,
	const skb_font_create_params_t* params = nullptr)
{
	if (skb_font_collection_add_font(font_collection, path.c_str(), family, params))
		return true;
	TACHYON_LOG_WARNING(k_log_text, "could not load font {}", path);
	return false;
}

static bool add_face(skb_font_collection_t* font_collection, const FontFaceSource& face, uint8_t family,
	const skb_font_create_params_t* params = nullptr)
{
	if (face.data.empty())
		return add_font(font_collection, std::string(face.file), family, params);
	// Named by its address, so the same data is the same font.
	const std::string name = "memory:" + std::to_string(reinterpret_cast<uintptr_t>(face.data.data()))
	                       + ":" + std::to_string(face.data.size());
	if (skb_font_collection_add_font_from_data(font_collection, name.c_str(), face.data.data(), face.data.size(),
	                                           nullptr, nullptr, family, params))
		return true;
	TACHYON_LOG_WARNING(k_log_text, "could not load a font from memory ({} bytes)", face.data.size());
	return false;
}

// An optional fallback font: a missing one is reported once and skipped.
static bool add_optional_font(skb_font_collection_t* font_collection, const std::string& path, uint8_t family,
	FontFallbackState& fonts, const skb_font_create_params_t* params = nullptr)
{
	if (fonts.missing.contains(path))
		return false;
	if (skb_font_collection_add_font(font_collection, path.c_str(), family, params))
		return true;
	fonts.missing.insert(path);
	TACHYON_LOG_WARNING(k_log_text, "optional fallback font {} is missing; skipped", path);
	return false;
}

static bool on_font_fallback(skb_font_collection_t* font_collection, const char* /*lang*/,
	uint8_t script, uint8_t font_family, void* context)
{
	// Keyed per family: each family gets its own copy of the script's fonts.
	auto* fonts = static_cast<FontFallbackState*>(context);

	const uint32_t tag = skb_script_to_iso15924_tag(script);
	if (!fonts->attempts.insert((uint64_t(font_family) << 32) | tag).second)
		return false;

	auto it = script_font_map().find(tag);
	if (it == script_font_map().end())
		return false;

	// Skip synthetic bold for emoji - color glyphs already carry weight.
	const bool add_bold = (font_family != SKB_FONT_FAMILY_EMOJI);

	bool any_loaded = false;
	for (const char* filename : it->second)
	{
		const std::string path = font_path(fonts->fallbackDirectory, filename);
		if (!add_optional_font(font_collection, path, font_family, *fonts))
			continue;
		any_loaded = true;
		if (add_bold)
			add_optional_font(font_collection, path, font_family, *fonts, &k_synthetic_bold);
	}
	return any_loaded;
}

// What backs every family for codepoints its faces lack: Inter, then the fallback
// set's Latin/Greek/Cyrillic and symbol fonts.
static void add_base_fonts(skb_font_collection_t* font_collection, uint8_t family, FontFallbackState& fonts)
{
	if (!fonts.defaultDirectory.empty())
	{
		const std::string path = font_path(fonts.defaultDirectory, "Inter/Inter-Regular.ttf");
		add_font(font_collection, path, family);
		add_font(font_collection, path, family, &k_synthetic_bold);
	}
	if (fonts.fallbackDirectory.empty())
		return;
	for (const char* filename : { "NotoSans-Regular.otf", "NotoSansSymbols-Regular.otf", "NotoSansSymbols2-Regular.otf" })
	{
		const std::string path = font_path(fonts.fallbackDirectory, filename);
		if (add_optional_font(font_collection, path, family, fonts))
			add_optional_font(font_collection, path, family, fonts, &k_synthetic_bold);
	}
}

bool skribidi_add_font_family(skb_font_collection_t* font_collection, uint8_t family, std::span<const FontFaceSource> faces,
                              FontFallbackState& fonts)
{
	// Insertion order is match priority: the family's own faces, then synthetic
	// bolds (a real bold face added earlier still wins), then the base fonts for
	// codepoints the faces lack.
	std::vector<const FontFaceSource*> loaded;
	for (const FontFaceSource& face : faces)
		if (add_face(font_collection, face, family))
			loaded.push_back(&face);
	if (loaded.empty())
		return false;

	for (const FontFaceSource* face : loaded)
		add_face(font_collection, *face, family, &k_synthetic_bold);
	add_base_fonts(font_collection, family, fonts);
	return true;
}

bool skribidi_load_fonts(skb_font_collection_t* font_collection, FontFallbackState& fonts)
{
	// The built-in families: Inter for text, JetBrains Mono for monospace.
	if (!fonts.defaultDirectory.empty())
	{
		auto paths = [&](std::initializer_list<const char*> files)
		{
			std::vector<std::string> result;
			for (const char* file : files)
				result.push_back(font_path(fonts.defaultDirectory, file));
			return result;
		};
		auto faces = [](const std::vector<std::string>& files)
		{
			std::vector<FontFaceSource> result;
			for (const std::string& file : files)
				result.push_back(FontFaceSource{ .file = file });
			return result;
		};
		const auto inter = paths({ "Inter/Inter-Regular.ttf", "Inter/Inter-Medium.ttf", "Inter/Inter-SemiBold.ttf", "Inter/Inter-Bold.ttf",
		                           "Inter/Inter-Italic.ttf", "Inter/Inter-BoldItalic.ttf" });
		const auto mono  = paths({ "JetBrainsMono/JetBrainsMono-Regular.ttf", "JetBrainsMono/JetBrainsMono-Bold.ttf" });
		for (uint8_t family : { uint8_t(SKB_FONT_FAMILY_DEFAULT), uint8_t(SKB_FONT_FAMILY_SANS_SERIF),
		                        uint8_t(SKB_FONT_FAMILY_SERIF), uint8_t(SKB_FONT_FAMILY_MATH) })
			if (!skribidi_add_font_family(font_collection, family, faces(inter), fonts)) return false;
		if (!skribidi_add_font_family(font_collection, SKB_FONT_FAMILY_MONOSPACE, faces(mono), fonts)) return false;
	}
	else
	{
		for (uint8_t family : { uint8_t(SKB_FONT_FAMILY_DEFAULT), uint8_t(SKB_FONT_FAMILY_SANS_SERIF),
		                        uint8_t(SKB_FONT_FAMILY_SERIF), uint8_t(SKB_FONT_FAMILY_MATH),
		                        uint8_t(SKB_FONT_FAMILY_MONOSPACE) })
			add_base_fonts(font_collection, family, fonts);
	}

	// Everything else comes from the optional fallback set, on demand.
	if (!fonts.fallbackDirectory.empty())
	{
		add_optional_font(font_collection, font_path(fonts.fallbackDirectory, "NotoColorEmoji-Regular.ttf"),
		                  SKB_FONT_FAMILY_EMOJI, fonts);
		skb_font_collection_set_on_font_fallback(font_collection, on_font_fallback, &fonts);
	}
	return true;
}

} // namespace tachyon
