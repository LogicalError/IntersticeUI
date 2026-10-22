#include "application.h"
#include <gui_core/diagnostics/gui_logging.h>

//
// This file is for testing and prototyping various miscellaneous GUI rendering features that don't fit into other categories, such as custom draw, on-demand color styles, polygons, dashed borders, etc.
//

#include <chrono>
#include <cmath>

static constexpr GUI::Diagnostics::LogCategory kLogDemo = GUI::Diagnostics::MakeLogCategory("demo");

namespace GUI
{

    void Application::RenderMiscPanel()
    {
        BeginHorizontalWrap();

        // Get current time from the system clock for animating some elements
        static const auto s_appStart = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float elapsedMilliseconds = std::chrono::duration<float, std::milli>(now - s_appStart).count();

        int2 size = { styleManager.Px(490), styleManager.Px(200) };
        {
            GUI::ID id = idManager.AllocateDynamicID();
            GUI::Element shapeBox{};
            shapeBox.id = id;
            shapeBox.state = guiStateManager.GetCurrentState(shapeBox.id);
            shapeBox.styleRef = boxStyleRefs[demo.testBoxStyleCount];

            auto addRes = elementStorage.Add(std::move(shapeBox), GUI::LayoutOverride::FixedSize(size.x, size.y));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));

            VERIFY(elementStorage.AddCustomDraw(addRes.ref, [elapsedMilliseconds, this](GUI::CustomDrawContext& ctx)
            {
                // Coords are inner-rect-local LAYOUT pixels.
                const StyleManager& sm = styleManager;
                const GUI::LineStyle lineStyle      = { .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = sm.Px(3.0f) };
                const GUI::LineStyle innerLineStyle = { .colorStyleIndex = triangleFillStyle, .lineWidth = sm.Px(10.0f) };
                const GUI::LineStyle outerLineStyle = { .colorStyleIndex = theme.lineDarkStyle, .lineWidth = innerLineStyle.lineWidth + sm.Px(3.0f) };

                ctx.RenderLine({ sm.Px(24), sm.Px(120) }, { sm.Px(24), sm.Px(24) }, { sm.Px(180), sm.Px(24) }, { sm.Px(180), sm.Px(120) }, lineStyle);
                ctx.RenderDot({ sm.Px(24.0f), sm.Px(120.0f) }, sm.Px(20.0f), { 1.0f, 0.2f, 0.2f, 1.0f });

                // Rotate triangle over time. elapsedMilliseconds is in ms.
                float t = elapsedMilliseconds * 0.001f; // seconds
                float angle = t; // radians (1 radian per second)
                float s = std::sin(angle);
                float c = std::cos(angle);

                // Element-local triangle points.
                GUI::float2 pa { sm.Px(260.f), sm.Px( 40.f) };
                GUI::float2 pb { sm.Px(300.f), sm.Px(130.f) };
                GUI::float2 pc { sm.Px(220.f), sm.Px(130.f) };

                // Compute centroid and rotate around it
                GUI::float2 center = (pa + pb + pc) * (1.0f / 3.0f);

                auto rotatePoint = [&](const GUI::float2& p) -> GUI::float2
                {
                    GUI::float2 v{ p.x - center.x, p.y - center.y };
                    GUI::float2 rp{ v.x * c - v.y * s, v.x * s + v.y * c };
                    GUI::float2 finalP{ rp.x + center.x, rp.y + center.y };
                    return GUI::float2{ (finalP.x + 0.5f), (finalP.y + 0.5f) };
                };

                ctx.RenderTriangle(rotatePoint(pa), rotatePoint(pb), rotatePoint(pc), triangleFillStyle);

                // Second triangle + colorwheel, centered at (400, 90) element-local.
                GUI::int2 c2{ sm.Px(400), sm.Px(90) };
                ctx.RenderTriangle(
                    GUI::float2{ sm.Px(-40.f), sm.Px( 30.f) } + c2,
                    GUI::float2{          0.f, sm.Px(-50.f) } + c2,
                    GUI::float2{ sm.Px( 40.f), sm.Px( 30.f) } + c2,
                    triangleFillStyle2);
                ctx.RenderCircle(c2, sm.Px(60.f), outerLineStyle);
                ctx.RenderCircle(c2, sm.Px(60.f), innerLineStyle);
            }));
        }

        // ---- Demo: on-demand color style, polygon, dashed border, per-run font ----
        {
            // --- Demo 1: on-demand color style (animated hue) ---
            {
                float hue = std::fmod(elapsedMilliseconds * 0.06f, 360.0f); // cycle hue
                float h   = hue / 60.0f;
                float s   = 1.0f, v = 1.0f;
                int   hi  = (int)h % 6;
                float f   = h - (float)(int)h;
                float p   = v * (1 - s), q = v * (1 - s * f), t2 = v * (1 - s * (1 - f));
                float r, g, b;
                if (hi == 0) { r=v; g=t2; b=p; }
                else if (hi == 1) { r=q; g=v; b=p; }
                else if (hi == 2) { r=p; g=v; b=t2; }
                else if (hi == 3) { r=p; g=q; b=v; }
                else if (hi == 4) { r=t2; g=p; b=v; }
                else              { r=v;  g=p; b=q; }

                GUI::ColorStyle cs;
                cs.type = GUI::ColorType::SolidColor;
                cs.gradient.stopCount = 1;
                cs.gradient.stops[0].SetColor(GUI::color4b { (uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255), 255 });

                if (animatedHueStyle == GUI::VisualColorStyleIndex::Invalid)
                    animatedHueStyle = styleManager.CreateColorStyle(cs);
                else
                    styleManager.UpdateColorStyle(animatedHueStyle, cs);
            }

            // --- Demo 2: polygon and dashed border ---

            size = { styleManager.Px(490), styleManager.Px(220) };
            GUI::ID demoId = idManager.AllocateDynamicID();
            GUI::Element demoBox{};
            demoBox.id = demoId;
            demoBox.state = guiStateManager.GetCurrentState(demoId);
            demoBox.styleRef = boxStyleRefs[demo.testBoxStyleCount];

            auto demoRes = elementStorage.Add(std::move(demoBox), GUI::LayoutOverride::FixedSize(size.x, size.y));
            VERIFY(demoRes.status);
            VERIFY(windowRegistry.Add(demoRes.ref));

            VERIFY(elementStorage.AddCustomDraw(demoRes.ref, [elapsedMilliseconds, this](GUI::CustomDrawContext& ctx)
            {
                // Coords are inner-rect-local LAYOUT pixels.
                const StyleManager& sm = styleManager;

                // Demo 1: line using animated on-demand color style
                GUI::LineStyle animStyle { .colorStyleIndex = animatedHueStyle, .lineWidth = sm.Px(4.0f) };
                ctx.RenderLine({ sm.Px(10), sm.Px(20) }, { sm.Px(150), sm.Px(20) }, animStyle);

                // Demo 2a: filled hexagon
                const float hexRadius = sm.Px(55.0f);
                GUI::float2 hexCenter = { sm.Px(230.f), sm.Px(110.f) };
                GUI::float2 hexPts[6];
                for (int i = 0; i < 6; i++) {
                    float a = i * (3.14159f / 3.0f);
                    hexPts[i] = hexCenter + GUI::float2{ std::cosf(a) * hexRadius, std::sinf(a) * hexRadius };
                }
                ctx.RenderFilledPolygon(hexPts, triangleFillStyle);

                // Demo 2b: outlined pentagon
                const float pentRadius = sm.Px(50.0f);
                GUI::float2 pentCenterF = { sm.Px(80.f), sm.Px(120.f) };
                GUI::int2 pentPts[5];
                for (int i = 0; i < 5; i++) {
                    float a = i * (2.0f * 3.14159f / 5.0f) - 3.14159f / 2.0f;
                    pentPts[i] = GUI::int2 { (int)(pentCenterF.x + std::cosf(a) * pentRadius), (int)(pentCenterF.y + std::sinf(a) * pentRadius) };
                }
                GUI::LineStyle pentStyle { .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = sm.Px(2.5f) };
                ctx.RenderPolyline(pentPts, /*closed=*/true, pentStyle);

                // Demo 3: dashed box
                ctx.RenderBox(
                    GUI::IntRect { .position = { sm.Px(340), sm.Px(60) }, .size = { sm.Px(130), sm.Px(80) } },
                    theme.dashedStyleRef.visualBoxStyleIndex, GUI::ElementState::None);
            }));
        }

        // Standalone icon examples
        {
            GUI::int2 iconSize = { styleManager.Px(48), styleManager.Px(48) };

            // Circle icon
            GUI::Element circleIconElem {};
            circleIconElem.id = idManager.AllocateDynamicID();
            circleIconElem.iconID = demo.circleIcon;
            circleIconElem.state = guiStateManager.GetCurrentState(circleIconElem.id);
            circleIconElem.styleRef = theme.firstIconStyleRef;

            auto circleRes = elementStorage.Add(std::move(circleIconElem), GUI::LayoutOverride::FixedSize(iconSize.x, iconSize.y));
            VERIFY(circleRes.status);
            VERIFY(windowRegistry.Add(circleRes.ref));

            // Heart icon
            GUI::Element heartIconElem {};
            heartIconElem.id = idManager.AllocateDynamicID();
            heartIconElem.iconID = demo.heartIcon;
            heartIconElem.state = guiStateManager.GetCurrentState(heartIconElem.id);
            heartIconElem.styleRef = theme.regularControls.button;

            auto heartRes = elementStorage.Add(std::move(heartIconElem), GUI::LayoutOverride::FixedSize(iconSize.x, iconSize.y));
            VERIFY(heartRes.status);
            VERIFY(windowRegistry.Add(heartRes.ref));

            // Star icon
            GUI::Element starIconElem {};
            starIconElem.id = idManager.AllocateDynamicID();
            starIconElem.iconID = demo.starIcon;
            starIconElem.state = guiStateManager.GetCurrentState(starIconElem.id);
            starIconElem.styleRef = theme.regularControls.button;

            auto starRes = elementStorage.Add(std::move(starIconElem), GUI::LayoutOverride::FixedSize(iconSize.x, iconSize.y));
            VERIFY(starRes.status);
            VERIFY(windowRegistry.Add(starRes.ref));
        }

        // Icon + Text button example
        {
            GUI::int2 buttonSize = { styleManager.Px(200), styleManager.Px(48) };
            GUI::ID iconButtonId = idManager.AllocateDynamicID();
            GUI::ElementState state = guiStateManager.GetCurrentState(iconButtonId);

            // Create button with icon and text using rich text
            static GUI::ContentRun buttonContent[] =
            {
                { .type = GUI::ContentRun::Icon, .iconID = demo.heartIcon, .iconWidth = 24.0f, .iconHeight = 24.0f },
                { .type = GUI::ContentRun::Text, .text = " Like This!" }
            };

            GUI::Element iconButtonElem{};
            iconButtonElem.id = iconButtonId;
            iconButtonElem.state = guiStateManager.GetCurrentState(iconButtonElem.id);
            iconButtonElem.styleRef = theme.regularControls.button;
            iconButtonElem.textID = textSystem->RegisterRichText(iconButtonId, buttonContent, theme.regularControls.button, state);
            iconButtonElem.eventHandler = MakeButtonHandler([this]()
            {
                LOG_INFO(kLogDemo, "Icon button clicked!");
            });

            auto addRes = elementStorage.Add(std::move(iconButtonElem), GUI::LayoutOverride::FixedSize(buttonSize.x, buttonSize.y));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
            tabChainManager.RegisterTabTarget(iconButtonId);
        }

        // Rich text with multiple icons example
        {
            GUI::int2 richTextSize = { styleManager.Px(300), styleManager.Px(60) };
            GUI::ID richTextId = idManager.AllocateDynamicID();
            GUI::ElementState state = guiStateManager.GetCurrentState(richTextId);

            static GUI::ContentRun richContent[] =
            {
                {.type = GUI::ContentRun::Text, .text = "Rate: " },
                {.type = GUI::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = GUI::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = GUI::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = GUI::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = GUI::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = GUI::ContentRun::Text, .text = " Favorite: " },
                {.type = GUI::ContentRun::Icon, .iconID = demo.heartIcon, .iconWidth = 16.0f, .iconHeight = 16.0f }
            };

            GUI::Element richTextElem{};
            richTextElem.id = richTextId;
            richTextElem.state = guiStateManager.GetCurrentState(richTextElem.id);
            richTextElem.styleRef = theme.textStyleRef;
            richTextElem.textID = textSystem->RegisterRichText(richTextId, richContent, theme.textStyleRef, state);

            auto addRes = elementStorage.Add(std::move(richTextElem), GUI::LayoutOverride::FixedSize(richTextSize.x, richTextSize.y));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // Rich text with clickable links
        {
            GUI::int2 linkTextSize = { styleManager.Px(400), styleManager.Px(60) };
            GUI::ID linkTextId = idManager.AllocateDynamicID();
            GUI::ElementState state = guiStateManager.GetCurrentState(linkTextId);

            static GUI::ContentRun linkContent[] =
            {
                { .type = GUI::ContentRun::Text, .text = "Learn about " },
                { .type = GUI::ContentRun::Text, .text = "our documentation",
                  .contentId = 1, .isLink = true,
                  .linkStyle = { .color = { 64, 148, 255, 255 }, .hoverColor = { 128, 200, 255, 255 } } },
                { .type = GUI::ContentRun::Text, .text = " or check " },
                { .type = GUI::ContentRun::Text, .text = "the source",
                  .contentId = 2, .isLink = true,
                  .linkStyle = { .color = { 64, 148, 255, 255 }, .hoverColor = { 128, 200, 255, 255 } } },
                { .type = GUI::ContentRun::Text, .text = " for more info." },
            };

            GUI::TextID linkTextID = textSystem->RegisterRichText(linkTextId, linkContent, theme.textStyleRef, state);

            GUI::Element linkElem{};
            linkElem.id = linkTextId;
            linkElem.state = guiStateManager.GetCurrentState(linkElem.id);
            linkElem.styleRef = theme.textStyleRef;
            linkElem.textID = linkTextID;
            linkElem.eventHandler = [this, linkTextID](ElementEvent&& event)->bool
            {
                if (event.type != GUI::EventType::MouseMove && event.type != GUI::EventType::MouseDown)
                    return false;

                // event.rect->innerRect is in layout space — use LocalCursor so the
                // subtract stays in the same space under any ancestor contentTransform.
                int2 pos = (event.rect != nullptr)
                    ? event.LocalCursor(elementStorage) - event.rect->innerRect.position
                    : event.state.screenPos;
                intptr_t hitId =
                    textSystem->HitTestContent(linkTextID, pos.x, pos.y);
                textSystem->SetHoveredContent(linkTextID, hitId);
                if (hitId != 0 && event.type == GUI::EventType::MouseDown && event.state.LeftPressed())
                {
                    LOG_INFO(kLogDemo, "Link clicked: {}", hitId == 1 ? "documentation" : "source");
                    return true;
                }
                return false;
            };

            auto addRes = elementStorage.Add(std::move(linkElem), GUI::LayoutOverride::FixedSize(linkTextSize.x, linkTextSize.y));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // Image element — drop an image straight into the flow. Two display
        // sizes, then a clickable one standing in for an image-selection field.
        {
            Image(demoImageIndex, GUI::LayoutOverride::FixedSize(styleManager.Px(96), styleManager.Px(96)));
            Image(demoImageIndex, GUI::LayoutOverride::FixedSize(styleManager.Px(48), styleManager.Px(48)));

            const GUI::ID fieldID = idManager.AllocateDynamicID("imageField");
            GUI::Element field{};
            field.id              = fieldID;
            field.styleRef        = theme.imageStyleRef;
            field.backgroundImage = demoImageIndex;
            field.state           = guiStateManager.GetCurrentState(fieldID);
            field.eventHandler    = [](GUI::ElementEvent&& ev) -> bool
            {
                if (ev.type == GUI::EventType::MouseDown && ev.state.LeftPressed())
                {
                    LOG_INFO(kLogDemo, "Image field: open picker");
                    return true;
                }
                return false;
            };
            auto fRes = elementStorage.Add(std::move(field), GUI::LayoutOverride::FixedSize(styleManager.Px(64), styleManager.Px(64)));
            VERIFY(fRes.status);
            VERIFY(windowRegistry.Add(fRes.ref));
            SetTooltip("Click to choose an image");
        }

        // --- Demo: per-run font override via textStyleOverride ---
        {
            // A family registered from code (one face) and a generic family.
            static const GUI::FontID boldFontId = [this]
            {
                const std::string_view boldFaces[] = { "fonts/Inter/Inter-Bold.ttf" };
                return textSystem->RegisterFontFamily("Inter Bold", boldFaces);
            }();

            static GUI::TextStyle boldStyle  { .fontSize = 16.0f, .fontId = GUI::FontID::Invalid };
            static GUI::TextStyle monoStyle  { .fontSize = 14.0f, .family = GUI::FontFamily::Monospace };
            boldStyle.fontId   = boldFontId;

            GUI::int2 mixedFontSize = { styleManager.Px(420), styleManager.Px(30) };
            GUI::ID mixedFontId = idManager.AllocateDynamicID();
            GUI::ElementState mixedState = guiStateManager.GetCurrentState(mixedFontId);

            static GUI::ContentRun mixedFontRuns[] =
            {
                { .type = GUI::ContentRun::Text, .text = "Normal " },
                { .type = GUI::ContentRun::Text, .text = "Bold ",     .textStyleOverride = &boldStyle },
                { .type = GUI::ContentRun::Text, .text = "Monospace", .textStyleOverride = &monoStyle },
            };

            GUI::Element mixedElem{};
            mixedElem.id       = mixedFontId;
            mixedElem.state    = mixedState;
            mixedElem.styleRef = theme.textStyleRef;
            mixedElem.textID   = textSystem->RegisterRichText(mixedFontId, mixedFontRuns, theme.textStyleRef, mixedState);

            auto addRes = elementStorage.Add(std::move(mixedElem), GUI::LayoutOverride::FixedSize(mixedFontSize.x, mixedFontSize.y));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // --- Script coverage: text the default font lacks, drawn by fallback fonts ---
        {
            static const char8_t* const samples[] =
            {
                u8"Symbols:  ★ ✓ ✗ → ⚠ ♪ ∑ √ ∞",
                u8"Emoji:    😀 🎉 👍 🚀",
                u8"Korean:   안녕하세요, 세계",
                u8"Chinese:  你好，世界",
                u8"Japanese: こんにちは、カタカナ、漢字",
                u8"Arabic:   مرحبا بالعالم",
                u8"Hindi:    नमस्ते दुनिया",
                u8"Thai:     สวัสดีชาวโลก",
                u8"Mixed:    Inter, 中文, 한국어 and العربية on one line",
            };
            BeginVertical(GUI::LayoutOverride::Auto().NoGrow());
            for (const char8_t* sample : samples)
                Text(reinterpret_cast<const char*>(sample));
            EndVertical();
        }

        // --- Per-element text overrides (StyleManager::WithTextOverride) -----------
        {
            // Translation table: each column's text is in its own language, so shared
            // characters (first row) take that region's letterforms from one CJK font.
            static const char* const languages[] = { "ja", "zh-Hans", "zh-Hant", "zh-HK", "ko" };
            static const char* const headers[]   = { "", "Japanese", "Simplified", "Traditional", "Hong Kong", "Korean" };
            static const char8_t* const rows[][6] =
            {
                { u8"Forms",    u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述", u8"次 直 骨 述" },
                { u8"Settings", u8"設定",        u8"设置",        u8"設定",        u8"設定",        u8"설정" },
                { u8"Delete",   u8"削除",        u8"删除",        u8"刪除",        u8"刪除",        u8"삭제" },
                { u8"Edit",     u8"編集",        u8"编辑",        u8"編輯",        u8"編輯",        u8"편집" },
            };
            const int colWidth = styleManager.Px(96);
            BeginVertical(GUI::LayoutOverride::Auto().NoGrow());
            {
                BeginHorizontal(GUI::LayoutOverride::Auto().NoGrow());
                for (const char* header : headers)
                    Text(header, GUI::LayoutOverride::Auto().MinWidth(colWidth));
                EndHorizontal();
                for (const auto& row : rows)
                {
                    BeginHorizontal(GUI::LayoutOverride::Auto().NoGrow());
                    Text(reinterpret_cast<const char*>(row[0]), GUI::LayoutOverride::Auto().MinWidth(colWidth));
                    for (int c = 0; c < 5; ++c)
                    {
                        const GUI::StyleRef style = styleManager.WithTextOverride(theme.staticTextStyleRef, { .language = GUI::LanguageTag(languages[c]) });
                        Text(reinterpret_cast<const char*>(row[c + 1]), style, GUI::LayoutOverride::Auto().MinWidth(colWidth));
                    }
                    EndHorizontal();
                }
            }
            EndVertical();

            // A text box with its own font; all other text keeps the CSS font.
            static const GUI::FontID serifFont = [this]
            {
                const std::string_view faces[] = { "fonts/Noto-otf/NotoSerif-Regular.otf" };
                return textSystem->RegisterFontFamily("Noto Serif", faces);
            }();
            static std::string serifText = "This text box uses Noto Serif";
            Field(serifText, styleManager.WithTextOverride(theme.controls->textbox, { .font = serifFont }),
                  GUI::LayoutOverride::Auto().MinWidth(styleManager.Px(320)).NoGrow());
        }

        // --- Icon + Text Examples with iconTextGap and iconSide ---

        {
            AddBox(idManager.AllocateDynamicID(), theme.iconLeftStyleRef, demo.circleIcon, "Left");

            AddBox(idManager.AllocateDynamicID(), theme.iconRightStyleRef, demo.starIcon, "Right");

            AddBox(idManager.AllocateDynamicID(), theme.iconTopStyleRef, demo.circleIcon, "Top");

            AddBox(idManager.AllocateDynamicID(), theme.iconBottomStyleRef, demo.starIcon, "Bottom");
        }

        // --- Vertical-align demo matrix -----------------------------------------
        // 4 icon sides × 3 vertical alignments (-interstice-vertical-text-align). The
        // boxes share an 80px min-height so the alignment differences show.
        {
            static const GUI::IconID kSideIcon[4] = {
                demo.circleIcon, demo.starIcon, demo.circleIcon, demo.starIcon
            };
            static const char* kLabel[4][3] = {
                { "L / top",    "L / mid",    "L / bot"    },
                { "R / top",    "R / mid",    "R / bot"    },
                { "T / top",    "T / mid",    "T / bot"    },
                { "B / top",    "B / mid",    "B / bot"    },
            };
            for (int s = 0; s < 4; ++s)
            {
                for (int v = 0; v < 3; ++v)
                {
                    AddBox(idManager.AllocateDynamicID(),
                           iconVAlignDemoStyleRefs[s][v],
                           kSideIcon[s],
                           kLabel[s][v]);
                }
            }
        }

        EndHorizontalWrap();
    }

}