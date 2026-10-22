#pragma once

#include <gui_core/gui_platform.h>

#include <stdint.h>
#include <vector>
#include <string_view>

// Uncomment to enable debug ID tracking
//#define GUI_ID_DEBUG

// ID System
// Provides static, dynamic, and ranged-based unique identifiers for GUI elements.
namespace GUI 
{
    struct ID;
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

        // Resets the parent stack and dynamic/ranged counters. Call before creating any IDs.
        public: [[nodiscard]] bool BeginFrame();
        
        // Checks the parent stack is back to just the root Invalid ID.
        public: void EndFrame();

        public: inline [[nodiscard]] bool IsInsideFrame() const { return !m_parent_stack.empty(); }

        // ------------------------------------------------------------------------
        // Parent Stack
        // ------------------------------------------------------------------------
        
        // Dynamic and ranged IDs created under it inherit its hash. Any ID type can be
        // a parent; returns false for an Invalid ID.
        public: [[nodiscard]] bool PushParent(ID parent_id);

        // Never pops the root ID.
        public: [[nodiscard]] bool PopParent();

        // ------------------------------------------------------------------------
        // Static IDs
        // ------------------------------------------------------------------------

        // Persistent ID tracked by slot and generation; must be released explicitly.
        public: [[nodiscard]] ID CreateStaticID();
        
        // Bumps the slot's generation, invalidating old references. Only the current generation can be released.
        public: bool ReleaseStaticID(ID id);

        // Tests reach it through IDManagerTestAccess (defined in the tests).
        private: bool SetGenerationForSlot(uint32_t index, int32_t generation);
        friend struct IDManagerTestAccess;

        // True if the slot is active and the generation matches.
        public: bool IsStaticIDValid(ID id) const;

        // ------------------------------------------------------------------------
        // Dynamic IDs
        // ------------------------------------------------------------------------

        private: [[nodiscard]] ID AllocateDynamicID(uint64_t hash);

#ifdef SUPPORT_CPP20
        // Allocate a dynamic ID automatically using the source location.
        // Will return a different ID for each call site. Will be affected by parent stack.
        public: inline [[nodiscard]] ID AllocateDynamicID(const std::source_location location = std::source_location::current())
        {
            return AllocateDynamicID(location.file_name(), location.line());
        }
#endif

        // Allocate a dynamic ID using file and line information. (use __FILE__ and __LINE__)
        // Will return a different ID for each call site. Will be affected by parent stack.
        public: [[nodiscard]] ID AllocateDynamicID(std::string_view file, int line);

        // Allocate a dynamic ID using a string hint.
        // Will return a different ID for each call site. Will be affected by parent stack.
        public: [[nodiscard]] ID AllocateDynamicID(std::string_view hint);

        // Debug: checks the tracking list (slow). Release: true for any non-Invalid Dynamic ID.
        public: [[nodiscard]] bool IsDynamicIDValid(ID id) const;

        // ------------------------------------------------------------------------
        // Ranged IDs
        // ------------------------------------------------------------------------

        private: [[nodiscard]] ID AllocateRangedID(uint32_t size, uint64_t hash);

#ifdef SUPPORT_CPP20
        // Allocate a ranged ID automatically using the source location.
        // Will return a different ID for each call site. Will be affected by parent stack.
        public: inline [[nodiscard]] ID AllocateRangedID(uint32_t size, const std::source_location location = std::source_location::current())
        {
            return AllocateRangedID(size, location.file_name(), location.line());
        }
#endif
        // Allocate a ranged ID using file and line information. (use __FILE__ and __LINE__)
        // Will return a different ID for each call site. Will be affected by parent stack.
        public: [[nodiscard]] ID AllocateRangedID(uint32_t size, std::string_view file, int line);

        // Allocate a ranged of IDs using a string hint.
        // Will return a different ID for each call site. Will be affected by parent stack.
        public: [[nodiscard]] ID AllocateRangedID(uint32_t size, std::string_view hint);

        // Get the size of a ranged ID.
        public: uint32_t GetRangeSize(ID ranged_id) const;

        // Debug: checks stored ranges (slow). Release: true for any non-Invalid Ranged ID.
        public: bool IsRangedIDValid(ID id) const;
                       
        // Sub-range IDs inherit the range's hash and use the index within it.
        public: [[nodiscard]] ID GetSubID(ID ranged_id, uint32_t index) const;

        // Debug: checks stored ranges (slow). Release: true for any non-Invalid SubRange ID.
        public: bool IsSubRangeIDValid(ID id) const;

        // ------------------------------------------------------------------------
        // Parent Stack Utilities
        // ------------------------------------------------------------------------

        // Slow: linear search.
        public: bool IsInParentStack(ID id) const;
              
        // Get the current top parent ID from the stack.
        public: ID GetStackParent() const;


        // ------------------------------------------------------------------------
        // Validation
        // ------------------------------------------------------------------------

        // Validate any ID. (slow in debug mode)
        // Returns true if the ID is valid.
        public: bool IsIDValid(ID id) const;
                      
        public: inline size_t GetStaticIDCount() const { return m_static_id_count; }
        public: inline size_t GetDynamicIDCount() const { return m_dynamic_id_total; }
        public: inline size_t GetRangedIDCount() const { return m_ranged_count; }



    private:
        struct ParentData
        {
            ID id;
            uint32_t counter = 0;
        };

        // Parent stack for hierarchical hashing
        std::vector<ParentData> m_parent_stack;
        
        // Static ID management (slot map with generations)
        std::vector<int32_t> m_static_slots;
        std::vector<uint32_t> m_static_free_list;
        size_t m_static_id_count = 0;

        // Dynamic ID tracking
        size_t m_dynamic_id_total = 0; 

        // Ranged ID tracking
        size_t m_ranged_count = 0;

#ifdef GUI_ID_DEBUG
        std::vector<ID> m_debug_dynamic_ids;
        std::vector<ID> m_debug_ranges;
#endif
    };

} // namespace GUI
