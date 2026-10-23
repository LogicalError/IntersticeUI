#pragma once

#include <stdint.h>
#include <string_view>

namespace interstice
{
    namespace Hash
    {
        // FNV-1a Hash constants
        constexpr uint64_t Basis = 14695981039346656037ULL;
        constexpr uint64_t Prime = 1099511628211ULL;

        // Compile-time hash
        constexpr uint64_t CalculateConst(std::string_view str, uint64_t hash = Basis)
        {
            for (char c : str)
            {
                hash ^= static_cast<uint64_t>(c);
                hash *= Prime;
            }
            return hash;
        }

        // Runtime hash for dynamic strings
        inline uint64_t Calculate(std::string_view str)
        {
            uint64_t hash = Basis;
            for (size_t i = 0; i < str.size(); ++i)
            {
                hash ^= static_cast<uint64_t>(str[i]);
                hash *= Prime;
            }
            return hash;
        }

        // Hash a raw buffer of bytes using FNV-1a
        inline uint64_t CalculateBytes(const void* data, size_t len)
        {
            const uint8_t* ptr = static_cast<const uint8_t*>(data);
            uint64_t hash = Basis;
            for (size_t i = 0; i < len; ++i)
            {
                hash ^= static_cast<uint64_t>(ptr[i]);
                hash *= Prime;
            }
            return hash;
        }

        // Generic Calculate for trivially-copyable types (avoids colliding with string_view overload)
        // Pointers are rejected: hashing one hashes the address, not the object.
        template <typename T,
                  typename = std::enable_if_t<std::is_trivially_copyable_v<std::decay_t<T>> &&
                                              !std::is_pointer_v<std::decay_t<T>> &&
                                              !std::is_same_v<std::decay_t<T>, std::string_view>>>
        inline uint64_t Calculate(const T& value)
        {
            return CalculateBytes(&value, sizeof(T));
        }

        // Order-dependent combine; repeated values don't cancel out.
        inline constexpr uint64_t Combine(uint64_t a, uint64_t b)
        {
            return a ^ (b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2));
        }

        template<typename... Args>
        inline constexpr uint64_t Combine(const Args&... args)
        {
            uint64_t hash = Basis;
            ((hash = Combine(hash, Calculate(args))), ...);
            return hash;
        }
    }
}