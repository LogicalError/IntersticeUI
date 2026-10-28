#include "gtest/gtest.h"
#include <gmock/gmock.h>

#include <gui_core/style/gui_style_manager.h>
#include <gui_core/style/gui_gradient_manager.h>

#include <gui_core/windows/gui_window_registry.h>

#include <gui_core/windows/gui_window_backend.h>

#include <gui_core/windows/gui_os_window_manager.h>
#include <gui_core/elements/gui_element_storage.h>
#include <gui_core/windows/gui_os_window_manager.h>

#include "mock_window_backend.h"

namespace tachyon
{
    using ::testing::Return;
    using ::testing::_;
    using ::testing::NiceMock;
    using GUITest::MockWindowBackend;

    class OSWindowManagerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            mockBackend = new NiceMock<MockWindowBackend>();
            manager = std::make_unique<OSWindowManager>(nullptr, mockBackend);

            ON_CALL(*mockBackend, CreateWindow(_, testing::A<int2>(), _, _))
                .WillByDefault([this](std::string_view, int2, WindowFlags, OSWindowHandle) {
                return reinterpret_cast<OSWindowHandle>(++handleCounter);
                    });
            ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
                .WillByDefault([this](std::string_view, IntRect, WindowFlags, OSWindowHandle) {
                return reinterpret_cast<OSWindowHandle>(++handleCounter);
                    });

            ON_CALL(*mockBackend, IsWindow(_))
                .WillByDefault(Return(true));

            ON_CALL(*mockBackend, GetWindowRect(_))
                .WillByDefault([](OSWindowHandle) {
                return IntRect{ 0, 0, 800, 600 };
                    });
        
            ON_CALL(*mockBackend, GetMonitorFromWindow(_))
                .WillByDefault(Return(0));
        
            ON_CALL(*mockBackend, GetMonitors())
                .WillByDefault([]() {
                    std::vector<MonitorInfo> monitors;
                    MonitorInfo primary {
                        .rect = IntRect{ 0, 0, 1920, 1080 },
                        .workArea = IntRect{ 0, 0, 1920, 1040 },
                        .isPrimary = true
                    };
                    monitors.push_back(primary);
                    return monitors;
                });
        }

        void TearDown() override
        {
            manager.reset();
            delete mockBackend;
            mockBackend = nullptr;
        }

        ID CreateTestID(uint32_t value)
        {
            ID id;
            id.value.integer = value;
            return id;
        }

        MockWindowBackend* mockBackend;
        std::unique_ptr<OSWindowManager> manager;
        uint64_t handleCounter = 0;
    };

    // ============================================================================
    // Window Creation Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, CreateWindow_Success)
    {
        ID id = CreateTestID(1);

        manager->BeginFrame();
        auto result = manager->GetOrCreateWindow(id, "Test Window", IntRect{0, 0, 800, 600}, nullptr);

        EXPECT_EQ(result, AddWindowResult::SuccessCreate);
        EXPECT_FALSE(manager->IsNotAlive(id));
    }

    TEST_F(OSWindowManagerTest, CreateWindow_InvalidID)
    {
        manager->BeginFrame();
        auto result = manager->GetOrCreateWindow(ID::Invalid(), "Test", IntRect{0, 0, 800, 600}, nullptr);

        EXPECT_EQ(result, AddWindowResult::InvalidID);
    }

    TEST_F(OSWindowManagerTest, CreateWindow_FailedOSCreation)
    {
        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(nullptr));

        ID id = CreateTestID(1);

        manager->BeginFrame();
        auto result = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);

        EXPECT_EQ(result, AddWindowResult::FailedToCreateOSWindow);
    }

    TEST_F(OSWindowManagerTest, GetOrCreateWindow_RecyclesExisting)
    {
        EXPECT_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _)).Times(1);
        ID id = CreateTestID(1);

        manager->BeginFrame();
        auto result1 = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        auto result2 = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);

        EXPECT_EQ(manager->WindowCount(), 1);
        EXPECT_EQ(result1, AddWindowResult::SuccessCreate);
        EXPECT_EQ(result2, AddWindowResult::SuccessRecycle);
    }

    TEST_F(OSWindowManagerTest, CreateMultipleWindows)
    {
        ID id1 = CreateTestID(1);
        ID id2 = CreateTestID(2);
        ID id3 = CreateTestID(3);

        manager->BeginFrame();
        manager->GetOrCreateWindow(id1, "Window 1", IntRect{0, 0, 800, 600}, nullptr);
        manager->GetOrCreateWindow(id2, "Window 2", IntRect{0, 0, 640, 480}, nullptr);
        manager->GetOrCreateWindow(id3, "Window 3", IntRect{0, 0, 1024, 768}, nullptr);

        EXPECT_FALSE(manager->IsNotAlive(id1));
        EXPECT_FALSE(manager->IsNotAlive(id2));
        EXPECT_FALSE(manager->IsNotAlive(id3));
        EXPECT_EQ(manager->WindowCount(), 3);
    }

    // ============================================================================
    // IsNotAlive Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, IsNotAlive_InvalidID)
    {
        EXPECT_TRUE(manager->IsNotAlive(ID::Invalid()));
    }

    TEST_F(OSWindowManagerTest, IsNotAlive_BadID)
    {
        EXPECT_TRUE(manager->IsNotAlive(ID{ .value{ .integer = 1 } }));
    }

    // ============================================================================
    // Parent-Child Relationship Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, CreateChildWindow)
    {
        ID parentId = CreateTestID(1);
        ID childId = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600}, nullptr);
        auto result = manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);

        EXPECT_EQ(result, AddWindowResult::SuccessCreate);
        EXPECT_FALSE(manager->IsNotAlive(childId));
    }

    TEST_F(OSWindowManagerTest, CloseParent_ClosesChildWithClosePolicy)
    {
        ID parentId = CreateTestID(1);
        ID childId = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600}, nullptr);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);

        manager->RequestClose(parentId);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(childId));
        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, CloseParent_DetachesChildWithDetachPolicy)
    {
        ID parentId = CreateTestID(1);
        ID childId = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600}, nullptr);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags, parentId);

        manager->RequestClose(parentId);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_FALSE(manager->IsNotAlive(childId));
        EXPECT_EQ(manager->WindowCount(), 1);
    }

    TEST_F(OSWindowManagerTest, CloseParent_WithAlreadyClosedChild)
    {
        ID parentId = CreateTestID(1);
        ID childId = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600}, nullptr);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags, parentId);

        manager->RequestClose(childId);
        manager->RequestClose(parentId);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(childId));
        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, MultipleChildren_MixedPolicies)
    {
        ID parentId = CreateTestID(1);
        ID child1Id = CreateTestID(2);
        ID child2Id = CreateTestID(3);
        ID child3Id = CreateTestID(4);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600}, nullptr);
        manager->GetOrCreateWindow(child1Id, "Child1", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child2Id, "Child2", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags, parentId);
        manager->GetOrCreateWindow(child3Id, "Child3", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);

        manager->RequestClose(parentId);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(child1Id));
        EXPECT_FALSE(manager->IsNotAlive(child2Id));  // Detached
        EXPECT_TRUE(manager->IsNotAlive(child3Id));
    }

    TEST_F(OSWindowManagerTest, NestedChildren_ClosePolicy)
    {
        ID grandparentId = CreateTestID(1);
        ID parentId = CreateTestID(2);
        ID childId = CreateTestID(3);

        manager->BeginFrame();
        manager->GetOrCreateWindow(grandparentId, "Grandparent", IntRect{0, 0, 800, 600}, nullptr);
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, grandparentId);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 200, 150}, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);

        manager->RequestClose(grandparentId);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(grandparentId));
        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(childId));
    }

    // ============================================================================
    // Window Lifecycle Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, BeginFrame_DetectsOSClose)
    {
        ID id = CreateTestID(1);
        OSWindowHandle handle = reinterpret_cast<OSWindowHandle>(1);

        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(handle));

        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        manager->EndFrame();

        // Simulate OS closing the window
        ON_CALL(*mockBackend, IsWindow(handle))
            .WillByDefault(Return(false));

        manager->BeginFrame();
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(id));
    }

    TEST_F(OSWindowManagerTest, EndFrame_DestroysClosingWindows)
    {
        ID id = CreateTestID(1);
        OSWindowHandle handle = reinterpret_cast<OSWindowHandle>(1);

        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(handle));

        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);

        EXPECT_CALL(*mockBackend, DestroyWindow(handle))
            .Times(1);

        manager->RequestClose(id);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(id));
    }

    TEST_F(OSWindowManagerTest, MultipleFrames_PersistsAliveWindows)
    {
        ID id = CreateTestID(1);

        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        manager->EndFrame();

        for (int i = 0; i < 10; ++i)
        {
            manager->BeginFrame();
            EXPECT_FALSE(manager->IsNotAlive(id));
            manager->EndFrame();
        }

        EXPECT_FALSE(manager->IsNotAlive(id));
    }

    // ============================================================================
    // Window Query Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, GetWindowRect)
    {
        ID id = CreateTestID(1);

        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr, WindowFlags::UserControlled | WindowFlags::CloseWithParent, ID::Invalid(), -1);

        IntRect rect = manager->GetWindowRect(id);

        EXPECT_EQ(rect.x, 0);
        EXPECT_EQ(rect.y, 0);
        EXPECT_EQ(rect.w, 800);
        EXPECT_EQ(rect.h, 600);

        manager->EndFrame();

        // simulate window being moved by user
        IntRect movedRect{ 10, 20, 800, 600 };
        ON_CALL(*mockBackend, GetWindowRect(_))
            .WillByDefault(Return(movedRect));

        manager->BeginFrame();
        manager->EndFrame();

        rect = manager->GetWindowRect(id);

        EXPECT_EQ(rect.x, movedRect.x);
        EXPECT_EQ(rect.y, movedRect.y);
        EXPECT_EQ(rect.w, movedRect.w);
        EXPECT_EQ(rect.h, movedRect.h);
    }

    TEST_F(OSWindowManagerTest, GetWindowRect_InvalidID)
    {
        ID id = CreateTestID(999);

        IntRect rect = manager->GetWindowRect(id);

        EXPECT_EQ(rect.x, 0);
        EXPECT_EQ(rect.y, 0);
        EXPECT_EQ(rect.w, 0);
        EXPECT_EQ(rect.h, 0);
    }

    TEST_F(OSWindowManagerTest, BringToFront)
    {
        ID id = CreateTestID(1);
        OSWindowHandle handle = reinterpret_cast<OSWindowHandle>(1);

        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(handle));

        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);

        EXPECT_CALL(*mockBackend, BringToFront(handle))
            .Times(1);

        manager->BringToFront(id);
    }

    TEST_F(OSWindowManagerTest, AllWindows)
    {
        ID id1 = CreateTestID(1);
        ID id2 = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(id1, "Window 1", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(id2, "Window 2", IntRect{0, 0, 640, 480 }, nullptr);

        const auto& windows = manager->AllWindows();

        EXPECT_EQ(windows.size(), 2);
    }

    // ============================================================================
    // Edge Cases and Stress Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, RemoveMiddleWindow_UpdatesIndices)
    {
        ID id1 = CreateTestID(1);
        ID id2 = CreateTestID(2);
        ID id3 = CreateTestID(3);

        manager->BeginFrame();
        manager->GetOrCreateWindow(id1, "Window 1", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(id2, "Window 2", IntRect{0, 0, 640, 480 }, nullptr);
        manager->GetOrCreateWindow(id3, "Window 3", IntRect{0, 0, 1024, 768 }, nullptr);

        manager->RequestClose(id2);
        manager->EndFrame();

        EXPECT_FALSE(manager->IsNotAlive(id1));
        EXPECT_TRUE(manager->IsNotAlive(id2));
        EXPECT_FALSE(manager->IsNotAlive(id3));
        EXPECT_EQ(manager->WindowCount(), 2);
    }

    TEST_F(OSWindowManagerTest, CloseAllWindows)
    {
        ID id1 = CreateTestID(1);
        ID id2 = CreateTestID(2);
        ID id3 = CreateTestID(3);

        manager->BeginFrame();
        manager->GetOrCreateWindow(id1, "Window 1", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(id2, "Window 2", IntRect{0, 0, 640, 480 }, nullptr);
        manager->GetOrCreateWindow(id3, "Window 3", IntRect{0, 0, 1024, 768 }, nullptr);

        manager->RequestClose(id1);
        manager->RequestClose(id2);
        manager->RequestClose(id3);
        manager->EndFrame();

        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, CreateAndDestroyManyWindows)
    {
        manager->BeginFrame();
        for (uint32_t i = 1; i <= 100; ++i)
        {
            ID id = CreateTestID(i);
            manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600 }, nullptr);
        }

        EXPECT_EQ(manager->WindowCount(), 100);

        for (uint32_t i = 1; i <= 100; ++i)
        {
            ID id = CreateTestID(i);
            manager->RequestClose(id);
        }

        manager->EndFrame();

        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, DeepHierarchy)
    {
        const int depth = 10;
        ID ids[depth]{};

        manager->BeginFrame();
        for (int i = 0; i < depth; ++i)
        {
            ids[i] = CreateTestID(i + 1);
            ID parentId = (i == 0) ? ID::Invalid() : ids[i - 1];
            manager->GetOrCreateWindow(ids[i], "Window", IntRect{0, 0, 800, 600 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        }

        manager->RequestClose(ids[0]);
        manager->EndFrame();

        for (int i = 0; i < depth; ++i)
        {
            EXPECT_TRUE(manager->IsNotAlive(ids[i]));
        }
    }

    TEST_F(OSWindowManagerTest, CloseChild_DoesNotAffectParent)
    {
        ID parentId = CreateTestID(1);
        ID childId = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);

        manager->RequestClose(childId);
        manager->EndFrame();

        EXPECT_FALSE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(childId));
    }

    // ============================================================================
    // Frame Lifecycle Guard Tests
    //
    // Misuse is reported (logged once) and returns its error; the manager stays usable.
    // ============================================================================

    TEST_F(OSWindowManagerTest, CreateWindow_CalledBeforeBeginFrame) 
    {
        ID id = CreateTestID(1);
        auto result = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600 }, nullptr);
    
        EXPECT_EQ(result, AddWindowResult::CalledOutsideOfFrame);
        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, CreateWindow_CalledAfterEndFrame) 
    {
        ID id = CreateTestID(1);
    
        manager->BeginFrame();
        manager->EndFrame();

        auto result = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600 }, nullptr);
    
        EXPECT_EQ(result, AddWindowResult::CalledOutsideOfFrame);
        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, RequestClose_CalledBeforeBeginFrame) 
    {
        ID id = CreateTestID(1);
    
        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600 }, nullptr);
        manager->EndFrame();

        auto result = manager->RequestClose(id);
    
        EXPECT_EQ(result, RequestCloseResult::CalledOutsideFrame);
    }

    TEST_F(OSWindowManagerTest, DoubleBeginFrame_WithoutEndFrame) 
    {
        EXPECT_TRUE(manager->BeginFrame());
        EXPECT_FALSE(manager->BeginFrame());
        EXPECT_TRUE(manager->EndFrame());
        EXPECT_TRUE(manager->BeginFrame()) << "the next frame works";
        EXPECT_TRUE(manager->EndFrame());
    }

    TEST_F(OSWindowManagerTest, DoubleEndFrame_AfterBeginFrame) 
    {
        EXPECT_TRUE(manager->BeginFrame());
        EXPECT_TRUE(manager->EndFrame());
        EXPECT_FALSE(manager->EndFrame());
    }

    TEST_F(OSWindowManagerTest, EndFrame_WithoutBeginFrame) 
    {
        EXPECT_FALSE(manager->EndFrame());
    }

    // ============================================================================
    // Error Result Handling Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, RequestClose_AlreadyClosedWindow) 
    {
        ID id = CreateTestID(1);
    
        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600 }, nullptr);
    
        auto result1 = manager->RequestClose(id);
        auto result2 = manager->RequestClose(id);
    
        EXPECT_EQ(result1, RequestCloseResult::Success);
        EXPECT_EQ(result2, RequestCloseResult::WindowAlreadyClosed);
    }

    TEST_F(OSWindowManagerTest, RequestClose_NonExistentWindow) 
    {
        ID id = CreateTestID(999);
    
        manager->BeginFrame();
        auto result = manager->RequestClose(id);
    
        EXPECT_EQ(result, RequestCloseResult::IndexOutOfBounds);
    }

    TEST_F(OSWindowManagerTest, RequestClose_InvalidID) 
    {
        manager->BeginFrame();
        auto result = manager->RequestClose(ID::Invalid());
    
        EXPECT_EQ(result, RequestCloseResult::IndexOutOfBounds);
    }

    // ============================================================================
    // Complex Sibling Scenarios
    // ============================================================================

    TEST_F(OSWindowManagerTest, CloseSibling_FirstOfMany) 
    {
        ID parentId = CreateTestID(1);
        ID child1Id = CreateTestID(2);
        ID child2Id = CreateTestID(3);
        ID child3Id = CreateTestID(4);
        ID child4Id = CreateTestID(5);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(child1Id, "Child1", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child2Id, "Child2", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child3Id, "Child3", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child4Id, "Child4", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
    
        manager->RequestClose(child1Id);
        manager->EndFrame();
    
        EXPECT_FALSE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(child1Id));
        EXPECT_FALSE(manager->IsNotAlive(child2Id));
        EXPECT_FALSE(manager->IsNotAlive(child3Id));
        EXPECT_FALSE(manager->IsNotAlive(child4Id));
        EXPECT_EQ(manager->WindowCount(), 4);
    }

    TEST_F(OSWindowManagerTest, CloseSibling_MiddleOfMany) 
    {
        ID parentId = CreateTestID(1);
        ID child1Id = CreateTestID(2);
        ID child2Id = CreateTestID(3);
        ID child3Id = CreateTestID(4);
        ID child4Id = CreateTestID(5);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(child1Id, "Child1", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child2Id, "Child2", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child3Id, "Child3", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child4Id, "Child4", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
    
        manager->RequestClose(child2Id);
        manager->EndFrame();
    
        EXPECT_FALSE(manager->IsNotAlive(parentId));
        EXPECT_FALSE(manager->IsNotAlive(child1Id));
        EXPECT_TRUE(manager->IsNotAlive(child2Id));
        EXPECT_FALSE(manager->IsNotAlive(child3Id));
        EXPECT_FALSE(manager->IsNotAlive(child4Id));
        EXPECT_EQ(manager->WindowCount(), 4);
    }

    TEST_F(OSWindowManagerTest, CloseSibling_LastOfMany) 
    {
        ID parentId = CreateTestID(1);
        ID child1Id = CreateTestID(2);
        ID child2Id = CreateTestID(3);
        ID child3Id = CreateTestID(4);
        ID child4Id = CreateTestID(5);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(child1Id, "Child1", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child2Id, "Child2", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child3Id, "Child3", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child4Id, "Child4", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
    
        manager->RequestClose(child4Id);
        manager->EndFrame();
    
        EXPECT_FALSE(manager->IsNotAlive(parentId));
        EXPECT_FALSE(manager->IsNotAlive(child1Id));
        EXPECT_FALSE(manager->IsNotAlive(child2Id));
        EXPECT_FALSE(manager->IsNotAlive(child3Id));
        EXPECT_TRUE(manager->IsNotAlive(child4Id));
        EXPECT_EQ(manager->WindowCount(), 4);
    }

    TEST_F(OSWindowManagerTest, CloseMultipleSiblings_InSequence) 
    {
        ID parentId = CreateTestID(1);
        ID child1Id = CreateTestID(2);
        ID child2Id = CreateTestID(3);
        ID child3Id = CreateTestID(4);
        ID child4Id = CreateTestID(5);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(child1Id, "Child1", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child2Id, "Child2", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child3Id, "Child3", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
        manager->GetOrCreateWindow(child4Id, "Child4", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
    
        manager->RequestClose(child2Id);
        manager->RequestClose(child4Id);
        manager->EndFrame();
    
        EXPECT_FALSE(manager->IsNotAlive(parentId));
        EXPECT_FALSE(manager->IsNotAlive(child1Id));
        EXPECT_TRUE(manager->IsNotAlive(child2Id));
        EXPECT_FALSE(manager->IsNotAlive(child3Id));
        EXPECT_TRUE(manager->IsNotAlive(child4Id));
        EXPECT_EQ(manager->WindowCount(), 3);
    }

    TEST_F(OSWindowManagerTest, MultipleSiblings_AllDetachPolicy) 
    {
        ID parentId = CreateTestID(1);
        ID child1Id = CreateTestID(2);
        ID child2Id = CreateTestID(3);
        ID child3Id = CreateTestID(4);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(child1Id, "Child1", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, parentId);
        manager->GetOrCreateWindow(child2Id, "Child2", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, parentId);
        manager->GetOrCreateWindow(child3Id, "Child3", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, parentId);
    
        manager->RequestClose(parentId);
        manager->EndFrame();
    
        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_FALSE(manager->IsNotAlive(child1Id));
        EXPECT_FALSE(manager->IsNotAlive(child2Id));
        EXPECT_FALSE(manager->IsNotAlive(child3Id));
        EXPECT_EQ(manager->WindowCount(), 3);
    }

    TEST_F(OSWindowManagerTest, OnlyChild_ClosePolicy) 
    {
        ID parentId = CreateTestID(1);
        ID childId = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
    
        manager->RequestClose(childId);
        manager->EndFrame();
    
        EXPECT_FALSE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(childId));
        EXPECT_EQ(manager->WindowCount(), 1);
    }

    // ============================================================================
    // Detach Policy Complex Scenarios
    // ============================================================================

    TEST_F(OSWindowManagerTest, DetachChild_ThenCloseDetachedChild_SameFrame) 
    {
        ID parentId = CreateTestID(1);
        ID childId = CreateTestID(2);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, parentId);
    
        manager->RequestClose(parentId);
        manager->RequestClose(childId);
        manager->EndFrame();
    
        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(childId));
        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, DetachedChild_BecomesParent) 
    {
        ID grandparentId = CreateTestID(1);
        ID parentId = CreateTestID(2);
        ID childId = CreateTestID(3);

        manager->BeginFrame();
        manager->GetOrCreateWindow(grandparentId, "Grandparent", IntRect{0, 0, 800, 600}, nullptr);
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 400, 300}, nullptr, WindowFlags::DefaultFlags, grandparentId);
        manager->GetOrCreateWindow(childId, "Child", IntRect{0, 0, 200, 150}, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentId);
    
        manager->RequestClose(grandparentId);
        manager->EndFrame();
    
        EXPECT_TRUE(manager->IsNotAlive(grandparentId));
        EXPECT_FALSE(manager->IsNotAlive(parentId));
        EXPECT_FALSE(manager->IsNotAlive(childId));
        EXPECT_EQ(manager->WindowCount(), 2);
    
        // Now close the detached parent
        manager->BeginFrame();
        manager->RequestClose(parentId);
        manager->EndFrame();
    
        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_TRUE(manager->IsNotAlive(childId));
        EXPECT_EQ(manager->WindowCount(), 0);
    }

    TEST_F(OSWindowManagerTest, MultipleDetachedChildren_FromSameParent) 
    {
        ID parentId = CreateTestID(1);
        ID child1Id = CreateTestID(2);
        ID child2Id = CreateTestID(3);
        ID child3Id = CreateTestID(4);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentId, "Parent", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(child1Id, "Child1", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, parentId);
        manager->GetOrCreateWindow(child2Id, "Child2", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, parentId);
        manager->GetOrCreateWindow(child3Id, "Child3", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, parentId);
    
        manager->RequestClose(parentId);
        manager->EndFrame();
    
        EXPECT_TRUE(manager->IsNotAlive(parentId));
        EXPECT_FALSE(manager->IsNotAlive(child1Id));
        EXPECT_FALSE(manager->IsNotAlive(child2Id));
        EXPECT_FALSE(manager->IsNotAlive(child3Id));
        EXPECT_EQ(manager->WindowCount(), 3);
    }

    TEST_F(OSWindowManagerTest, DetachChild_NestedHierarchy_Mixed) 
    {
        ID root = CreateTestID(1);
        ID level1_detach = CreateTestID(2);
        ID level2_close = CreateTestID(3);
        ID level3_detach = CreateTestID(4);

        manager->BeginFrame();
        manager->GetOrCreateWindow(root, "Root", IntRect{0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(level1_detach, "L1", IntRect{0, 0, 400, 300 }, nullptr, WindowFlags::DefaultFlags, root);
        manager->GetOrCreateWindow(level2_close, "L2", IntRect{0, 0, 200, 150 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, level1_detach);
        manager->GetOrCreateWindow(level3_detach, "L3", IntRect{0, 0, 100, 75 }, nullptr, WindowFlags::DefaultFlags, level2_close);
    
        manager->RequestClose(root);
        manager->EndFrame();
    
        // When root closes, level1_detach (with Detach policy) becomes top-level
        // Its children (level2_close and level3_detach) stay attached to it
        EXPECT_TRUE(manager->IsNotAlive(root));
        EXPECT_FALSE(manager->IsNotAlive(level1_detach));
        EXPECT_FALSE(manager->IsNotAlive(level2_close));
        EXPECT_FALSE(manager->IsNotAlive(level3_detach));
    }

    // ============================================================================
    // User Close Tests
    // ============================================================================

    TEST_F(OSWindowManagerTest, UserClose_PreventsAutomaticReopening)
    {
        ID id = CreateTestID(1);
        OSWindowHandle handle = reinterpret_cast<OSWindowHandle>(1);

        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(handle));

        // Create window in first frame
        manager->BeginFrame();
        auto result1 = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        EXPECT_EQ(result1, AddWindowResult::SuccessCreate);
        manager->EndFrame();

        // Simulate user manually closing the window (OS window handle becomes invalid)
        ON_CALL(*mockBackend, IsWindow(handle))
            .WillByDefault(Return(false));

        manager->BeginFrame();
        manager->EndFrame();

        // Window should be closed now
        EXPECT_TRUE(manager->IsNotAlive(id));

        // Try to recreate the window in next frame - should fail because user closed it
        manager->BeginFrame();
        auto result2 = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        EXPECT_EQ(result2, AddWindowResult::WindowUserClosed);
        EXPECT_TRUE(manager->IsNotAlive(id));
    }

    TEST_F(OSWindowManagerTest, UserClose_CanBeReopenedWithAllowReopen)
    {
        ID id = CreateTestID(1);
        OSWindowHandle handle = reinterpret_cast<OSWindowHandle>(1);

        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(handle));

        // Create window
        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        manager->EndFrame();

        // Simulate user closing the window
        ON_CALL(*mockBackend, IsWindow(handle))
            .WillByDefault(Return(false));

        manager->BeginFrame();
        manager->EndFrame();

        EXPECT_TRUE(manager->WasUserClosed(id));
        EXPECT_TRUE(manager->IsNotAlive(id));

        // Allow reopening
        manager->AllowReopenWindow(id);
        EXPECT_FALSE(manager->WasUserClosed(id));

        // Now window can be recreated
        OSWindowHandle newHandle = reinterpret_cast<OSWindowHandle>(2);
        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(newHandle));
        ON_CALL(*mockBackend, IsWindow(newHandle))
            .WillByDefault(Return(true));

        manager->BeginFrame();
        auto result = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        EXPECT_EQ(result, AddWindowResult::SuccessCreate);
        EXPECT_FALSE(manager->IsNotAlive(id));
    }

    TEST_F(OSWindowManagerTest, ProgrammaticClose_AllowsAutomaticReopening)
    {
        ID id = CreateTestID(1);
        OSWindowHandle handle = reinterpret_cast<OSWindowHandle>(1);

        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(handle));

        // Create window
        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        manager->EndFrame();

        // Programmatically close using RequestCloseAllowReopen
        manager->BeginFrame();
        manager->RequestCloseAllowReopen(id);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(id));
        EXPECT_FALSE(manager->WasUserClosed(id));

        // Window can be recreated automatically
        OSWindowHandle newHandle = reinterpret_cast<OSWindowHandle>(2);
        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(newHandle));
        ON_CALL(*mockBackend, IsWindow(newHandle))
            .WillByDefault(Return(true));

        manager->BeginFrame();
        auto result = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        EXPECT_EQ(result, AddWindowResult::SuccessCreate);
        EXPECT_FALSE(manager->IsNotAlive(id));
    }

    TEST_F(OSWindowManagerTest, UserClose_CanReopenAfterNotDeclaring)
    {
        ID id = CreateTestID(1);
        OSWindowHandle handle = reinterpret_cast<OSWindowHandle>(1);

        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(handle));

        // Create window
        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Test", IntRect { 0, 0, 800, 600 }, nullptr);
        manager->EndFrame();

        // Simulate user closing the window
        ON_CALL(*mockBackend, IsWindow(handle))
            .WillByDefault(Return(false));

        manager->BeginFrame();
        manager->EndFrame();

        EXPECT_TRUE(manager->WasUserClosed(id));
        EXPECT_TRUE(manager->IsNotAlive(id));

        // Try to create window immediately - should fail
        manager->BeginFrame();
        auto result1 = manager->GetOrCreateWindow(id, "Test", IntRect { 0, 0, 800, 600 }, nullptr);
        EXPECT_EQ(result1, AddWindowResult::WindowUserClosed);
        manager->EndFrame();

        tachyon::WindowRegistry registry;
        registry.Clear();

        // Frame where we DON'T declare the window - this should clear the user-closed flag
        manager->BeginFrame();

        manager->ReleaseUndeclaredWindows([&](ID declared) { return registry.Contains(declared); });
        manager->EndFrame();

        // The user-closed flag should now be cleared
        EXPECT_FALSE(manager->WasUserClosed(id));

        // Now window can be recreated
        OSWindowHandle newHandle = reinterpret_cast<OSWindowHandle>(2);
        ON_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _))
            .WillByDefault(Return(newHandle));
        ON_CALL(*mockBackend, IsWindow(newHandle))
            .WillByDefault(Return(true));

        manager->BeginFrame();
        auto result2 = manager->GetOrCreateWindow(id, "Test", IntRect{0, 0, 800, 600}, nullptr);
        EXPECT_EQ(result2, AddWindowResult::SuccessCreate);
        EXPECT_FALSE(manager->IsNotAlive(id));
        manager->EndFrame();
    }


    // ============================================================================
    // Recycling
    // ============================================================================

    // A popup auto-closed because another window took focus, but still declared by
    // the app that frame, is hidden for reuse rather than destroyed and recreated.
    TEST_F(OSWindowManagerTest, AutoClosedPopupIsHiddenNotDestroyed)
    {
        const ID mainId  = CreateTestID(1);
        const ID popupId = CreateTestID(2);
        const WindowFlags popupFlags = WindowFlags::DefaultFlags | WindowFlags::PopupMenu;

        manager->BeginFrame();
        manager->GetOrCreateWindow(mainId, "Main", IntRect{ 0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(popupId, "Popup", IntRect{ 10, 10, 100, 100 }, nullptr, popupFlags | WindowFlags::CloseWithParent, mainId);
        manager->EndFrame();

        ON_CALL(*mockBackend, GetActiveWindow()).WillByDefault(Return(reinterpret_cast<OSWindowHandle>(1)));
        ON_CALL(*mockBackend, GetFocusChangeCount()).WillByDefault(Return(1u));
        EXPECT_CALL(*mockBackend, DestroyWindow(_)).Times(0);
        EXPECT_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(), _, _)).Times(0);

        for (int frame = 0; frame < 2; ++frame)
        {
            manager->BeginFrame();
            manager->GetOrCreateWindow(mainId, "Main", IntRect{ 0, 0, 800, 600 }, nullptr);
            manager->GetOrCreateWindow(popupId, "Popup", IntRect{ 10, 10, 100, 100 }, nullptr, popupFlags | WindowFlags::CloseWithParent, mainId);
            manager->EndFrame();
        }
        EXPECT_EQ(manager->WindowCount(), 2);
    }

    // Popups are only checked against the active window after the focus moves, so
    // one that hasn't taken focus yet stays open.
    TEST_F(OSWindowManagerTest, PopupClosesOnlyWhenTheFocusMoves)
    {
        const ID mainId  = CreateTestID(1);
        const ID popupId = CreateTestID(2);
        const WindowFlags popupFlags = WindowFlags::DefaultFlags | WindowFlags::PopupMenu | WindowFlags::CloseWithParent;

        manager->BeginFrame();
        manager->GetOrCreateWindow(mainId, "Main", IntRect{ 0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(popupId, "Popup", IntRect{ 10, 10, 100, 100 }, nullptr, popupFlags, mainId);
        manager->EndFrame();

        // The main window stays active, but nothing moved the focus.
        ON_CALL(*mockBackend, GetActiveWindow()).WillByDefault(Return(manager->GetWindowHandle(mainId)));
        manager->BeginFrame();
        EXPECT_EQ(manager->GetWindowState(popupId), WindowState::Alive);
        manager->EndFrame();

        ON_CALL(*mockBackend, GetFocusChangeCount()).WillByDefault(Return(1u));
        manager->BeginFrame();
        EXPECT_EQ(manager->GetWindowState(popupId), WindowState::Hidden);
        manager->EndFrame();
    }

    // The window manager raises a focusable popup when it appears (a new one after it has
    // been drawn a few frames); not-focusable ones (tooltips) and other windows are left
    // where they are.
    TEST_F(OSWindowManagerTest, OpeningAPopupBringsItToFront)
    {
        const ID mainId    = CreateTestID(1);
        const ID popupId   = CreateTestID(2);
        const ID tooltipId = CreateTestID(3);

        manager->BeginFrame();
        manager->GetOrCreateWindow(mainId, "Main", IntRect{ 0, 0, 800, 600 }, nullptr);
        const OSWindowHandle popupHandle = reinterpret_cast<OSWindowHandle>(handleCounter + 1);
        EXPECT_CALL(*mockBackend, BringToFront(popupHandle)).Times(2);
        EXPECT_CALL(*mockBackend, BringToFront(testing::Ne(popupHandle))).Times(0);
        manager->GetOrCreateWindow(popupId, "Popup", IntRect{ 10, 10, 100, 100 }, nullptr,
                                   WindowFlags::DefaultFlags | WindowFlags::PopupMenu, mainId);
        manager->GetOrCreateWindow(tooltipId, "Tooltip", IntRect{ 10, 10, 50, 20 }, nullptr,
                                   WindowFlags::DefaultFlags | WindowFlags::PopupMenu | WindowFlags::NotFocusable, mainId);
        manager->EndFrame();

        // It appears within a few frames, raised then, and isn't raised again while open.
        for (int i = 0; i < 8; ++i)
        {
            manager->BeginFrame();
            manager->GetOrCreateWindow(popupId, "Popup", IntRect{ 10, 10, 100, 100 }, nullptr,
                                       WindowFlags::DefaultFlags | WindowFlags::PopupMenu, mainId);
            manager->EndFrame();
        }

        // Hidden, then shown again: raised again.
        manager->BeginFrame();
        manager->ReleaseUndeclaredWindows([&](ID id) { return id == mainId; });
        manager->EndFrame();
        EXPECT_EQ(manager->GetWindowState(popupId), WindowState::Hidden);
        manager->BeginFrame();
        manager->GetOrCreateWindow(popupId, "Popup", IntRect{ 10, 10, 100, 100 }, nullptr,
                                   WindowFlags::DefaultFlags | WindowFlags::PopupMenu, mainId);
        manager->EndFrame();
    }

    // A new popup is created hidden and shows once it has been drawn a few frames, so it
    // never appears empty; reopening it shows it at once. (The mock can't cloak, so this is
    // the hidden path.)
    TEST_F(OSWindowManagerTest, ANewPopupShowsOnceDrawn)
    {
        const ID mainId  = CreateTestID(1);
        const ID popupId = CreateTestID(2);
        auto declare = [&] {
            manager->GetOrCreateWindow(popupId, "Popup", IntRect{ 10, 10, 100, 100 }, nullptr,
                                       WindowFlags::DefaultFlags | WindowFlags::PopupMenu, mainId);
        };

        manager->BeginFrame();
        manager->GetOrCreateWindow(mainId, "Main", IntRect{ 0, 0, 800, 600 }, nullptr);
        const OSWindowHandle popupHandle = reinterpret_cast<OSWindowHandle>(handleCounter + 1);
        EXPECT_CALL(*mockBackend, CreateWindow(_, testing::A<IntRect>(),
                                               testing::Truly([](WindowFlags f) { return (f & WindowFlags::Hidden) != WindowFlags::None; }), _));
        declare();
        manager->EndFrame();
        testing::Mock::VerifyAndClearExpectations(mockBackend);

        int framesUntilShown = 1;
        bool shown = false;
        ON_CALL(*mockBackend, SetWindowVisible(popupHandle, true)).WillByDefault([&] { shown = true; });
        for (; framesUntilShown < 10 && !shown; ++framesUntilShown)
        {
            manager->BeginFrame();
            declare();
            manager->EndFrame();
        }
        EXPECT_TRUE(shown);
        EXPECT_GT(framesUntilShown, 2) << "drawn more than once first";

        // Hidden, then declared again: shown in that frame.
        manager->BeginFrame();
        manager->ReleaseUndeclaredWindows([&](ID id) { return id == mainId; });
        manager->EndFrame();
        shown = false;
        manager->BeginFrame();
        declare();
        EXPECT_TRUE(shown) << "reopening shows it at once";
        manager->EndFrame();
    }

    // Re-declaring a window under another parent moves it to that parent's child
    // list, so closing the new parent applies the child's policy to it.
    TEST_F(OSWindowManagerTest, RecycledWindowWithNewParentIsRelinked)
    {
        const ID parentA = CreateTestID(1);
        const ID parentB = CreateTestID(2);
        const ID child   = CreateTestID(3);

        manager->BeginFrame();
        manager->GetOrCreateWindow(parentA, "A", IntRect{ 0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(parentB, "B", IntRect{ 0, 0, 800, 600 }, nullptr);
        manager->GetOrCreateWindow(child, "C", IntRect{ 0, 0, 200, 100 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentA);
        manager->EndFrame();

        manager->BeginFrame();
        manager->GetOrCreateWindow(child, "C", IntRect{ 0, 0, 200, 100 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, parentB);
        EXPECT_EQ(manager->RequestClose(parentB), RequestCloseResult::Success);
        manager->EndFrame();

        EXPECT_TRUE(manager->IsNotAlive(child)) << "closed with its new parent";
        EXPECT_FALSE(manager->IsNotAlive(parentA));

        // The old parent no longer lists it: closing A works and leaves one window less.
        manager->BeginFrame();
        EXPECT_EQ(manager->RequestClose(parentA), RequestCloseResult::Success);
        manager->EndFrame();
        EXPECT_EQ(manager->WindowCount(), 0);
    }

    // With no monitors reported (headless session, display unplugged) centring
    // falls back to an empty work area instead of reading past the list.
    TEST_F(OSWindowManagerTest, CenterInMonitorWithoutMonitors)
    {
        ON_CALL(*mockBackend, GetMonitors()).WillByDefault([] { return std::vector<MonitorInfo>{}; });
        OSWindowManager noMonitors(nullptr, mockBackend);

        const IntRect rect = noMonitors.CenterInMonitor(0, 200, 100);
        EXPECT_EQ(rect.w, 200);
        EXPECT_EQ(rect.h, 100);
    }

    // The IME candidate window is placed in window coordinates, and only when
    // the caret actually moves.
    TEST_F(OSWindowManagerTest, TextInputAreaIsWindowRelativeAndSentOnChange)
    {
        const ID id = CreateTestID(1);
        ON_CALL(*mockBackend, GetWindowRect(_)).WillByDefault(Return(IntRect{ 100, 50, 800, 600 }));
        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Main", IntRect{ 100, 50, 800, 600 }, nullptr);
        manager->EndFrame();
        manager->BeginFrame();

        EXPECT_CALL(*mockBackend, SetTextInputArea(_, IntRect{ 20, 30, 1, 16 })).Times(1);
        EXPECT_CALL(*mockBackend, SetTextInputArea(_, IntRect{ 28, 30, 1, 16 })).Times(1);
        manager->SetTextInputArea(id, IntRect{ 120, 80, 1, 16 });
        manager->SetTextInputArea(id, IntRect{ 120, 80, 1, 16 });
        manager->SetTextInputArea(id, IntRect{ 128, 80, 1, 16 });
        manager->EndFrame();
    }

    // A window reports its display scale (pixels per point), refreshed every frame
    // so a move to a monitor with a different scale is picked up.
    TEST_F(OSWindowManagerTest, WindowDisplayScaleFollowsTheBackend)
    {
        const ID id = CreateTestID(1);
        float scale = 1.5f;
        ON_CALL(*mockBackend, GetWindowDisplayScale(_)).WillByDefault([&](OSWindowHandle) { return scale; });

        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Main", IntRect{ 0, 0, 800, 600 }, nullptr);
        EXPECT_FLOAT_EQ(manager->GetWindowDisplayScale(id), 1.5f) << "known as soon as the window exists";
        manager->EndFrame();

        scale = 2.0f; // dragged onto a 200% monitor
        manager->BeginFrame();
        EXPECT_FLOAT_EQ(manager->GetWindowDisplayScale(id), 2.0f);
        manager->EndFrame();

        EXPECT_FLOAT_EQ(manager->GetWindowDisplayScale(CreateTestID(99)), 1.0f) << "unknown window";
    }

    TEST_F(OSWindowManagerTest, UnknownDisplayScaleFallsBackToOne)
    {
        const ID id = CreateTestID(1);
        ON_CALL(*mockBackend, GetWindowDisplayScale(_)).WillByDefault(Return(0.0f));

        manager->BeginFrame();
        manager->GetOrCreateWindow(id, "Main", IntRect{ 0, 0, 800, 600 }, nullptr);
        manager->EndFrame();
        EXPECT_FLOAT_EQ(manager->GetWindowDisplayScale(id), 1.0f);
    }

    // The UI scale follows the main window (the first top-level one): its monitor's
    // scale. Before any window exists, the primary monitor's.
    TEST_F(OSWindowManagerTest, MainDisplayScaleFollowsTheFirstTopLevelWindow)
    {
        ON_CALL(*mockBackend, GetMonitors()).WillByDefault([] {
            MonitorInfo primary{ .rect = IntRect{ 0, 0, 1920, 1080 }, .workArea = IntRect{ 0, 0, 1920, 1040 },
                                 .isPrimary = true, .contentScale = 1.25f };
            return std::vector<MonitorInfo>{ primary };
        });
        OSWindowManager wm(nullptr, mockBackend);
        EXPECT_FLOAT_EQ(wm.MainDisplayScale(), 1.25f) << "no window yet: the primary monitor";

        std::unordered_map<OSWindowHandle, float> scales;
        ON_CALL(*mockBackend, GetWindowDisplayScale(_)).WillByDefault([&](OSWindowHandle h) { return scales[h]; });
        const ID mainId = CreateTestID(1), childId = CreateTestID(2), otherId = CreateTestID(3);

        wm.BeginFrame();
        wm.GetOrCreateWindow(mainId, "Main", IntRect{ 0, 0, 800, 600 }, nullptr, WindowFlags::DefaultFlags);
        wm.GetOrCreateWindow(childId, "Popup", IntRect{ 0, 0, 100, 100 }, nullptr, WindowFlags::DefaultFlags | WindowFlags::CloseWithParent, mainId);
        wm.GetOrCreateWindow(otherId, "Other", IntRect{ 0, 0, 400, 300 }, nullptr);
        wm.EndFrame();
        scales[reinterpret_cast<OSWindowHandle>(handleCounter - 2)] = 2.0f;   // the main window's monitor
        scales[reinterpret_cast<OSWindowHandle>(handleCounter - 1)] = 3.0f;   // the child's
        scales[reinterpret_cast<OSWindowHandle>(handleCounter)]     = 1.5f;   // the other top-level

        wm.BeginFrame();
        EXPECT_FLOAT_EQ(wm.MainDisplayScale(), 2.0f);
        EXPECT_EQ(wm.RequestClose(mainId), RequestCloseResult::Success);
        wm.EndFrame();

        wm.BeginFrame();
        EXPECT_FLOAT_EQ(wm.MainDisplayScale(), 1.5f) << "the remaining top-level window takes over";
        wm.EndFrame();
    }

    // The frame clock paces to the main window's monitor; the primary monitor's
    // before any window exists.
    TEST_F(OSWindowManagerTest, MainRefreshRateFollowsTheMainWindowsMonitor)
    {
        ON_CALL(*mockBackend, GetMonitors()).WillByDefault([] {
            return std::vector<MonitorInfo>{
                MonitorInfo{ .rect = IntRect{ 0, 0, 1920, 1080 }, .isPrimary = true, .refreshRate = 60.0f },
                MonitorInfo{ .rect = IntRect{ 1920, 0, 2560, 1440 }, .refreshRate = 144.0f },
            };
        });
        OSWindowManager wm(nullptr, mockBackend);
        EXPECT_FLOAT_EQ(wm.MainRefreshRate(), 60.0f) << "no window yet: the primary monitor";

        ON_CALL(*mockBackend, GetMonitorFromWindow(_)).WillByDefault(Return(1));
        wm.BeginFrame();
        wm.GetOrCreateWindow(CreateTestID(1), "Main", IntRect{ 2000, 100, 800, 600 }, nullptr, WindowFlags::DefaultFlags);
        wm.EndFrame();

        wm.BeginFrame();
        EXPECT_FLOAT_EQ(wm.MainRefreshRate(), 144.0f);
        wm.EndFrame();
    }
}