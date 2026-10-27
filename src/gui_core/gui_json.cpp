#include <gui_core/gui_json.h>
#include <gui_core/gui_from_chars.h>

#include <charconv>
#include <cmath>
#include <cstdio>

namespace tachyon
{
    namespace
    {
        const std::string       kEmptyString;
        const JsonValue::Array  kEmptyArray;
        const JsonValue::Object kEmptyObject;

        constexpr int kMaxDepth = 64;

        class Parser
        {
        public:
            explicit Parser(std::string_view text) : m_text(text) {}

            Result<JsonValue> ParseDocument()
            {
                Result<JsonValue> value = ParseValue(0);
                if (!value) return value;
                SkipSpace();
                if (m_pos != m_text.size()) return Fail("text after the value");
                return value;
            }

        private:
            Error Fail(const char* what) const
            {
                return Error{ ErrorCode::ParseFailed, std::string("JSON: ") + what + " at offset " + std::to_string(m_pos) };
            }

            void SkipSpace()
            {
                while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' || m_text[m_pos] == '\n' || m_text[m_pos] == '\r'))
                    ++m_pos;
            }

            bool Literal(std::string_view word)
            {
                if (m_text.substr(m_pos, word.size()) != word) return false;
                m_pos += word.size();
                return true;
            }

            Result<JsonValue> ParseValue(int depth)
            {
                if (depth > kMaxDepth) return Fail("too deeply nested");
                SkipSpace();
                if (m_pos >= m_text.size()) return Fail("unexpected end");
                const char c = m_text[m_pos];
                if (c == '{') return ParseObject(depth);
                if (c == '[') return ParseArray(depth);
                if (c == '"')
                {
                    Result<std::string> s = ParseString();
                    if (!s) return s.GetError();
                    return JsonValue(std::move(*s));
                }
                if (Literal("true"))  return JsonValue(true);
                if (Literal("false")) return JsonValue(false);
                if (Literal("null"))  return JsonValue(nullptr);
                return ParseNumber();
            }

            Result<JsonValue> ParseNumber()
            {
                const size_t start = m_pos;
                if (m_pos < m_text.size() && m_text[m_pos] == '-') ++m_pos;
                while (m_pos < m_text.size() && std::string_view("0123456789.eE+-").find(m_text[m_pos]) != std::string_view::npos)
                    ++m_pos;
                double value = 0.0;
                const auto [end, ec] = FromChars(m_text.data() + start, m_text.data() + m_pos, value);
                if (ec != std::errc{} || end != m_text.data() + m_pos || m_pos == start)
                {
                    m_pos = start;
                    return Fail("not a value");
                }
                return JsonValue(value);
            }

            static void AppendUtf8(std::string& out, uint32_t cp)
            {
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
            }

            bool Hex4(uint32_t& out)
            {
                if (m_pos + 4 > m_text.size()) return false;
                out = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char h = m_text[m_pos++];
                    out <<= 4;
                    if (h >= '0' && h <= '9') out |= uint32_t(h - '0');
                    else if (h >= 'a' && h <= 'f') out |= uint32_t(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') out |= uint32_t(h - 'A' + 10);
                    else return false;
                }
                return true;
            }

            Result<std::string> ParseString()
            {
                ++m_pos;   // the opening quote
                std::string out;
                while (m_pos < m_text.size())
                {
                    const char c = m_text[m_pos++];
                    if (c == '"') return out;
                    if (static_cast<unsigned char>(c) < 0x20) return Fail("a control character in a string");
                    if (c != '\\') { out += c; continue; }
                    if (m_pos >= m_text.size()) break;
                    const char e = m_text[m_pos++];
                    switch (e)
                    {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u':
                        {
                            uint32_t cp = 0;
                            if (!Hex4(cp)) return Fail("a bad \\u escape");
                            if (cp >= 0xD800 && cp < 0xDC00)   // a surrogate pair
                            {
                                uint32_t low = 0;
                                if (!Literal("\\u") || !Hex4(low) || low < 0xDC00 || low >= 0xE000) return Fail("a lone surrogate");
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            }
                            AppendUtf8(out, cp);
                            break;
                        }
                        default: return Fail("a bad escape");
                    }
                }
                return Fail("an unterminated string");
            }

            Result<JsonValue> ParseArray(int depth)
            {
                ++m_pos;
                JsonValue array = JsonValue::MakeArray();
                SkipSpace();
                if (m_pos < m_text.size() && m_text[m_pos] == ']') { ++m_pos; return array; }
                while (true)
                {
                    Result<JsonValue> item = ParseValue(depth + 1);
                    if (!item) return item;
                    array.Push(std::move(*item));
                    SkipSpace();
                    if (m_pos < m_text.size() && m_text[m_pos] == ',') { ++m_pos; continue; }
                    if (m_pos < m_text.size() && m_text[m_pos] == ']') { ++m_pos; return array; }
                    return Fail("expected , or ]");
                }
            }

            Result<JsonValue> ParseObject(int depth)
            {
                ++m_pos;
                JsonValue object = JsonValue::MakeObject();
                SkipSpace();
                if (m_pos < m_text.size() && m_text[m_pos] == '}') { ++m_pos; return object; }
                while (true)
                {
                    SkipSpace();
                    if (m_pos >= m_text.size() || m_text[m_pos] != '"') return Fail("expected a member name");
                    Result<std::string> key = ParseString();
                    if (!key) return key.GetError();
                    SkipSpace();
                    if (m_pos >= m_text.size() || m_text[m_pos] != ':') return Fail("expected :");
                    ++m_pos;
                    Result<JsonValue> value = ParseValue(depth + 1);
                    if (!value) return value;
                    object.Set(*key, std::move(*value));
                    SkipSpace();
                    if (m_pos < m_text.size() && m_text[m_pos] == ',') { ++m_pos; continue; }
                    if (m_pos < m_text.size() && m_text[m_pos] == '}') { ++m_pos; return object; }
                    return Fail("expected , or }");
                }
            }

            std::string_view m_text;
            size_t           m_pos = 0;
        };

        void WriteString(std::string& out, std::string_view s)
        {
            out += '"';
            for (const char c : s)
            {
                switch (c)
                {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20)
                        {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
                            out += buf;
                        }
                        else
                            out += c;
                }
            }
            out += '"';
        }
    }

    Result<JsonValue> JsonValue::Parse(std::string_view text) { return Parser(text).ParseDocument(); }

    std::string JsonValue::Write() const
    {
        std::string out;
        WriteTo(out);
        return out;
    }

    void JsonValue::WriteTo(std::string& out) const
    {
        switch (m_value.index())
        {
            case 0: out += "null"; break;
            case 1: out += std::get<1>(m_value) ? "true" : "false"; break;
            case 2:
            {
                const double v = std::get<2>(m_value);
                if (!std::isfinite(v)) { out += "null"; break; }   // JSON has no NaN or infinity
                char buf[32];
                const auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
                out.append(buf, ec == std::errc{} ? end : buf);
                break;
            }
            case 3: WriteString(out, std::get<3>(m_value)); break;
            case 4:
            {
                out += '[';
                bool first = true;
                for (const JsonValue& item : std::get<4>(m_value))
                {
                    if (!first) out += ',';
                    first = false;
                    item.WriteTo(out);
                }
                out += ']';
                break;
            }
            default:
            {
                out += '{';
                bool first = true;
                for (const auto& [key, value] : std::get<5>(m_value))
                {
                    if (!first) out += ',';
                    first = false;
                    WriteString(out, key);
                    out += ':';
                    value.WriteTo(out);
                }
                out += '}';
            }
        }
    }

    const std::string&        JsonValue::AsString() const { return IsString() ? std::get<3>(m_value) : kEmptyString; }
    const JsonValue::Array&   JsonValue::Items() const    { return IsArray()  ? std::get<4>(m_value) : kEmptyArray; }
    const JsonValue::Object&  JsonValue::Members() const  { return IsObject() ? std::get<5>(m_value) : kEmptyObject; }

    const JsonValue* JsonValue::Find(std::string_view key) const
    {
        if (!IsObject()) return nullptr;
        for (const auto& [name, value] : std::get<5>(m_value))
            if (name == key) return &value;
        return nullptr;
    }

    JsonValue& JsonValue::Set(std::string_view key, JsonValue value)
    {
        if (!IsObject()) m_value = Object{};
        auto& members = std::get<5>(m_value);
        for (auto& [name, existing] : members)
            if (name == key) { existing = std::move(value); return existing; }
        members.emplace_back(std::string(key), std::move(value));
        return members.back().second;
    }

    JsonValue& JsonValue::Push(JsonValue value)
    {
        if (!IsArray()) m_value = Array{};
        auto& items = std::get<4>(m_value);
        items.push_back(std::move(value));
        return items.back();
    }
}
