#include <gui_core/gui_id.h>
#include <gui_core/gui_id_manager.h>
#include <gui_core/gui_hash.h>

#include <assert.h>
#include <gui_core/diagnostics/gui_logging.h>
#include <vector>
        
namespace interstice
{
    namespace
    {
        // Misuse (a call outside the frame, an ID of the wrong kind, ...) is reported and the
        // call returns its failure value.
        constexpr Diagnostics::LogCategory kLogID = Diagnostics::MakeLogCategory("id");
    }

    bool IDManager::BeginFrame()
    {
        if (IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Previous frame has not ended when calling BeginFrame");
            return false;
        }
        m_dynamic_id_total = 0;
        m_ranged_count = 0;
#ifdef INTERSTICE_ID_DEBUG
        m_debug_dynamic_ids.clear();
        m_debug_ranges.clear();
#endif
        m_parent_stack.clear(); // for release mode safety
        m_parent_stack.push_back(ParentData{ .id = ID::Invalid(), .counter = 0 });
        return true;
    }
        
    void IDManager::EndFrame()
    {
        if (m_parent_stack.size() != 1 || m_parent_stack.back().id != ID::Invalid())
            Diagnostics::ReportMisuse(kLogID, "IDManager: PushParent left unpopped at EndFrame (cleared)");
        m_parent_stack.clear();
    }

    bool IDManager::PushParent(ID parent_id)
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: PushParent() can only be called between Begin/EndFrame");
            return false;
        }
        if (parent_id == ID::Invalid())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Pushing Invalid ID as parent");
            return false;
        }
        m_parent_stack.push_back(ParentData{ .id = parent_id, .counter = 0 });
        return true;
    }

    bool IDManager::PopParent()
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: PopParent() can only be called between Begin/EndFrame");
            return false;
        }
        if (m_parent_stack.size() == 1)
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Parent stack underflow");
            return false;
        }
        m_parent_stack.pop_back();
        return true;
    }

    ID IDManager::CreateStaticID()
    {
        uint32_t index;
        if (!m_static_free_list.empty()) 
        {
            index = m_static_free_list.back();
            m_static_free_list.pop_back();
        } else 
        {
            index = static_cast<uint32_t>(m_static_slots.size());
            m_static_slots.push_back(-1); // Start at gen 1 (inactive)
        }

        int32_t& generation = m_static_slots[index];
        assert(generation < 0 && generation >= -static_cast<int32_t>(ID::MAX_STATIC_GEN_MASK) && "Static ID generation overflow (should never happen)");
        generation = std::abs(generation);
        m_static_id_count++;

        return ID{
                    .value =
                    {
                        .staticParts =
                        {
                            .mode = IDMode::Static,
                            .slotIndex = index,
                            .generation = (uint32_t)generation,
                        }
                    }
                };
    }
        
    bool IDManager::ReleaseStaticID(ID id)
    {
        if (id.GetMode() != IDMode::Static)
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Not a static ID");
            return false;
        }

        const uint32_t index = id.value.staticParts.slotIndex;
        const uint32_t gen = id.value.staticParts.generation;

        if (index >= m_static_slots.size())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Invalid slot index");
            return false;
        }
        int32_t& generation = m_static_slots[index];
            
        // Only release if the ID matches the current occupant exactly
        if (generation <= 0 || static_cast<uint32_t>(generation) != gen)
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Generation mismatch");
            return false;
        }

        // Increase generation
        generation = std::abs(generation);
        generation++;
        if (generation < 0 || generation > ID::MAX_STATIC_GEN_MASK) generation = 1; // wrap around protection

        // Mark slot as inactive (negative)
        generation = -generation; 

        m_static_free_list.push_back(index);
        m_static_id_count--;
        return true;
    }

    bool IDManager::SetGenerationForSlot(uint32_t index, int32_t generation)
    {
        if (index >= m_static_slots.size())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Invalid slot index");
            return false;
        }

        int32_t& slot_generation = m_static_slots[index];
        slot_generation = generation;
        return true;
    }

    bool IDManager::IsStaticIDValid(ID id) const
    {
        if (id.GetMode() != IDMode::Static) return false;
        if (id == ID::Invalid()) return false;
        const uint32_t index = id.value.staticParts.slotIndex;
        const uint32_t gen = id.value.staticParts.generation;
        return (index < m_static_slots.size()) &&
                (m_static_slots[index] > 0) && // positive = active
                (static_cast<uint32_t>(m_static_slots[index]) == gen);
    }

    ID IDManager::AllocateDynamicID(uint64_t hash)
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: AllocateDynamicID() can only be called between Begin/EndFrame");
            return ID::Invalid();
        }
        m_dynamic_id_total++;
        ParentData& parentData = m_parent_stack.back();
        parentData.counter++;
        //*
        hash = Hash::Combine(hash, parentData.counter, parentData.id.value.integer);
        
        // Ensure non-zero
        if (hash == 0) hash = 1;

        ID id = ID::Invalid();
        id.value = { .dynamicParts =
                    {
                        .mode = IDMode::Dynamic,
                        .hashValue = hash
                    }};
        /*/
        hash = Hash::Combine(hash, parentData.id.value.integer);

        uint32_t hash32 = (uint32_t)(hash ^ (hash >> 32));

        // Ensure non-zero
        if (hash32 == 0) hash32 = 1;

        ID id = ID::Invalid();
        id.value = { .dynamicParts =
                    {
                        .mode = IDMode::Dynamic,
                        .hashValue = hash32 | ((uint64_t)parentData.counter << 32)
                    } };
        //*/
#ifdef INTERSTICE_ID_DEBUG
        m_debug_dynamic_ids.push_back(id);
#endif
        return id;
    }

    bool IDManager::IsDynamicIDValid(ID id) const
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: IsDynamicIDValid() can only be called between Begin/EndFrame");
            return false;
        }
        if (id.GetMode() != IDMode::Dynamic) return false;
        if (id == ID::Invalid()) return false;
        const uint64_t hash = id.value.dynamicParts.hashValue;
        if (hash == 0) return false;
#ifdef INTERSTICE_ID_DEBUG
        for (ID debug_id : m_debug_dynamic_ids)
        {
            if (debug_id.value.dynamicParts.hashValue == hash)
            {
                return true;
            }
        }
        return false;
#else
        // In release mode, we cannot validate dynamic IDs
        return true;
#endif
    }

    ID IDManager::AllocateDynamicID(std::string_view file, int line)
    {
        return AllocateDynamicID(Hash::Combine(Hash::Calculate(file), static_cast<uint64_t>(line)));
    }

    ID IDManager::AllocateDynamicID(std::string_view hint)
    {
        return AllocateDynamicID(Hash::Calculate(hint));
    }

    ID IDManager::AllocateRangedID(uint32_t size, uint64_t hash)
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: AllocateRangedID() can only be called between Begin/EndFrame");
            return ID::Invalid();
        }
        if (size <= 0 || size > ID::MAX_RANGED_SIZE_MASK) 
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Ranged size out of bounds");
            return ID::Invalid();
        }
        hash = Hash::Combine(hash, m_parent_stack.back().id.value.integer);
        hash &= ID::MAX_RANGED_HASH_MASK;
        if (hash == 0) hash = 1;
        m_ranged_count++;
        ID id = ID::Invalid();
        id.value = { .rangedParts =
                    {
                        .mode = IDMode::Ranged,
                        .rangedSize = size,
                        .hashValue = hash
                    }};
#ifdef INTERSTICE_ID_DEBUG
        m_debug_ranges.push_back(id);
#endif
        return id;
    }
    
    ID IDManager::AllocateRangedID(uint32_t size, std::string_view file, int line)
    {
        return AllocateRangedID(size, Hash::Combine(Hash::Calculate(file), static_cast<uint64_t>(line)));
    }

    ID IDManager::AllocateRangedID(uint32_t size, std::string_view hint)
    {
        return AllocateRangedID(size, Hash::Calculate(hint));
    }

    uint32_t IDManager::GetRangeSize(ID ranged_id) const
    {
        if (ranged_id.GetMode() != IDMode::Ranged)
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Not a ranged ID");
            return 0;
        }
        return ranged_id.value.rangedParts.rangedSize;
    }
        
    bool IDManager::IsRangedIDValid(ID id) const
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: IsRangedIDValid() can only be called between Begin/EndFrame");
            return false;
        }
        if (id.GetMode() != IDMode::Ranged) return false;
        if (id == ID::Invalid()) return false;
        const uint64_t hash = id.value.rangedParts.hashValue;
        if (hash == 0) return false;
#ifdef INTERSTICE_ID_DEBUG
        for (const auto& ranged_id : m_debug_ranges)
        {
            if (ranged_id.value.rangedParts.hashValue == hash)
            {
                return true;
            }
        }
        return false;
#else
        // In release mode, we cannot validate ranged IDs
        return true;
#endif
    }
                       
    ID IDManager::GetSubID(ID ranged_id, uint32_t index) const
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: GetSubID() can only be called between Begin/EndFrame");
            return ID::Invalid();
        }
        if (ranged_id.GetMode() != IDMode::Ranged)
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Not a ranged ID");
            return ID::Invalid();
        }
        if (ranged_id == ID::Invalid())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Not a ranged ID");
            return ID::Invalid();
        }
        const uint32_t ranged_size = ranged_id.value.rangedParts.rangedSize;
        if (index >= ranged_size)
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: Index out of range");
            return ID::Invalid();
        }
        const uint64_t parent_hash = ranged_id.value.rangedParts.hashValue;
        return ID{
                    .value = 
                    {
                        .subRangeParts =
                        {
                            .mode = IDMode::SubRange,
                            .index = index,
                            .parentHashValue = parent_hash
                        }
                    } 
                 };
    }

    bool IDManager::IsSubRangeIDValid(ID id) const
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: IsSubRangeIDValid() can only be called between Begin/EndFrame");
            return false;
        }
        if (id.GetMode() != IDMode::SubRange) return false;
        if (id == ID::Invalid()) return false;
        const uint64_t parent_hash = id.value.subRangeParts.parentHashValue;
        if (parent_hash == 0) return false;

        // Find the parent range in debug info (only in debug mode)
#ifdef INTERSTICE_ID_DEBUG
        const uint32_t index = id.value.subRangeParts.index;
        for (const auto& ranged_id : m_debug_ranges)
        {
            // check if parent ranged id is valid and index within range
            if (ranged_id.value.rangedParts.hashValue == parent_hash)
            {
                return index < ranged_id.value.rangedParts.rangedSize;
            }
        }
        return false;
#else
        // In release mode, we cannot validate subrange IDs
        return true;
#endif

    }
    
    bool IDManager::IsInParentStack(ID id) const
    {
        if (id == ID::Invalid()) return false;
        for (const ParentData& parent : m_parent_stack)
        {
            if (parent.id == id) return true;
        }
        return false;
    }
              
    ID IDManager::GetStackParent() const
    {
        if (!IsInsideFrame())
        {
            Diagnostics::ReportMisuse(kLogID, "IDManager: GetStackParent() can only be called between Begin/EndFrame");
            return ID::Invalid();
        }
        return m_parent_stack.back().id;
    }

    bool IDManager::IsIDValid(ID id) const
    {
        if (id == ID::Invalid()) return false;
        switch (id.GetMode())
        {
            case IDMode::Dynamic:  return IsDynamicIDValid(id);
            case IDMode::Static:   return IsStaticIDValid(id);
            case IDMode::Ranged:   return IsRangedIDValid(id);
            case IDMode::SubRange: return IsSubRangeIDValid(id);
            default: return false;
        }
    }

} // namespace interstice
