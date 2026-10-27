#pragma once

#include <gui_core/gui_platform.h>

#include <stdint.h>
#include <source_location>
#include <vector>
#include <string_view>

// Uncomment to enable debug ID tracking
//#define TACHYON_ID_DEBUG

// ID system: static, dynamic and ranged identifiers for GUI elements.
namespace tachyon 
{
    struct ID;
    /// Makes element IDs: per-frame dynamic / ranged IDs (hashed from the call site or a hint
    /// and the parent stack) and persistent static IDs.
    class IDManager
    {
        // ------------------------------------------------------------------------
        // Constructor
        // ------------------------------------------------------------------------
        public: IDManager()
        {
            m_parent_stack.reserve(4096);
            m_static_slots.reserve(4096);
            m_static_free_list.reserve(4096);
        }

        // ------------------------------------------------------------------------
        // Frame Management
        // ------------------------------------------------------------------------

        /// Resets the parent stack and dynamic/ranged counters. Call before creating any IDs.
        /// False (misuse) when the previous frame was not ended.
        public: [[nodiscard]] bool BeginFrame();

        /// Clears the parent stack; reports misuse when a PushParent was left unpopped.
        public: void EndFrame();

        /// True between BeginFrame and EndFrame.
        public: [[nodiscard]] inline bool IsInsideFrame() const { return !m_parent_stack.empty(); }

        // ------------------------------------------------------------------------
        // Parent Stack
        // ------------------------------------------------------------------------
        
        /// Makes `parent_id` the parent: dynamic and ranged IDs created under it inherit its hash.
        /// Any ID mode can be a parent; returns false for an Invalid ID or outside a frame.
        public: [[nodiscard]] bool PushParent(ID parent_id);

        /// Pops the last PushParent. Never pops the root; false on underflow or outside a frame.
        public: [[nodiscard]] bool PopParent();

        // ------------------------------------------------------------------------
        // Static IDs
        // ------------------------------------------------------------------------

        /// A persistent ID tracked by slot and generation; release it with ReleaseStaticID.
        public: [[nodiscard]] ID CreateStaticID();

        /// Frees the slot and bumps its generation, invalidating copies of `id`. False when `id`
        /// is not the slot's current generation.
        public: bool ReleaseStaticID(ID id);

        // Tests reach it through IDManagerTestAccess (defined in the tests).
        private: bool SetGenerationForSlot(uint32_t index, int32_t generation);
        friend struct IDManagerTestAccess;

        /// True if the slot is active and the generation matches.
        public: bool IsStaticIDValid(ID id) const;

        // ------------------------------------------------------------------------
        // Dynamic IDs
        // ------------------------------------------------------------------------

        private: [[nodiscard]] ID AllocateDynamicID(uint64_t hash);

#ifdef TACHYON_SUPPORT_CPP20
        /// A dynamic ID from the caller's source location and the parent stack; different per
        /// call site. Repeated calls from one site under one parent are numbered in call order.
        public: [[nodiscard]] inline ID AllocateDynamicID(const std::source_location location = std::source_location::current())
        {
            return AllocateDynamicID(location.file_name(), location.line());
        }
#endif

        /// A dynamic ID from `file` and `line` (__FILE__, __LINE__) and the parent stack; different
        /// per call site. Repeated calls from one site under one parent are numbered in call order.
        public: [[nodiscard]] ID AllocateDynamicID(std::string_view file, int line);

        /// A dynamic ID from `hint` and the parent stack; different per hint. Repeated calls with
        /// one hint under one parent are numbered in call order.
        public: [[nodiscard]] ID AllocateDynamicID(std::string_view hint);

        /// Debug: checks the tracking list (slow). Release: true for any non-Invalid Dynamic ID.
        public: [[nodiscard]] bool IsDynamicIDValid(ID id) const;

        // ------------------------------------------------------------------------
        // Ranged IDs
        // ------------------------------------------------------------------------

        private: [[nodiscard]] ID AllocateRangedID(uint32_t size, uint64_t hash);

#ifdef TACHYON_SUPPORT_CPP20
        /// A ranged ID of `size` sub-IDs from the caller's source location and the parent stack.
        /// Unlike dynamic IDs, repeated calls from one site under one parent return the same ID.
        public: [[nodiscard]] inline ID AllocateRangedID(uint32_t size, const std::source_location location = std::source_location::current())
        {
            return AllocateRangedID(size, location.file_name(), location.line());
        }
#endif
        /// A ranged ID of `size` sub-IDs from `file` and `line` (__FILE__, __LINE__) and the
        /// parent stack. Repeated calls from one site under one parent return the same ID.
        public: [[nodiscard]] ID AllocateRangedID(uint32_t size, std::string_view file, int line);

        /// A ranged ID of `size` sub-IDs from `hint` and the parent stack; different per hint.
        public: [[nodiscard]] ID AllocateRangedID(uint32_t size, std::string_view hint);

        /// The number of sub-IDs in a ranged ID; 0 (and a misuse report) for other modes.
        public: uint32_t GetRangeSize(ID ranged_id) const;

        /// Debug: checks stored ranges (slow). Release: true for any non-Invalid Ranged ID.
        public: bool IsRangedIDValid(ID id) const;

        /// The ID of item `index` (< range size) in a ranged ID, for indexed content such as
        /// list rows. Invalid when `index` is out of range.
        public: [[nodiscard]] ID GetSubID(ID ranged_id, uint32_t index) const;

        /// Debug: checks stored ranges (slow). Release: true for any non-Invalid SubRange ID.
        public: bool IsSubRangeIDValid(ID id) const;

        // ------------------------------------------------------------------------
        // Parent Stack Utilities
        // ------------------------------------------------------------------------

        /// True when `id` is anywhere on the parent stack. Slow: linear search.
        public: bool IsInParentStack(ID id) const;

        /// The innermost parent; Invalid at the root or outside a frame.
        public: ID GetStackParent() const;


        // ------------------------------------------------------------------------
        // Validation
        // ------------------------------------------------------------------------

        /// True when `id` is valid for its mode (see the Is*IDValid functions); slow in debug builds.
        public: bool IsIDValid(ID id) const;

        /// Live static IDs, and dynamic / ranged IDs made so far this frame.
        ///@{
        public: inline size_t GetStaticIDCount() const { return m_static_id_count; }
        public: inline size_t GetDynamicIDCount() const { return m_dynamic_id_total; }
        public: inline size_t GetRangedIDCount() const { return m_ranged_count; }
        ///@}



    private:
        struct ParentData
        {
            ID id;
        };

        // Parent stack for hierarchical hashing
        std::vector<ParentData> m_parent_stack;
        // Dynamic IDs made so far this frame per call site (or hint) and parent: only a
        // repeat of the same site renumbers, so a widget appearing elsewhere doesn't. A flat
        // open-addressed table: a slot stamped with an older frame is empty, so a new frame
        // clears nothing and allocates nothing once the table has grown.
        struct SiteSlot { uint64_t site = 0; uint32_t count = 0; uint32_t frame = 0; };
        std::vector<SiteSlot> m_site_slots = std::vector<SiteSlot>(1024);
        uint32_t              m_site_used  = 0;
        uint32_t              m_frame      = 0;
        uint32_t NextSiteCount(uint64_t site);
        
        // Static ID management (slot map with generations)
        std::vector<int32_t> m_static_slots;
        std::vector<uint32_t> m_static_free_list;
        size_t m_static_id_count = 0;

        // Dynamic ID tracking
        size_t m_dynamic_id_total = 0; 

        // Ranged ID tracking
        size_t m_ranged_count = 0;

#ifdef TACHYON_ID_DEBUG
        std::vector<ID> m_debug_dynamic_ids;
        std::vector<ID> m_debug_ranges;
#endif
    };

} // namespace tachyon
