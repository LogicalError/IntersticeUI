#pragma once

#include <stdint.h>
#include <memory>

namespace tachyon
{
    /// @addtogroup elements
    /// @{

    /// How an ID was made; stored in 2 bits of the ID.
    enum class IDMode : uint64_t
    {
        Static   = 0, ///< persistent; created and released through IDManager, checked by generation
        Dynamic  = 1, ///< remade each frame from the call site or a hint and the parent stack
        Ranged   = 2, ///< remade each frame; a block of sub-IDs for indexed content (IDManager::GetSubID)
		SubRange = 3  ///< one item of a Ranged ID: the range's hash plus an index
    };

    /// A 64-bit element ID whose bit layout depends on its IDMode. Runtime only: not stable
    /// across runs, so never serialize it.
    struct ID
    {
        /// Bit layout sizes; internal.
        static constexpr uint64_t MODE_BITS = 2;

        static constexpr uint64_t MAX_STATIC_INDEX_BITS = 32;
        static constexpr uint64_t MAX_STATIC_GEN_BITS = (64 - MODE_BITS - MAX_STATIC_INDEX_BITS);
        static constexpr uint64_t MAX_STATIC_INDEX_MASK = (1ull << MAX_STATIC_INDEX_BITS) - 1;
        static constexpr uint64_t MAX_STATIC_GEN_MASK = (1ull << MAX_STATIC_GEN_BITS) - 1;

        static constexpr uint64_t MAX_RANGED_SIZE_BITS = 25;
        static constexpr uint64_t MAX_RANGED_HASH_BITS = (64 - MODE_BITS - MAX_RANGED_SIZE_BITS);
        static constexpr uint64_t MAX_RANGED_SIZE_MASK = (1ull << MAX_RANGED_SIZE_BITS) - 1;
        static constexpr uint64_t MAX_RANGED_HASH_MASK = (1ull << MAX_RANGED_HASH_BITS) - 1;

        /// The packed bits, viewed per mode; internal (use GetMode / Value / IDManager).
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
        /// The "no element" ID (all bits zero); also the default for optional ID parameters.
        static constexpr ID Invalid() { return { 0 }; }

        /// How the ID was made.
        inline IDMode GetMode() const { return static_cast<IDMode>(value.staticParts.mode); }

        /// The raw 64 bits, e.g. as a hash or map key; not stable across runs.
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

    /// @}
} // namespace tachyon

namespace std
{
    template<>
    struct hash<tachyon::ID>
    {
        inline size_t operator()(const tachyon::ID& id) const noexcept
        {
            return static_cast<size_t>(id.value.integer);
        }
    };
}