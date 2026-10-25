// UndoHistory: steps undo and redo in order, a push drops the undone steps, and steps
// with the same key merge until the merge is ended.

#include <gtest/gtest.h>

#include <gui_core/input/gui_undo_history.h>

namespace interstice
{
    namespace
    {
        // A step that sets `value` to `before` on undo and `after` on redo.
        UndoHistory::Step SetStep(int& value, int before, int after, uint64_t mergeKey = 0)
        {
            return { .description = "Set",
                     .undo = [&value, before] { value = before; },
                     .redo = [&value, after]  { value = after; },
                     .mergeKey = mergeKey };
        }

        TEST(UndoHistoryTest, UndoAndRedoWalkTheSteps)
        {
            UndoHistory history;
            int value = 0;
            EXPECT_FALSE(history.CanUndo());
            EXPECT_FALSE(history.CanRedo());

            value = 1; history.Push(SetStep(value, 0, 1)); history.EndMerge();
            value = 2; history.Push(SetStep(value, 1, 2)); history.EndMerge();
            EXPECT_EQ(history.UndoCount(), 2u);

            history.Undo();
            EXPECT_EQ(value, 1);
            history.Undo();
            EXPECT_EQ(value, 0);
            EXPECT_FALSE(history.CanUndo());
            history.Undo();   // nothing left: no-op
            EXPECT_EQ(value, 0);

            history.Redo();
            EXPECT_EQ(value, 1);
            history.Redo();
            EXPECT_EQ(value, 2);
            EXPECT_FALSE(history.CanRedo());
        }

        TEST(UndoHistoryTest, PushDropsTheUndoneSteps)
        {
            UndoHistory history;
            int value = 0;
            value = 1; history.Push(SetStep(value, 0, 1)); history.EndMerge();
            value = 2; history.Push(SetStep(value, 1, 2)); history.EndMerge();
            history.Undo();
            EXPECT_EQ(history.RedoCount(), 1u);

            value = 5; history.Push(SetStep(value, 1, 5));
            EXPECT_FALSE(history.CanRedo());
            EXPECT_EQ(history.UndoCount(), 2u);
            history.Undo();
            EXPECT_EQ(value, 1);
        }

        TEST(UndoHistoryTest, StepsWithTheSameKeyMergeUntilEndMerge)
        {
            UndoHistory history;
            int value = 0;
            value = 1; history.Push(SetStep(value, 0, 1, 7));
            value = 2; history.Push(SetStep(value, 1, 2, 7));
            EXPECT_EQ(history.UndoCount(), 1u) << "same key while merging: one step";

            history.EndMerge();
            value = 3; history.Push(SetStep(value, 2, 3, 7));
            EXPECT_EQ(history.UndoCount(), 2u) << "EndMerge starts a step of its own";

            history.Undo();
            EXPECT_EQ(value, 2);
            history.Undo();
            EXPECT_EQ(value, 0) << "the merged step undoes to the first one's state";
            history.Redo();
            EXPECT_EQ(value, 2) << "and redoes to the latest one's";
        }

        TEST(UndoHistoryTest, DifferentKeysAndUnkeyedStepsDoNotMerge)
        {
            UndoHistory history;
            int value = 0;
            history.Push(SetStep(value, 0, 1, 7));
            history.Push(SetStep(value, 1, 2, 8));
            history.Push(SetStep(value, 2, 3));
            history.Push(SetStep(value, 3, 4));
            EXPECT_EQ(history.UndoCount(), 4u);
        }

        TEST(UndoHistoryTest, UndoEndsTheMerge)
        {
            UndoHistory history;
            int value = 0;
            history.Push(SetStep(value, 0, 1, 7));
            history.Push(SetStep(value, 1, 2, 7));
            history.Undo();
            history.Redo();
            history.Push(SetStep(value, 2, 3, 7));
            EXPECT_EQ(history.UndoCount(), 2u) << "a step after an undo / redo doesn't merge into it";
        }

        TEST(UndoHistoryTest, DescriptionsAndClear)
        {
            UndoHistory history;
            int value = 0;
            history.Push({ .description = "Move text", .undo = [&] { value = 0; }, .redo = [&] { value = 1; } });
            EXPECT_EQ(history.UndoDescription(), "Move text");
            EXPECT_EQ(history.RedoDescription(), "");
            history.Undo();
            EXPECT_EQ(history.RedoDescription(), "Move text");

            history.Clear();
            EXPECT_FALSE(history.CanUndo());
            EXPECT_FALSE(history.CanRedo());
        }
    }
}
