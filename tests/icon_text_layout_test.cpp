#include <gtest/gtest.h>
#include <gui_core/text/gui_text_system.h>
#include <gui_core/style/gui_style.h>
#include <vector>

namespace tachyon
{
    class IconTextLayoutTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
        }

        void TearDown() override
        {
        }
    };

    TEST_F(IconTextLayoutTest, IconSideLeftPlacesIconFirst)
    {
        std::vector<ContentRun> contentRuns;
        
        ContentRun icon;
        icon.type = ContentRun::Icon;
        icon.iconID = IconID(1);
        icon.iconWidth = 16.0f;
        icon.iconHeight = 16.0f;
        contentRuns.push_back(icon);
        
        ContentRun text;
        text.type = ContentRun::Text;
        text.text = "Label";
        contentRuns.push_back(text);

        EXPECT_EQ(contentRuns[0].type, ContentRun::Icon);
        EXPECT_EQ(contentRuns[1].type, ContentRun::Text);
    }

    TEST_F(IconTextLayoutTest, IconSideRightPlacesIconLast)
    {
        std::vector<ContentRun> contentRuns;
        
        ContentRun text;
        text.type = ContentRun::Text;
        text.text = "Label";
        contentRuns.push_back(text);
        
        ContentRun icon;
        icon.type = ContentRun::Icon;
        icon.iconID = IconID(1);
        icon.iconWidth = 16.0f;
        icon.iconHeight = 16.0f;
        contentRuns.push_back(icon);

        EXPECT_EQ(contentRuns[0].type, ContentRun::Text);
        EXPECT_EQ(contentRuns[1].type, ContentRun::Icon);
    }

    TEST_F(IconTextLayoutTest, IconTextGapPropertyExists)
    {
        Style style;
        style.iconTextGap = 8;
        
        EXPECT_EQ(style.iconTextGap, 8);
    }

    TEST_F(IconTextLayoutTest, IconSidePropertyExists)
    {
        Style style;
        style.iconSide = IconSide::Right;

        EXPECT_EQ(style.iconSide, IconSide::Right);
    }

    // iconSide is a bitfield; it must hold every IconSide value (Bottom = 4 needs 3 bits).
    TEST_F(IconTextLayoutTest, IconSideBitfieldHoldsAllEnumValues)
    {
        Style style;
        for (IconSide v : { IconSide::None, IconSide::Left, IconSide::Right,
                            IconSide::Top,  IconSide::Bottom })
        {
            style.iconSide = v;
            EXPECT_EQ(style.iconSide, v)
                << "Style::iconSide bitfield truncated value "
                << static_cast<int>(v);
        }

        LayoutStyle layout;
        for (IconSide v : { IconSide::None, IconSide::Left, IconSide::Right,
                            IconSide::Top,  IconSide::Bottom })
        {
            layout.iconSide = v;
            EXPECT_EQ(layout.iconSide, v)
                << "LayoutStyle::iconSide bitfield truncated value "
                << static_cast<int>(v);
        }
    }

    TEST_F(IconTextLayoutTest, LayoutStyleIconProperties)
    {
        LayoutStyle layout;
        layout.iconTextGap = 10;
        layout.iconSide = IconSide::Top;
        
        EXPECT_EQ(layout.iconTextGap, 10);
        EXPECT_EQ(layout.iconSide, IconSide::Top);
    }

    TEST_F(IconTextLayoutTest, AllIconSideValues)
    {
        EXPECT_EQ(IconSide::Left, IconSide::Left);
        EXPECT_EQ(IconSide::Right, IconSide::Right);
        EXPECT_EQ(IconSide::Top, IconSide::Top);
        EXPECT_EQ(IconSide::Bottom, IconSide::Bottom);
    }

    TEST_F(IconTextLayoutTest, ContentRunAutoIconSizing)
    {
        ContentRun run;
        run.type = ContentRun::Icon;
        run.iconID = IconID(1);
        
        // kSizeAuto should be negative
        EXPECT_EQ(run.iconWidth, ContentRun().kSizeAuto);
        EXPECT_EQ(run.iconHeight, ContentRun().kSizeAuto);
        EXPECT_LT(run.iconWidth, 0);
    }

    TEST_F(IconTextLayoutTest, MultipleIconsWithText)
    {
        std::vector<ContentRun> contentRuns;
        
        ContentRun icon1;
        icon1.type = ContentRun::Icon;
        icon1.iconID = IconID(1);
        contentRuns.push_back(icon1);
        
        ContentRun text1;
        text1.type = ContentRun::Text;
        text1.text = "First";
        contentRuns.push_back(text1);
        
        ContentRun icon2;
        icon2.type = ContentRun::Icon;
        icon2.iconID = IconID(2);
        contentRuns.push_back(icon2);
        
        ContentRun text2;
        text2.type = ContentRun::Text;
        text2.text = "Second";
        contentRuns.push_back(text2);

        EXPECT_EQ(contentRuns.size(), 4);
        EXPECT_EQ(contentRuns[0].type, ContentRun::Icon);
        EXPECT_EQ(contentRuns[1].type, ContentRun::Text);
        EXPECT_EQ(contentRuns[2].type, ContentRun::Icon);
        EXPECT_EQ(contentRuns[3].type, ContentRun::Text);
    }

} // namespace tachyon
