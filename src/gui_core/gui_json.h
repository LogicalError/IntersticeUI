#pragma once

#include <gui_core/gui_result.h>

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace interstice
{
    // A JSON value (RFC 8259): null, bool, number, string, array or object. Objects keep
    // their members in order, so what is written reads back the same.
    class JsonValue
    {
    public:
        using Array  = std::vector<JsonValue>;
        using Object = std::vector<std::pair<std::string, JsonValue>>;

        JsonValue() = default;
        JsonValue(std::nullptr_t) {}
        JsonValue(bool value) : m_value(value) {}
        JsonValue(int value) : m_value(double(value)) {}
        JsonValue(double value) : m_value(value) {}
        JsonValue(std::string value) : m_value(std::move(value)) {}
        JsonValue(std::string_view value) : m_value(std::string(value)) {}
        JsonValue(const char* value) : m_value(std::string(value)) {}
        JsonValue(Array value) : m_value(std::move(value)) {}
        JsonValue(Object value) : m_value(std::move(value)) {}

        static JsonValue MakeArray()  { return JsonValue(Array{}); }
        static JsonValue MakeObject() { return JsonValue(Object{}); }

        // Depth-limited (64): deeper text is an error, not a stack overflow.
        static Result<JsonValue> Parse(std::string_view text);
        std::string Write() const;

        bool IsNull()   const { return m_value.index() == 0; }
        bool IsBool()   const { return m_value.index() == 1; }
        bool IsNumber() const { return m_value.index() == 2; }
        bool IsString() const { return m_value.index() == 3; }
        bool IsArray()  const { return m_value.index() == 4; }
        bool IsObject() const { return m_value.index() == 5; }

        // The value, or `fallback` when it is of another type.
        bool               AsBool(bool fallback = false) const      { return IsBool() ? std::get<1>(m_value) : fallback; }
        double             AsNumber(double fallback = 0.0) const    { return IsNumber() ? std::get<2>(m_value) : fallback; }
        const std::string& AsString() const;
        const Array&       Items() const;
        const Object&      Members() const;

        // An object's member by name, or null when absent (or not an object).
        const JsonValue* Find(std::string_view key) const;

        // Building: Set adds or replaces an object's member, Push appends to an array.
        JsonValue& Set(std::string_view key, JsonValue value);
        JsonValue& Push(JsonValue value);

    private:
        void WriteTo(std::string& out) const;

        std::variant<std::nullptr_t, bool, double, std::string, Array, Object> m_value = nullptr;
    };
}
