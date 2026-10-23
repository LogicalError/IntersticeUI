#pragma once

#include <stdint.h>
#include <memory>

namespace interstice
{
    // Stored as 2 bits for mode
    enum class IDMode : uint64_t
    {
        Static   = 0, // Manually allocated IDs with slot and generation for reuse
        Dynamic  = 1, // IDs recreated each frame with hierarchical hashing for uniqueness without tracking
        Ranged   = 2, // IDs recreated each frame, representing a range of sub-IDs, with a hash for validation and index-based sub-ID generation
		SubRange = 3  // IDs recreated each frame, representing a single sub-ID within a parent range, inheriting the parent's hash and using an index for uniqueness
    };

    // ID is a 64-bit value with different bit layouts per mode
    // NOT DESIGNED FOR SERIALIZATION, ONLY RUNTIME
    struct ID
    {
        static constexpr uint64_t MODE_BITS = 2;

        static constexpr uint64_t MAX_STATIC_INDEX_BITS = 32;
        static constexpr uint64_t MAX_STATIC_GEN_BITS = (64 - MODE_BITS - MAX_STATIC_INDEX_BITS);
        static constexpr uint64_t MAX_STATIC_INDEX_MASK = (1ull << MAX_STATIC_INDEX_BITS) - 1;
        static constexpr uint64_t MAX_STATIC_GEN_MASK = (1ull << MAX_STATIC_GEN_BITS) - 1;

        static constexpr uint64_t MAX_RANGED_SIZE_BITS = 25;
        static constexpr uint64_t MAX_RANGED_HASH_BITS = (64 - MODE_BITS - MAX_RANGED_SIZE_BITS);
        static constexpr uint64_t MAX_RANGED_SIZE_MASK = (1ull << MAX_RANGED_SIZE_BITS) - 1;
        static constexpr uint64_t MAX_RANGED_HASH_MASK = (1ull << MAX_RANGED_HASH_BITS) - 1;

        union Value
        {
            uint64_t integer;
            struct StaticParts
            {
                IDMode   mode : MODE_BITS;
                uint64_t slotIndex : MAX_STATIC_INDEX_BITS;
                uint64_t generation : MAX_STATIC_GEN_BITS;
            } staticParts;
            struct DynamicParts
            {
                IDMode   mode : MODE_BITS;
                uint64_t hashValue : 62;
            } dynamicParts;
            struct RangedParts
            {
                IDMode   mode : MODE_BITS;
                uint64_t rangedSize : MAX_RANGED_SIZE_BITS;
                uint64_t hashValue : MAX_RANGED_HASH_BITS;
            } rangedParts;
            struct SubRangeParts
            {
                IDMode   mode : MODE_BITS;
                uint64_t index : MAX_RANGED_SIZE_BITS;
                uint64_t parentHashValue : MAX_RANGED_HASH_BITS;
            } subRangeParts;
        } value;
        static constexpr ID Invalid() { return { 0 }; }

        // Get the mode of the ID
        inline IDMode GetMode() const { return static_cast<IDMode>(value.staticParts.mode); }

		inline uint64_t Value() const { return value.integer; }

        constexpr bool operator==(const ID& other) const noexcept { return value.integer == other.value.integer; }
        constexpr bool operator!=(const ID& other) const noexcept { return value.integer != other.value.integer; }
        constexpr bool operator<(const ID& other) const noexcept  { return value.integer <  other.value.integer; }
        constexpr bool operator>(const ID& other) const noexcept  { return value.integer >  other.value.integer; }
        constexpr bool operator<=(const ID& other) const noexcept { return value.integer <= other.value.integer; }
        constexpr bool operator>=(const ID& other) const noexcept { return value.integer >= other.value.integer; }
    };
    static_assert(sizeof(ID) == sizeof(uint64_t), "ID size must be 64 bits");
    static_assert(sizeof(ID::Value::StaticParts) == sizeof(uint64_t), "ID size must be 64 bits");
    static_assert(sizeof(ID::Value::DynamicParts) == sizeof(uint64_t), "ID size must be 64 bits");
    static_assert(sizeof(ID::Value::RangedParts) == sizeof(uint64_t), "ID size must be 64 bits");
    static_assert(sizeof(ID::Value::SubRangeParts) == sizeof(uint64_t), "ID size must be 64 bits");

} // namespace interstice

namespace std
{
    template<>
    struct hash<interstice::ID>
    {
        inline size_t operator()(const interstice::ID& id) const noexcept
        {
            return static_cast<size_t>(id.value.integer);
        }
    };
}