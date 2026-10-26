#include "application.h"
#include <gui_core/diagnostics/gui_logging.h>

//
// This file is for testing and prototyping various miscellaneous GUI rendering features that don't fit into other categories, such as custom draw, on-demand color styles, polygons, dashed borders, etc.
//

#include <chrono>
#include <cmath>

static constexpr interstice::Diagnostics::LogCategory kLogDemo = interstice::Diagnostics::MakeLogCategory("demo");

namespace interstice
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
            interstice::ID id = idManager.AllocateDynamicID();
            interstice::Element shapeBox{};
            shapeBox.id = id;
            shapeBox.state = guiStateManager.GetCurrentState(shapeBox.id);
            shapeBox.styleRef = boxStyleRefs[demo.testBoxStyleCount];

            auto addRes = elementStorage.Add(std::move(shapeBox), interstice::LayoutOverride::FixedSize(Pixels{ size.x }, Pixels{ size.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));

            VERIFY(elementStorage.AddCustomDraw(addRes.ref, [elapsedMilliseconds, this](interstice::CustomDrawContext& ctx)
            {
                // Coords are inner-rect-local LAYOUT pixels.
                const StyleManager& sm = styleManager;
                const interstice::LineStyle lineStyle      = { .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = sm.Px(3.0f) };
                const interstice::LineStyle innerLineStyle = { .colorStyleIndex = triangleFillStyle, .lineWidth = sm.Px(10.0f) };
                const interstice::LineStyle outerLineStyle = { .colorStyleIndex = theme.lineDarkStyle, .lineWidth = innerLineStyle.lineWidth + sm.Px(3.0f) };

                ctx.RenderLine({ sm.Px(24), sm.Px(120) }, { sm.Px(24), sm.Px(24) }, { sm.Px(180), sm.Px(24) }, { sm.Px(180), sm.Px(120) }, lineStyle);
                ctx.RenderDot({ sm.Px(24.0f), sm.Px(120.0f) }, sm.Px(20.0f), { 1.0f, 0.2f, 0.2f, 1.0f });

                // Rotate triangle over time. elapsedMilliseconds is in ms.
                float t = elapsedMilliseconds * 0.001f; // seconds
                float angle = t; // radians (1 radian per second)
                float s = std::sin(angle);
                float c = std::cos(angle);

                // Element-local triangle points.
                interstice::float2 pa { sm.Px(260.f), sm.Px( 40.f) };
                interstice::float2 pb { sm.Px(300.f), sm.Px(130.f) };
                interstice::float2 pc { sm.Px(220.f), sm.Px(130.f) };

                // Compute centroid and rotate around it
                interstice::float2 center = (pa + pb + pc) * (1.0f / 3.0f);

                auto rotatePoint = [&](const interstice::float2& p) -> interstice::float2
                {
                    interstice::float2 v{ p.x - center.x, p.y - center.y };
                    interstice::float2 rp{ v.x * c - v.y * s, v.x * s + v.y * c };
                    interstice::float2 finalP{ rp.x + center.x, rp.y + center.y };
                    return interstice::float2{ (finalP.x + 0.5f), (finalP.y + 0.5f) };
                };

                ctx.RenderTriangle(rotatePoint(pa), rotatePoint(pb), rotatePoint(pc), triangleFillStyle);

                // Second triangle + colorwheel, centered at (400, 90) element-local.
                interstice::int2 c2{ sm.Px(400), sm.Px(90) };
                ctx.RenderTriangle(
                    interstice::float2{ sm.Px(-40.f), sm.Px( 30.f) } + c2,
                    interstice::float2{          0.f, sm.Px(-50.f) } + c2,
                    interstice::float2{ sm.Px( 40.f), sm.Px( 30.f) } + c2,
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

                interstice::ColorStyle cs;
                cs.type = interstice::ColorType::SolidColor;
                cs.gradient.stopCount = 1;
                cs.gradient.stops[0].SetColor(interstice::color4b { (uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255), 255 });

                if (animatedHueStyle == interstice::VisualColorStyleIndex::Invalid)
                    animatedHueStyle = styleManager.CreateColorStyle(cs);
                else
                    styleManager.UpdateColorStyle(animatedHueStyle, cs);
            }

            // --- Demo 2: polygon and dashed border ---

            size = { styleManager.Px(490), styleManager.Px(220) };
            interstice::ID demoId = idManager.AllocateDynamicID();
            interstice::Element demoBox{};
            demoBox.id = demoId;
            demoBox.state = guiStateManager.GetCurrentState(demoId);
            demoBox.styleRef = boxStyleRefs[demo.testBoxStyleCount];

            auto demoRes = elementStorage.Add(std::move(demoBox), interstice::LayoutOverride::FixedSize(Pixels{ size.x }, Pixels{ size.y }));
            VERIFY(demoRes.status);
            VERIFY(windowRegistry.Add(demoRes.ref));

            VERIFY(elementStorage.AddCustomDraw(demoRes.ref, [this](interstice::CustomDrawContext& ctx)
            {
                // Coords are inner-rect-local LAYOUT pixels.
                const StyleManager& sm = styleManager;

                // Demo 1: line using animated on-demand color style
                interstice::LineStyle animStyle { .colorStyleIndex = animatedHueStyle, .lineWidth = sm.Px(4.0f) };
                ctx.RenderLine({ sm.Px(10), sm.Px(20) }, { sm.Px(150), sm.Px(20) }, animStyle);

                // Demo 2a: filled hexagon
                const float hexRadius = sm.Px(55.0f);
                interstice::float2 hexCenter = { sm.Px(230.f), sm.Px(110.f) };
                interstice::float2 hexPts[6];
                for (int i = 0; i < 6; i++) {
                    float a = i * (3.14159f / 3.0f);
                    hexPts[i] = hexCenter + interstice::float2{ std::cos(a) * hexRadius, std::sin(a) * hexRadius };
                }
                ctx.RenderFilledPolygon(hexPts, triangleFillStyle);

                // Demo 2b: outlined pentagon
                const float pentRadius = sm.Px(50.0f);
                interstice::float2 pentCenterF = { sm.Px(80.f), sm.Px(120.f) };
                interstice::int2 pentPts[5];
                for (int i = 0; i < 5; i++) {
                    float a = i * (2.0f * 3.14159f / 5.0f) - 3.14159f / 2.0f;
                    pentPts[i] = interstice::int2 { (int)(pentCenterF.x + std::cos(a) * pentRadius), (int)(pentCenterF.y + std::sin(a) * pentRadius) };
                }
                interstice::LineStyle pentStyle { .colorStyleIndex = theme.lineWhiteStyle, .lineWidth = sm.Px(2.5f) };
                ctx.RenderPolyline(pentPts, /*closed=*/true, pentStyle);

                // Demo 3: dashed box
                ctx.RenderBox(
                    interstice::IntRect { .position = { sm.Px(340), sm.Px(60) }, .size = { sm.Px(130), sm.Px(80) } },
                    theme.dashedStyleRef.visualBoxStyleIndex, interstice::ElementState::None);
            }));
        }

        // Standalone icon examples
        {
            interstice::int2 iconSize = { styleManager.Px(48), styleManager.Px(48) };

            // Circle icon
            interstice::Element circleIconElem {};
            circleIconElem.id = idManager.AllocateDynamicID();
            circleIconElem.iconID = demo.circleIcon;
            circleIconElem.state = guiStateManager.GetCurrentState(circleIconElem.id);
            circleIconElem.styleRef = theme.firstIconStyleRef;

            auto circleRes = elementStorage.Add(std::move(circleIconElem), interstice::LayoutOverride::FixedSize(Pixels{ iconSize.x }, Pixels{ iconSize.y }));
            VERIFY(circleRes.status);
            VERIFY(windowRegistry.Add(circleRes.ref));

            // Heart icon
            interstice::Element heartIconElem {};
            heartIconElem.id = idManager.AllocateDynamicID();
            heartIconElem.iconID = demo.heartIcon;
            heartIconElem.state = guiStateManager.GetCurrentState(heartIconElem.id);
            heartIconElem.styleRef = theme.regularControls.button;

            auto heartRes = elementStorage.Add(std::move(heartIconElem), interstice::LayoutOverride::FixedSize(Pixels{ iconSize.x }, Pixels{ iconSize.y }));
            VERIFY(heartRes.status);
            VERIFY(windowRegistry.Add(heartRes.ref));

            // Star icon
            interstice::Element starIconElem {};
            starIconElem.id = idManager.AllocateDynamicID();
            starIconElem.iconID = demo.starIcon;
            starIconElem.state = guiStateManager.GetCurrentState(starIconElem.id);
            starIconElem.styleRef = theme.regularControls.button;

            auto starRes = elementStorage.Add(std::move(starIconElem), interstice::LayoutOverride::FixedSize(Pixels{ iconSize.x }, Pixels{ iconSize.y }));
            VERIFY(starRes.status);
            VERIFY(windowRegistry.Add(starRes.ref));
        }

        // Icon + Text button example
        {
            interstice::int2 buttonSize = { styleManager.Px(200), styleManager.Px(48) };
            interstice::ID iconButtonId = idManager.AllocateDynamicID();
            interstice::ElementState state = guiStateManager.GetCurrentState(iconButtonId);

            // Create button with icon and text using rich text
            static interstice::ContentRun buttonContent[] =
            {
                { .type = interstice::ContentRun::Icon, .iconID = demo.heartIcon, .iconWidth = 24.0f, .iconHeight = 24.0f },
                { .type = interstice::ContentRun::Text, .text = " Like This!" }
            };

            interstice::Element iconButtonElem{};
            iconButtonElem.id = iconButtonId;
            iconButtonElem.state = guiStateManager.GetCurrentState(iconButtonElem.id);
            iconButtonElem.styleRef = theme.regularControls.button;
            iconButtonElem.textID = textSystem->RegisterRichText(iconButtonId, buttonContent, theme.regularControls.button, state);
            iconButtonElem.eventHandler = MakeButtonHandler([]()
            {
                INTERSTICE_LOG_INFO(kLogDemo, "Icon button clicked!");
            });

            auto addRes = elementStorage.Add(std::move(iconButtonElem), interstice::LayoutOverride::FixedSize(Pixels{ buttonSize.x }, Pixels{ buttonSize.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
            tabChainManager.RegisterTabTarget(iconButtonId);
        }

        // Rich text with multiple icons example
        {
            interstice::int2 richTextSize = { styleManager.Px(300), styleManager.Px(60) };
            interstice::ID richTextId = idManager.AllocateDynamicID();
            interstice::ElementState state = guiStateManager.GetCurrentState(richTextId);

            static interstice::ContentRun richContent[] =
            {
                {.type = interstice::ContentRun::Text, .text = "Rate: " },
                {.type = interstice::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = interstice::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = interstice::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = interstice::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = interstice::ContentRun::Icon, .iconID = demo.starIcon, .iconWidth = 16.0f, .iconHeight = 16.0f },
                {.type = interstice::ContentRun::Text, .text = " Favorite: " },
                {.type = interstice::ContentRun::Icon, .iconID = demo.heartIcon, .iconWidth = 16.0f, .iconHeight = 16.0f }
            };

            interstice::Element richTextElem{};
            richTextElem.id = richTextId;
            richTextElem.state = guiStateManager.GetCurrentState(richTextElem.id);
            richTextElem.styleRef = theme.textStyleRef;
            richTextElem.textID = textSystem->RegisterRichText(richTextId, richContent, theme.textStyleRef, state);

            auto addRes = elementStorage.Add(std::move(richTextElem), interstice::LayoutOverride::FixedSize(Pixels{ richTextSize.x }, Pixels{ richTextSize.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // Rich text with clickable links
        {
            interstice::int2 linkTextSize = { styleManager.Px(400), styleManager.Px(60) };
            interstice::ID linkTextId = idManager.AllocateDynamicID();
            interstice::ElementState state = guiStateManager.GetCurrentState(linkTextId);

            static interstice::ContentRun linkContent[] =
            {
                { .type = interstice::ContentRun::Text, .text = "Learn about " },
                { .type = interstice::ContentRun::Text, .text = "our documentation",
                  .contentId = 1, .isLink = true,
                  .linkStyle = { .color = { 64, 148, 255, 255 }, .hoverColor = { 128, 200, 255, 255 } } },
                { .type = interstice::ContentRun::Text, .text = " or check " },
                { .type = interstice::ContentRun::Text, .text = "the source",
                  .contentId = 2, .isLink = true,
                  .linkStyle = { .color = { 64, 148, 255, 255 }, .hoverColor = { 128, 200, 255, 255 } } },
                { .type = interstice::ContentRun::Text, .text = " for more info." },
            };

            interstice::TextID linkTextID = textSystem->RegisterRichText(linkTextId, linkContent, theme.textStyleRef, state);

            interstice::Element linkElem{};
            linkElem.id = linkTextId;
            linkElem.state = guiStateManager.GetCurrentState(linkElem.id);
            linkElem.styleRef = theme.textStyleRef;
            linkElem.textID = linkTextID;
            linkElem.eventHandler = [this, linkTextID](ElementEvent&& event)->bool
            {
                if (event.type != interstice::EventType::MouseMove && event.type != interstice::EventType::MouseDown)
                    return false;

                // event.rect->innerRect is in layout space - use LocalCursor so the
                // subtract stays in the same space under any ancestor contentTransform.
                int2 pos = (event.rect != nullptr)
                    ? event.LocalCursor(elementStorage) - event.rect->innerRect.position
                    : event.state.screenPos;
                intptr_t hitId =
                    textSystem->HitTestContent(linkTextID, pos.x, pos.y);
                textSystem->SetHoveredContent(linkTextID, hitId);
                if (hitId != 0 && event.type == interstice::EventType::MouseDown && event.state.LeftPressed())
                {
                    INTERSTICE_LOG_INFO(kLogDemo, "Link clicked: {}", hitId == 1 ? "documentation" : "source");
                    return true;
                }
                return false;
            };

            auto addRes = elementStorage.Add(std::move(linkElem), interstice::LayoutOverride::FixedSize(Pixels{ linkTextSize.x }, Pixels{ linkTextSize.y }));
            VERIFY(addRes.status);
            VERIFY(windowRegistry.Add(addRes.ref));
        }

        // Image element - drop an image straight into the flow. Two display
        // sizes, then a clickable one standing in for an image-selection field.
        {
            Image(demoImageIndex, interstice::LayoutOverride::FixedSize(96, 96));
            Image(demoImageIndex, interstice::LayoutOverride::FixedSize(48, 48));

            const interstice::ID fieldID = idManager.AllocateDynamicID("imageField");
            interstice::Element field{};
            field.id              = fieldID;
            field.styleRef        = theme.imageStyleRef;
            field.backgroundImage = demoImageIndex;
            field.state           = guiStateManager.GetCurrentState(fieldID);
            field.eventHandler    = [](interstice::ElementEvent&& ev) -> bool
            {
                if (ev.type == interstice::EventType::MouseDown && ev.state.LeftPressed())
                {
                    INTERSTICE_LOG_INFO(kLogDemo, "Image field: open picker");
                    return true;
                }
                return false;
            };
            auto fRes = elementStorage.Add(std::move(field), interstice::LayoutOverride::FixedSize(64, 64));
            VERIFY(fRes.status);
            VERIFY(windowRegistry.Add(fRes.ref));
            SetTooltip("Click to choose an image");
        }

        // --- Demo: per-run font override via textStyleOverride ---
        {
            // A family registered from code (one face) and a generic family.
            static const interstice::FontID boldFontId = [this]
            {
                const std::string_view boldFaces[] = { "fonts/Inter/Inter-Bold.ttf" };
                return textSystem->RegisterFontFamily("Inter Bold", boldFaces);
            }();

            static interstice::TextStyle boldStyle  { .fontSize = 16.0f, .fontId = interstice::FontID::Invalid };
            static interstice::TextStyle monoStyle  { .fontSize = 14.0f, .family = interstice::FontFamily::Monospace };
            boldStyle.fontId   = boldFontId;

            interstice::int2 mixedFontSize = { styleManager.Px(420), styleManager.Px(30) };
            interstice::ID mixedFontId = idManager.AllocateDynamicID();
            interstice::ElementState mixedState = guiStateManager.GetCurrentState(mixedFontId);

            static interstice::ContentRun mixedFontRuns[] =
            {
                { .type = interstice::ContentRun::Text, .text = "Normal " },
                { .type = interstice::ContentRun::Text, .text = "Bold ",     .textStyleOverride = &boldStyle },
                { .type = interstice::ContentRun::Text, .text = "Monospace", .textStyleOverride = &monoStyle },
            };

            interstice::Element mixedElem{};
            mixedElem.id       = mixedFontId;
            mixedElem.state    = mixedState;
            mixedElem.styleRef = theme.textStyleRef;
            mixedElem.textID   = textSystem->RegisterRichText(mixedFontId, mixedFontRuns, theme.textStyleRef, mixedState);

            auto addRes = elementStorage.Add(std::move(mixedElem), interstice::LayoutOverride::FixedSize(Pixels{ mixedFontSize.x }, Pixels{ mixedFontSize.y }));
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
            BeginVertical(interstice::LayoutOverride::Auto().NoGrow());
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
            BeginVertical(interstice::LayoutOverride::Auto().NoGrow());
            {
                BeginHorizontal(interstice::LayoutOverride::Auto().NoGrow());
                for (const char* header : headers)
                    Text(header, interstice::LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                EndHorizontal();
                for (const auto& row : rows)
                {
                    BeginHorizontal(interstice::LayoutOverride::Auto().NoGrow());
                    Text(reinterpret_cast<const char*>(row[0]), interstice::LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                    for (int c = 0; c < 5; ++c)
                    {
                        const interstice::StyleRef style = styleManager.WithTextOverride(theme.staticTextStyleRef, { .language = interstice::LanguageTag(languages[c]) });
                        Text(reinterpret_cast<const char*>(row[c + 1]), style, interstice::LayoutOverride::Auto().MinWidth(Pixels{ colWidth }));
                    }
                    EndHorizontal();
                }
            }
            EndVertical();

            // A text box with its own font; all other text keeps the CSS font.
            static const interstice::FontID serifFont = [this]
            {
                const std::string_view faces[] = { "fonts/Noto-otf/NotoSerif-Regular.otf" };
                return textSystem->RegisterFontFamily("Noto Serif", faces);
            }();
            static std::string serifText = "This text box uses Noto Serif";
            Field(serifText, styleManager.WithTextOverride(theme.controls->textbox, { .font = serifFont }),
                  interstice::LayoutOverride::Auto().MinWidth(320).NoGrow());
        }

        // --- Icon + Text Examples with iconTextGap and iconSide ---

        {
            AddBox(idManager.AllocateDynamicID(), theme.iconLeftStyleRef, demo.circleIcon, "Left");

            AddBox(idManager.AllocateDynamicID(), theme.iconRightStyleRef, demo.starIcon, "Right");

            AddBox(idManager.AllocateDynamicID(), theme.iconTopStyleRef, demo.circleIcon, "Top");

            AddBox(idManager.AllocateDynamicID(), theme.iconBottomStyleRef, demo.starIcon, "Bottom");
        }

        // --- Vertical-align demo matrix -----------------------------------------
        // 4 icon sides x 3 vertical alignments (-interstice-vertical-text-align). The
        // boxes share an 80px min-height so the alignment differences show.
        {
            static const interstice::IconID kSideIcon[4] = {
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