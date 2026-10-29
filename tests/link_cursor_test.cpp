// Links in rich text: the cursor over them comes from their LinkStyle, which the theme's
// .link class fills, and hit tests find the run where it is drawn.

#include "headless_gui_app.h"
#include "skribidi_test_fixture.h"

#include <vector>

namespace tachyon
{
    using GUITest::HeadlessGUIApp;
    using GUITest::SkribidiHeadlessTest;

    namespace
    {
        class LinkCursorTest : public SkribidiHeadlessTest
        {
        protected:
            const ID element{ .value{ .integer = 501 } };

            // "Wiki" as a link with `linkStyle`, then " page" as hittable text that isn't a link,
            // in a 300x60 box, drawn the way the app draws it: into the element's content box.
            TextID Draw(StyleRef style, LinkStyle linkStyle, IntRect content = { 0, 0, 300, 60 })
            {
                runs = { { .type = ContentRun::Text, .text = "Wiki", .contentId = 1, .isLink = true, .linkStyle = linkStyle },
                         { .type = ContentRun::Text, .text = " page", .contentId = 2 } };
                const TextID id = textSystem->RegisterRichText(element, runs, style, ElementState::None);
                textSystem->RenderText(id, content, content, IntRect{ 0, 0, 2000, 2000 });
                return id;
            }

            StyleRef PlainStyle(TextAlign vertical = TextAlign::Start, uint16_t padding = 0)
            {
                Style s{};
                s.padding = BoxEdges(padding);
                s.textLayoutStyle.textWrapping      = TextWrap::NoWrap;
                s.textLayoutStyle.verticalAlignment = vertical;
                for (StyleState& state : s.states)
                    state.textStyle.fontSize = 16.0f;
                return styleManager.Register(s);
            }

            // Where the glyphs went: the top of the highest and the right of the rightmost.
            float GlyphTop() { return graphicsContext.Renderer().glyphTop; }

            std::vector<ContentRun> runs;
        };

        TEST_F(LinkCursorTest, ALinkHasItsOwnCursor)
        {
            const StyleRef style = PlainStyle();
            Draw(style, LinkStyle{ .cursor = MouseCursor::Crosshair });
            const int y = int(GlyphTop()) + 6;
            EXPECT_EQ(textSystem->LinkCursorAt(element, 4, y), MouseCursor::Crosshair);
            EXPECT_EQ(textSystem->LinkCursorAt(element, 4, 200), std::nullopt) << "below the text";

            const int32_t width = textSystem->GetTextMeasurement(textSystem->GetTextID(element)).maxWidth;
            EXPECT_EQ(textSystem->HitTestContent(textSystem->GetTextID(element), width - 3, y), 2);
            EXPECT_EQ(textSystem->LinkCursorAt(element, width - 3, y), std::nullopt) << "the text after it isn't a link";

            Draw(style, LinkStyle{});
            EXPECT_EQ(textSystem->LinkCursorAt(element, 4, y), MouseCursor::Pointer) << "the pointer unless set";
        }

        // A link with another style is other text: the new style is drawn and hit-tested.
        TEST_F(LinkCursorTest, ChangingALinksStyleUpdatesIt)
        {
            const StyleRef style = PlainStyle();
            const TextID first  = Draw(style, LinkStyle{ .cursor = MouseCursor::Crosshair });
            const TextID second = Draw(style, LinkStyle{ .cursor = MouseCursor::Move });
            EXPECT_NE(first, second);
            EXPECT_EQ(textSystem->LinkCursorAt(element, 4, int(GlyphTop()) + 6), MouseCursor::Move);

            const ContentRun a{ .type = ContentRun::Text, .text = "x", .contentId = 1, .isLink = true };
            ContentRun b = a;
            b.linkStyle.color = { 1, 2, 3, 255 };
            EXPECT_NE(a.GetHashCode(), b.GetHashCode());
            b = a;
            b.linkStyle.hoverColor = { 1, 2, 3, 255 };
            EXPECT_NE(a.GetHashCode(), b.GetHashCode());
        }

        // Positions are local to the content box, where the text is drawn: padding is already
        // out of them, and vertically centred text is found where it is drawn.
        TEST_F(LinkCursorTest, HitTestsFindTheTextWhereItIsDrawn)
        {
            const StyleRef padded = PlainStyle(TextAlign::Start, 20);
            const TextID paddedText = Draw(padded, LinkStyle{});
            const int top = int(GlyphTop()) + 6;
            EXPECT_EQ(textSystem->LinkCursorAt(element, 4, top), MouseCursor::Pointer);
            EXPECT_EQ(textSystem->HitTestContent(paddedText, 4, top), 1);

            graphicsContext.Renderer().glyphTop = 1e9f;
            const StyleRef centred = PlainStyle(TextAlign::Center);
            const TextID centredText = Draw(centred, LinkStyle{});
            const int middle = int(GlyphTop()) + 6;
            ASSERT_GT(middle, 20) << "the text is centred in the 60px box";
            EXPECT_EQ(textSystem->LinkCursorAt(element, 4, middle), MouseCursor::Pointer);
            EXPECT_EQ(textSystem->HitTestContent(centredText, 4, middle), 1);
            EXPECT_EQ(textSystem->HitTestContent(centredText, 4, 2), 0) << "above the centred text";
        }

        // The theme's .link class sets the links' colors and cursor.
        TEST(LinkStyleTheme, TheLinkClassSetsTheThemesLinkStyle)
        {
            HeadlessGUIApp dark;
            ASSERT_TRUE(dark.Initialize());
            EXPECT_EQ(dark.theme.linkStyle.color,      (color4b{ 0x8F, 0xB3, 0xFF, 255 }));   // --text-link
            EXPECT_EQ(dark.theme.linkStyle.hoverColor, (color4b{ 0xB5, 0xCD, 0xFF, 255 }));   // --text-link-hover
            EXPECT_EQ(dark.theme.linkStyle.cursor,     MouseCursor::Pointer);
            dark.Cleanup();

            HeadlessGUIApp light("themes/light.css");
            ASSERT_TRUE(light.Initialize());
            EXPECT_EQ(light.theme.linkStyle.color,      (color4b{ 0x24, 0x59, 0xC9, 255 }));
            EXPECT_EQ(light.theme.linkStyle.hoverColor, (color4b{ 0x1A, 0x42, 0x96, 255 }));
            light.Cleanup();
        }

        // Over a link the app shows the link's cursor; elsewhere, the element's own.
        TEST(LinkStyleTheme, TheAppShowsTheLinksCursor)
        {
            HeadlessGUIApp app;
            ASSERT_TRUE(app.Initialize());
            const ID text = app.idManager.CreateStaticID();
            std::vector<ContentRun> runs{ { .type = ContentRun::Text, .text = "link", .contentId = 1, .isLink = true } };
            auto frame = [&]
            {
                app.Frame([&]
                {
                    app.BeginRootWindow();
                    app.SelectableText(text, runs, LayoutOverride{}.Size(Pixels{ 200 }, Pixels{ 40 }));
                    app.EndRootWindow();
                });
            };
            frame();
            app.ownedTextSystem.linkCursor = MouseCursor::Crosshair;
            app.InjectMouseMove({ 20, 20 });
            frame();
            frame();
            EXPECT_EQ(app.guiStateManager.Cursor().Get(), MouseCursor::Crosshair);

            app.ownedTextSystem.linkCursor = std::nullopt;
            app.InjectMouseMove({ 21, 20 });
            frame();
            frame();
            EXPECT_EQ(app.guiStateManager.Cursor().Get(), MouseCursor::Default);
            app.idManager.ReleaseStaticID(text);
            app.Cleanup();
        }
    }
}
