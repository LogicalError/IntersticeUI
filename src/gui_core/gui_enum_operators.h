// This header defines TACHYON_ENUM_OPERATORS for enum class bitwise ops

#pragma once

// Macro to enable bitwise operators for enum classes
#define TACHYON_ENUM_OPERATORS(EnumType) \
    inline static EnumType operator|(EnumType a, EnumType b) { return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) | static_cast<std::underlying_type_t<EnumType>>(b)); } \
    inline static EnumType operator&(EnumType a, EnumType b) { return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) & static_cast<std::underlying_type_t<EnumType>>(b)); } \
    inline static EnumType operator~(EnumType a) { return static_cast<EnumType>(~static_cast<std::underlying_type_t<EnumType>>(a)); } \
    inline static bool operator!(EnumType a) { return static_cast<std::underlying_type_t<EnumType>>(a) == 0; } \
    inline static bool operator==(EnumType a, EnumType b) { return static_cast<std::underlying_type_t<EnumType>>(a) == static_cast<std::underlying_type_t<EnumType>>(b); } \
    inline static bool operator!=(EnumType a, EnumType b) { return static_cast<std::underlying_type_t<EnumType>>(a) != static_cast<std::underlying_type_t<EnumType>>(b); } \
    inline static EnumType& operator|=(EnumType& a, EnumType b) { a = a | b; return a; } \
    inline static EnumType& operator&=(EnumType& a, EnumType b) { a = a & b; return a; }
