#include <gtest/gtest.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_gradient_manager.h>
#include <gui_core/style/gui_style_manager.h>

#include <gui_core/windows/gui_window_registry.h>

#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/gui_id_manager.h>
#include <vector>

namespace tachyon
{

    class WindowRegistryTest : public ::testing::Test
    {
    protected:
        GradientManager gradientManager{ 256 };
        StyleManager styleManager{ gradientManager };
        StyleRef defaultStyleIndex{ styleManager.Register(Style{}) };
        ElementStorage storage{ styleManager };
        WindowRegistry registry{};
        IDManager idManager{};
        ID id;

        Element CreateElement(uint64_t id_val = 1, ElementFlags flags = ElementFlags::None)
        {
            Element e{ .styleRef = defaultStyleIndex, .flags = flags };
            e.id = ID{ .value {.integer = id_val } };
            return e;
        }

        // Helper to find an ElementRef by ID at a given depth (mirrors ElementStorageTest helper)
        ElementRef FindRefByIDAtDepth(uint64_t id_val, uint32_t depth) const
        {
            if (depth >= storage.NumberOfDepthLevels()) return ElementRef::Invalid();
            for (uint32_t i = 0; i < storage.DepthSizeOf(depth); ++i)
            {
                ElementRef ref{ depth, i };
                const Element* e = storage.GetElementPtr(ref);
                if (e && e->id.Value() == id_val) return ref;
            }
            return ElementRef::Invalid();
        }


        void SetUp() override
        {
            [[maybe_unused]] bool result = idManager.BeginFrame();
            assert(result);
            id = idManager.AllocateDynamicID();
        }

        void TearDown() override
        {
            idManager.EndFrame();
        }
    };

    TEST_F(WindowRegistryTest, BeginWindowScope_InvalidRef)
    {
        RegistryResult r = registry.BeginWindowScope(id, ElementRef::Invalid());
        EXPECT_EQ(r, RegistryResult::InvalidRef);
    }

    TEST_F(WindowRegistryTest, Add_InvalidRefAndNoActiveWindow)
    {
        // invalid ref
        RegistryResult r1 = registry.Add(ElementRef::Invalid());
        EXPECT_EQ(r1, RegistryResult::InvalidRef);

        // create a valid element in storage but do NOT open a window scope
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        auto addRes = storage.Add(CreateElement(42));
        ASSERT_TRUE(addRes);
        // do not call registry.BeginWindowScope
        RegistryResult r2 = registry.Add(addRes.ref);
        EXPECT_EQ(r2, RegistryResult::NoActiveWindow);
        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);
    }

    TEST_F(WindowRegistryTest, EndWindowScope_NoActiveWindow)
    {
        RegistryResult r = registry.EndWindowScope();
        EXPECT_EQ(r, RegistryResult::NoActiveWindow);
    }

    TEST_F(WindowRegistryTest, WindowLifecycleAndMembers)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        registry.Clear();

        // Window A
        auto wA = storage.Add(CreateElement(100, ElementFlags::IsWindow));
        ASSERT_TRUE(wA);
        EXPECT_EQ(registry.BeginWindowScope(id, wA.ref), RegistryResult::Success);

        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto childA1 = storage.Add(CreateElement(101));
        ASSERT_TRUE(childA1);
        EXPECT_EQ(registry.Add(childA1.ref), RegistryResult::Success);

        auto childA2 = storage.Add(CreateElement(102));
        ASSERT_TRUE(childA2);
        EXPECT_EQ(registry.Add(childA2.ref), RegistryResult::Success);

        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(registry.EndWindowScope(), RegistryResult::Success);

        // Window B
        auto wB = storage.Add(CreateElement(200, ElementFlags::IsWindow));
        ASSERT_TRUE(wB);
        EXPECT_EQ(registry.BeginWindowScope(id, wB.ref), RegistryResult::Success);

        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto childB1 = storage.Add(CreateElement(201));
        ASSERT_TRUE(childB1);
        EXPECT_EQ(registry.Add(childB1.ref), RegistryResult::Success);

        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(registry.EndWindowScope(), RegistryResult::Success);

        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        // Verify registry state
        EXPECT_EQ(registry.WindowCount(), 2u);

        EXPECT_EQ(registry.GetWindowRef(0), wA.ref);
        EXPECT_EQ(registry.GetWindowRef(1), wB.ref);
        EXPECT_EQ(registry.GetWindowRef(2), ElementRef::Invalid()); // out-of-range

        const auto* membersA = registry.GetWindowMembers(0);
        ASSERT_NE(membersA, nullptr);
        ASSERT_EQ(membersA->size(), 2u);
        EXPECT_EQ((*membersA)[0].ref, childA1.ref);
        EXPECT_EQ((*membersA)[1].ref, childA2.ref);

        const auto* membersB = registry.GetWindowMembers(1);
        ASSERT_NE(membersB, nullptr);
        ASSERT_EQ(membersB->size(), 1u);
        EXPECT_EQ((*membersB)[0].ref, childB1.ref);

        EXPECT_EQ(registry.GetWindowMembers(99), nullptr); // invalid index
        EXPECT_FALSE(registry.HasActiveWindow());
        EXPECT_EQ(registry.GetWindowStackDepth(), 0u);
    }

    TEST_F(WindowRegistryTest, NestedWindows_MembersAssignedToCorrectWindow)
    {
        ASSERT_EQ(storage.BeginConstruction(), ElementStorageResult::Success);
        registry.Clear();

        // Parent window
        auto wp = storage.Add(CreateElement(300, ElementFlags::IsWindow));
        ASSERT_TRUE(wp);
        EXPECT_EQ(registry.BeginWindowScope(id, wp.ref), RegistryResult::Success);

        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto pchild1 = storage.Add(CreateElement(301));
        ASSERT_TRUE(pchild1);
        EXPECT_EQ(registry.Add(pchild1.ref), RegistryResult::Success);

        // Nested window
        auto wn = storage.Add(CreateElement(400, ElementFlags::IsWindow));
        ASSERT_TRUE(wn);
        EXPECT_EQ(registry.BeginWindowScope(id, wn.ref), RegistryResult::Success);

        ASSERT_EQ(storage.BeginChildren(), ElementStorageResult::Success);
        auto nchild = storage.Add(CreateElement(401));
        ASSERT_TRUE(nchild);
        EXPECT_EQ(registry.Add(nchild.ref), RegistryResult::Success);
        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);

        // End nested window scope
        EXPECT_EQ(registry.EndWindowScope(), RegistryResult::Success);

        // Back to parent window scope: add another child to parent
        auto pchild2 = storage.Add(CreateElement(302));
        ASSERT_TRUE(pchild2);
        EXPECT_EQ(registry.Add(pchild2.ref), RegistryResult::Success);

        ASSERT_EQ(storage.EndChildren(), ElementStorageResult::Success);
        EXPECT_EQ(registry.EndWindowScope(), RegistryResult::Success);

        ASSERT_EQ(storage.EndConstruction(), ElementStorageResult::Success);

        // Verify windows -> parent first then nested
        EXPECT_EQ(registry.WindowCount(), 2u);
        const auto& wins = registry.Windows();
        // First window entry should be parent (in order of BeginWindowScope calls)
        EXPECT_EQ(wins[0].windowRef, wp.ref);
        EXPECT_EQ(wins[1].windowRef, wn.ref);

        // Parent members must contain pchild1 and pchild2
        ASSERT_EQ(wins[0].members.size(), 2u);
        EXPECT_EQ(wins[0].members[0].ref, pchild1.ref);
        EXPECT_EQ(wins[0].members[1].ref, pchild2.ref);

        // Nested window members must contain only nchild
        ASSERT_EQ(wins[1].members.size(), 1u);
        EXPECT_EQ(wins[1].members[0].ref, nchild.ref);
    }
}