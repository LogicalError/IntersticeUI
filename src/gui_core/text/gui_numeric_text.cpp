#include <gui_core/text/gui_numeric_text.h>

#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace interstice
{
	namespace
	{
	// Normalizes a run of leading zeros in `s` starting at `start`.
	// "007" -> "7"  (followed by another digit)
	// "000" -> "0", "00.5" -> "0.5"  (followed by non-digit or end: keep one zero)
	void strip_leading_zeros(std::string& s, size_t start)
	{
		size_t i = start;
		while (i < s.size() && s[i] == '0') i++;
		const size_t num_zeros = i - start;
		if (num_zeros <= 1)
			return;

		const bool followed_by_digit = (i < s.size()) && (s[i] >= '1' && s[i] <= '9');
		if (followed_by_digit)
			s.erase(start, num_zeros);
		else
			s.erase(start, num_zeros - 1);
	}

	bool filter_float_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		// [sign] mantissa [e|E [sign] digits]. Partial forms ("1e", "1e-") stay, so
		// exponent notation can be typed or pasted; the value parses once complete.
		bool seen_dot        = false;
		bool seen_digit      = false;
		size_t exponent_at   = std::string::npos;  // index of 'e' in `filtered`

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				seen_digit = true;
				continue;
			}

			if (c == '+' || c == '-')
			{
				if (filtered.empty() || (exponent_at != std::string::npos && filtered.size() == exponent_at + 1))
					filtered.push_back(c);
				continue;
			}

			if (c == '.')
			{
				if (!seen_dot && exponent_at == std::string::npos)
				{
					filtered.push_back(c);
					seen_dot = true;
				}
				continue;
			}

			if ((c == 'e' || c == 'E') && seen_digit && exponent_at == std::string::npos)
			{
				exponent_at = filtered.size();
				filtered.push_back(c);
				continue;
			}
		}

		{
			const size_t mantissa_start = (!filtered.empty() && (filtered[0] == '+' || filtered[0] == '-')) ? 1 : 0;
			std::string exponent;
			if (exponent_at != std::string::npos)
			{
				exponent = filtered.substr(exponent_at);
				filtered.resize(exponent_at);
			}
			strip_leading_zeros(filtered, mantissa_start);
			filtered += exponent;
		}

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	bool filter_int_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				continue;
			}

			if ((c == '+' || c == '-') && filtered.empty())
			{
				filtered.push_back(c);
				continue;
			}
		}

		const size_t digits_start = (!filtered.empty() && (filtered[0] == '+' || filtered[0] == '-')) ? 1 : 0;
		strip_leading_zeros(filtered, digits_start);

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	bool filter_uint_text(std::string& text)
	{
		std::string filtered;
		filtered.reserve(text.size());

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				filtered.push_back(c);
				continue;
			}
		}

		strip_leading_zeros(filtered, 0);

		if (filtered == text)
			return false;

		text = std::move(filtered);
		return true;
	}

	std::string format_to_text(double value, bool clamp)
	{
		if (std::isinf(value))
		{
			if (value < 0)
				return "-Infinity";
			return "Infinity";					
		}
		if (std::isnan(value))
		{
			return "NaN";
		}
		if (clamp)
		{
			char buffer[128] = {};
			int written = snprintf(buffer, sizeof(buffer), "%.9g", value);
			if (written <= 0) return "0";
			return std::string(buffer, static_cast<size_t>(written));
		} else
		{
			char buffer[128] = {};
			int written = snprintf(buffer, sizeof(buffer), "%g", value);
			if (written <= 0) return "0";
			return std::string(buffer, static_cast<size_t>(written));
		}
	}

	std::string format_to_text(float value, bool clamp) { return format_to_text(static_cast<double>(value), clamp); }

	std::string format_to_text(int64_t value)
	{
		char buffer[64] = {};
		int written = snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
		if (written <= 0) return "0";
		return std::string(buffer, static_cast<size_t>(written));
	}
	std::string format_to_text(int32_t value) { return format_to_text(static_cast<int64_t>(value)); }
	std::string format_to_text(int16_t value) { return format_to_text(static_cast<int64_t>(value)); }
	std::string format_to_text(int8_t  value) { return format_to_text(static_cast<int64_t>(value)); }

	std::string format_to_text(uint64_t value)
	{
		char buffer[64] = {};
		int written = snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
		if (written <= 0) return "0";
		return std::string(buffer, static_cast<size_t>(written));
	}
	std::string format_to_text(uint32_t value) { return format_to_text(static_cast<uint64_t>(value)); }
	std::string format_to_text(uint16_t value) { return format_to_text(static_cast<uint64_t>(value)); }
	std::string format_to_text(uint8_t  value) { return format_to_text(static_cast<uint64_t>(value)); }

	std::string format_to_text(NumericType type, const NumericValue& outValue)
	{
		switch (type)
		{
			case NumericType::TypeFloat:  return format_to_text(outValue.value_float, true);
			case NumericType::TypeDouble: return format_to_text(outValue.value_double, true);

			case NumericType::TypeUInt64: return format_to_text(outValue.value_uint64);
			case NumericType::TypeUInt32: return format_to_text(outValue.value_uint32);
			case NumericType::TypeUInt16: return format_to_text(outValue.value_uint16);
			case NumericType::TypeUInt8:  return format_to_text(outValue.value_uint8);

			case NumericType::TypeInt64: return format_to_text(outValue.value_int64);
			case NumericType::TypeInt32: return format_to_text(outValue.value_int32);
			case NumericType::TypeInt16: return format_to_text(outValue.value_int16);
			case NumericType::TypeInt8:  return format_to_text(outValue.value_int8);

			default:
				return {};
		}
	}

	bool try_parse_text(std::string_view text, double& outValue)
	{
		if (text.empty()) return false;
		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		double value = strtod(tmp.c_str(), &end);
		if (end == tmp.c_str() || *end != '\0') return false;
		if (errno == ERANGE || !std::isfinite(value)) return false;
		outValue = value;
		return true;
	}

	bool try_parse_text(std::string_view text, float& outValue)
	{
		if (text.empty()) return false;
		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		float value = strtof(tmp.c_str(), &end);
		if (end == tmp.c_str() || *end != '\0') return false;
		if (errno == ERANGE || !std::isfinite(value)) return false;

		outValue = value;
		return true;
	}

	bool try_parse_text(std::string_view text, int64_t& outValue)
	{
		if (text.empty())
			return false;

		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		long long value = strtoll(tmp.c_str(), &end, 10);

		if (end == tmp.c_str() || *end != '\0')
			return false;
		if (errno == ERANGE)
			return false;

		outValue = static_cast<int64_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint64_t& outValue)
	{
		if (text.empty())
			return false;

		// strtoull does not handle leading '-' the way we want for unsigned parsing,
		// so reject negative values explicitly.
		if (!text.empty() && text.front() == '-')
			return false;

		std::string tmp(text);
		char* end = nullptr;
		errno = 0;
		unsigned long long value = strtoull(tmp.c_str(), &end, 10);

		if (end == tmp.c_str() || *end != '\0')
			return false;
		if (errno == ERANGE)
			return false;

		outValue = static_cast<uint64_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int32_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT32_MIN || value > INT32_MAX) return false;
		outValue = static_cast<int32_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint32_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT32_MAX) return false;
		outValue = static_cast<uint32_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int16_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT16_MIN || value > INT16_MAX) return false;
		outValue = static_cast<int16_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint16_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT16_MAX) return false;
		outValue = static_cast<uint16_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, int8_t& outValue)
	{
		int64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value < INT8_MIN || value > INT8_MAX) return false;
		outValue = static_cast<int8_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, uint8_t& outValue)
	{
		uint64_t value;
		if (!try_parse_text(text, value)) return false;
		if (value > UINT8_MAX) return false;
		outValue = static_cast<uint8_t>(value);
		return true;
	}

	bool try_parse_text(std::string_view text, NumericType type, NumericValue& outValue)
	{
		switch (type)
		{
			case NumericType::TypeFloat:  return try_parse_text(text, outValue.value_float);
			case NumericType::TypeDouble: return try_parse_text(text, outValue.value_double);

			case NumericType::TypeUInt64: return try_parse_text(text, outValue.value_uint64);
			case NumericType::TypeUInt32: return try_parse_text(text, outValue.value_uint32);
			case NumericType::TypeUInt16: return try_parse_text(text, outValue.value_uint16);
			case NumericType::TypeUInt8:  return try_parse_text(text, outValue.value_uint8);

			case NumericType::TypeInt64: return try_parse_text(text, outValue.value_int64);
			case NumericType::TypeInt32: return try_parse_text(text, outValue.value_int32);
			case NumericType::TypeInt16: return try_parse_text(text, outValue.value_int16);
			case NumericType::TypeInt8:  return try_parse_text(text, outValue.value_int8);

			default:
				outValue = {};
				return false;
		}
	}

	using FilterFunc = bool(std::string& text);
	FilterFunc* get_text_filter_func(NumericType type)
	{
		if ((int(type) & int(NumericType::TypeUnsignedBit)) == int(NumericType::TypeUnsignedBit))
			return &filter_uint_text;
		if ((int(type) & int(NumericType::TypeSignedBit)) == int(NumericType::TypeSignedBit))
			return &filter_int_text;
		if ((int(type) & int(NumericType::TypeFloatBit)) == int(NumericType::TypeFloatBit))
			return &filter_float_text;
		assert(false && "Unknown numeric type");
		return nullptr;
	}
	}

	std::string FormatNumber(NumericType type, const NumericValue& value) { return format_to_text(type, value); }

	bool ParseNumber(std::string_view text, NumericType type, NumericValue& outValue) { return try_parse_text(text, type, outValue); }

	bool FilterNumberText(NumericType type, std::string& text)
	{
		FilterFunc* filter = get_text_filter_func(type);
		return filter != nullptr && filter(text);
	}

	bool IsNumberCharacter(NumericType type, uint32_t codepoint)
	{
		if (codepoint >= '0' && codepoint <= '9')
			return true;
		if ((int(type) & int(NumericType::TypeUnsignedBit)) == int(NumericType::TypeUnsignedBit))
			return false;
		if (codepoint == '+' || codepoint == '-')
			return true;
		return (int(type) & int(NumericType::TypeFloatBit)) == int(NumericType::TypeFloatBit)
			&& (codepoint == '.' || codepoint == 'e' || codepoint == 'E');
	}
}
