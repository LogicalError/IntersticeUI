#include <gui_core/style/gui_style_sheet_internal.h>

// Per-property CSS value parsing (Parser::ParseDeclValue) and application to a
// Style (Parser::ApplyDecl). Adding a CSS property touches only this file.

namespace interstice {

// Parser::ParseDeclValue  (out-of-class definition)
// ============================================================================

// justify-items: stretch has no JustifyContent value; it is the grid's fill-the-cell mode.
static constexpr int kJustifyItemsStretch = -1;

bool Parser::ParseDeclValue(StyleDecl& d)
{
    const std::string& p = d.prop;

    // --- Color properties ---------------------------------------------------
    if (ICEq(p,"background-color") || ICEq(p,"border-color") || ICEq(p,"color") ||
        ICEq(p,"-interstice-icon-color") || ICEq(p,"border-top-color") || ICEq(p,"border-right-color") ||
        ICEq(p,"border-bottom-color") || ICEq(p,"border-left-color"))
        return ParseColorStyleValue(d);

    if (ICEq(p,"text-underline-color")    || ICEq(p,"text-overline-color") ||
        ICEq(p,"text-throughline-color")  || ICEq(p,"text-bottomline-color"))
        return ParseColorValue(d.color4bVal);

    // --- Box model shorthands -----------------------------------------------
    if (ICEq(p,"margin") || ICEq(p,"padding") || ICEq(p,"border-width"))
        return ParseEdges(d.edges);

    if (ICEq(p,"border-radius"))
        return ParseCorners(d.corners);

    // Single-edge / corner
    static const char* kEdgeProps[] = {
        "margin-top","margin-right","margin-bottom","margin-left",
        "padding-top","padding-right","padding-bottom","padding-left",
        "border-top-width","border-right-width","border-bottom-width","border-left-width",
        "border-top-left-radius","border-top-right-radius",
        "border-bottom-right-radius","border-bottom-left-radius",
        nullptr
    };
    for (int i = 0; kEdgeProps[i]; ++i)
        if (ICEq(p, kEdgeProps[i])) return ParseDim(d.uVal);

    // --- Float scalars ------------------------------------------------------
    static const char* kFloatProps[] = {
        "font-size","letter-spacing","word-spacing",
        "-interstice-icon-text-gap","-interstice-icon-size","-interstice-icon-offset-y","gap","flex-grow","flex-shrink",
        "text-underline-thickness","text-underline-offset",
        "text-overline-thickness","text-overline-offset",
        "text-throughline-thickness","text-throughline-offset",
        "text-bottomline-thickness","text-bottomline-offset",
        "-interstice-border-dash-size","-interstice-border-dash-offset",
        nullptr
    };
    for (int i = 0; kFloatProps[i]; ++i)
    {
        if (ICEq(p, kFloatProps[i]))
        {
            if (m_lex.Check(TT::Number))
            {
                d.fVal = ConsumeFloat();
                if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"px")) m_lex.Consume();
                return true;
            }
            if (TryParseNumericVar(d.fVal)) return true;
            Error("Expected number or var() for '"+p+"'"); return false;
        }
    }

    // --- Grid gap properties (CSS: column-gap / row-gap) ----------------------
    if (ICEq(p,"column-gap") || ICEq(p,"row-gap"))
    {
        if (m_lex.Check(TT::Number))
        {
            d.iVal = (int32_t)ConsumeInt();
            if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"px")) m_lex.Consume();
            return true;
        }
        float f = 0;
        if (TryParseNumericVar(f)) { d.iVal = (int32_t)f; return true; }
        Error("Expected number or var() for '"+p+"'"); return false;
    }

    // --- Grid column/row shorthand: span N, <start>, or <start> / span <N> --
    if (ICEq(p,"grid-column") || ICEq(p,"grid-row"))
    {
        // "span N" -> sets span only
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "span"))
        {
            m_lex.Consume(); // consume "span"
            if (!m_lex.Check(TT::Number)) { Error("Expected number after 'span'"); return false; }
            d.iVal = -1; // start = auto
            d.uVal = (uint16_t)ConsumeInt();
            d.hasSecondNumber = true;
            return true;
        }
        // "<start>" or "<start> / span <N>"
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"auto"))
            { m_lex.Consume(); d.iVal = -1; }
        else if (m_lex.Check(TT::Number))
            d.iVal = (int32_t)ConsumeInt();
        else { Error("Expected number, auto, or 'span N' for '"+p+"'"); return false; }

        // Optional: / span N
        if (m_lex.Check(TT::Slash))
        {
            m_lex.Consume(); // consume '/'
            if (!m_lex.Check(TT::Ident) || !ICEq(m_lex.Peek().text, "span"))
                { Error("Expected 'span' after '/' in '"+p+"'"); return false; }
            m_lex.Consume(); // consume "span"
            if (!m_lex.Check(TT::Number)) { Error("Expected number after 'span'"); return false; }
            d.uVal = (uint16_t)ConsumeInt();
            d.hasSecondNumber = true;
        }
        return true;
    }

    // --- Grid placement (integer or "auto" -> -1) ----------------------------
    if (ICEq(p,"grid-column-start") || ICEq(p,"grid-row-start"))
    {
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"auto"))
            { m_lex.Consume(); d.iVal = -1; return true; }
        if (!m_lex.Check(TT::Number)) { Error("Expected number or auto for '"+p+"'"); return false; }
        d.iVal = (int32_t)ConsumeInt();
        return true;
    }

    // --- Grid track (auto | Npx | Nfr) --------------------------------------
    if (ICEq(p,"-interstice-grid-column-track") || ICEq(p,"-interstice-grid-row-track"))
        return ParseGridTrack(d.gridTrackVal);

    // --- line-height: normal | <number>px | <number>em | <number> -----------
    if (ICEq(p,"line-height"))
    {
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"normal"))
            { m_lex.Consume(); d.fVal = 1.0f; d.enumVal = (int)LineHeight::MetricsRelative; return true; }
        if (!m_lex.Check(TT::Number)) { Error("Expected number or 'normal' for 'line-height'"); return false; }
        d.fVal = ConsumeFloat();
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"px"))
            { m_lex.Consume(); d.enumVal = (int)LineHeight::Absolute; }
        else if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"em"))
            { m_lex.Consume(); d.enumVal = (int)LineHeight::FontSizeRelative; }
        else
            d.enumVal = (int)LineHeight::FontSizeRelative; // bare number = multiplier (CSS standard)
        return true;
    }

    // --- CSS size properties (width/height accept auto or <px>) -------------
    if (ICEq(p,"width") || ICEq(p,"height"))
    {
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"auto"))
            { m_lex.Consume(); d.iVal = LayoutData::kAutoSize; return true; }
        if (!m_lex.Check(TT::Number)) { Error("Expected number or auto for '"+p+"'"); return false; }
        d.iVal = (int32_t)ConsumeFloat();
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"px")) m_lex.Consume();
        return true;
    }
    if (ICEq(p,"min-width") || ICEq(p,"max-width") || ICEq(p,"min-height") || ICEq(p,"max-height"))
    {
        if (!m_lex.Check(TT::Number)) { Error("Expected number for '"+p+"'"); return false; }
        d.iVal = (int32_t)ConsumeFloat();
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"px")) m_lex.Consume();
        return true;
    }

    // --- flex-basis ---------------------------------------------------------
    if (ICEq(p,"flex-basis"))
    {
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"auto"))
        {
            m_lex.Consume(); d.iVal = FlexProperties::kAuto; return true;
        }
        if (!ParseDim(d.uVal)) return false;
        d.iVal = (int16_t)d.uVal;
        return true;
    }

    // --- flex shorthand: flex: none | auto | <grow> [<shrink>] [<basis>] ---
    if (ICEq(p,"flex"))
    {
        if (m_lex.Check(TT::Ident))
        {
            std::string kw = m_lex.Peek().text;
            if (ICEq(kw,"none")) { m_lex.Consume(); d.fVal = 0; d.uVal2 = 0; d.iVal = FlexProperties::kAuto; return true; }
            if (ICEq(kw,"auto")) { m_lex.Consume(); d.fVal = 1; d.uVal2 = 1; d.iVal = FlexProperties::kAuto; return true; }
        }
        if (!m_lex.Check(TT::Number)) { Error("Expected number, 'none', or 'auto' for 'flex'"); return false; }
        d.fVal = ConsumeFloat();  // grow
        d.uVal2 = 1;                                // shrink default
        d.iVal = 0;                                  // basis default (0 when using shorthand)
        if (m_lex.Check(TT::Number))
        {
            d.uVal2 = (uint16_t)std::clamp((int)ConsumeFloat(), 0, 65535); // shrink
            if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"auto"))
            {
                m_lex.Consume(); d.iVal = FlexProperties::kAuto;
            }
            else if (m_lex.Check(TT::Number))
            {
                float basis = ConsumeFloat();
                if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text,"px")) m_lex.Consume();
                d.iVal = (int16_t)basis;
            }
        }
        return true;
    }

    if (ICEq(p,"font-family"))
        return ParseFontFamilyList(d);

    // -interstice-language: <bcp47-tag> | "<bcp47-tag>" | none
    if (ICEq(p,"-interstice-language"))
    {
        if (!m_lex.Check(TT::Ident) && !m_lex.Check(TT::StringLit)) { Error("Expected a language tag for '-interstice-language'"); return false; }
        const std::string tag = m_lex.Consume().text;
        if (ICEq(tag, "none")) { d.languageVal = {}; return true; }
        d.languageVal = LanguageTag(tag);
        if (d.languageVal.empty()) { Error("Language tag '" + tag + "' is too long"); return false; }
        return true;
    }

    // --- Enum properties ----------------------------------------------------
    if (!m_lex.Check(TT::Ident) && !m_lex.Check(TT::Number))
    {
        Error("Expected value for '" + p + "'"); return false;
    }

    if (ICEq(p,"font-weight") && m_lex.Check(TT::Number))
    {
        int n = ConsumeInt();
        d.enumVal = (int)(
            n<=100 ? FontWeight::Thin       : n<=200 ? FontWeight::ExtraLight :
            n<=300 ? FontWeight::Light       : n<=400 ? FontWeight::Normal     :
            n<=500 ? FontWeight::Medium      : n<=600 ? FontWeight::DemiBold   :
            n<=700 ? FontWeight::Bold        : n<=800 ? FontWeight::ExtraBold  :
            n<=900 ? FontWeight::Black       : FontWeight::ExtraBlack);
        return true;
    }

    std::string v = m_lex.Consume().text;

    if (ICEq(p,"break-after"))
    {
        // break-after: auto | always | avoid | left | right
        // auto = reset all, always/right = breakAfter, left = breakBefore, avoid = keepWithNext
        if      (ICEq(v,"auto"))    { d.enumVal = 0; return true; }
        else if (ICEq(v,"always"))  { d.enumVal = 1; return true; }
        else if (ICEq(v,"right"))   { d.enumVal = 1; return true; } // same as always
        else if (ICEq(v,"left"))    { d.enumVal = 2; return true; }
        else if (ICEq(v,"avoid"))   { d.enumVal = 3; return true; }
        Error("Unknown break-after '"+v+"'"); return false;
    }
    if (ICEq(p,"-interstice-clamp-scroll"))
    {
        if      (ICEq(v,"true")  || ICEq(v,"1")) { d.enumVal = 1; return true; }
        else if (ICEq(v,"false") || ICEq(v,"0")) { d.enumVal = 0; return true; }
        Error("Expected true/false for '"+p+"'"); return false;
    }
    if (ICEq(p,"border-style"))
    {
        // border-style: solid | dashed | dotted - sets dash-size defaults
        if      (ICEq(v,"solid"))  { d.enumVal = 0; return true; }
        else if (ICEq(v,"dashed")) { d.enumVal = 1; return true; }
        else if (ICEq(v,"dotted")) { d.enumVal = 2; return true; }
        Error("Unknown border-style '"+v+"'"); return false;
    }
    if (ICEq(p,"display"))
    {
        if      (ICEq(v,"flex"))  { d.enumVal=(int)Display::Flex;  return true; }
        else if (ICEq(v,"grid"))  { d.enumVal=(int)Display::Grid;  return true; }
        else if (ICEq(v,"none"))  { d.enumVal=(int)Display::None;  return true; }
        Error("Unknown display '"+v+"'"); return false;
    }
    if (ICEq(p,"overflow") || ICEq(p,"overflow-x") || ICEq(p,"overflow-y"))
    {
        if      (ICEq(v,"auto"))    { d.enumVal=(int)Overflow::Auto;    return true; }
        else if (ICEq(v,"hidden"))  { d.enumVal=(int)Overflow::Hidden;  return true; }
        else if (ICEq(v,"visible")) { d.enumVal=(int)Overflow::Visible; return true; }
        Error("Unknown overflow '"+v+"'"); return false;
    }
    if (ICEq(p,"visibility"))
    {
        if      (ICEq(v,"visible"))   { d.enumVal=(int)Visibility::Visible;   return true; }
        else if (ICEq(v,"collapsed")) { d.enumVal=(int)Visibility::Collapsed; return true; }
        Error("Unknown visibility '"+v+"'"); return false;
    }
    if (ICEq(p,"flex-direction"))
    {
        if      (ICEq(v,"row"))            { d.enumVal=(int)FlexDirection::Row;           return true; }
        else if (ICEq(v,"row-reverse"))    { d.enumVal=(int)FlexDirection::RowReverse;    return true; }
        else if (ICEq(v,"column"))         { d.enumVal=(int)FlexDirection::Column;        return true; }
        else if (ICEq(v,"column-reverse")) { d.enumVal=(int)FlexDirection::ColumnReverse; return true; }
        Error("Unknown flex-direction '"+v+"'"); return false;
    }
    if (ICEq(p,"flex-wrap"))
    {
        if      (ICEq(v,"nowrap"))       { d.enumVal=(int)FlexWrap::NoWrap;      return true; }
        else if (ICEq(v,"wrap"))         { d.enumVal=(int)FlexWrap::Wrap;         return true; }
        else if (ICEq(v,"wrap-reverse")) { d.enumVal=(int)FlexWrap::WrapReverse;  return true; }
        Error("Unknown flex-wrap '"+v+"'"); return false;
    }
    if (ICEq(p,"align-items"))
    {
        if      (ICEq(v,"flex-start")||ICEq(v,"start")) { d.enumVal=(int)AlignItems::FlexStart; return true; }
        else if (ICEq(v,"flex-end")||ICEq(v,"end"))     { d.enumVal=(int)AlignItems::FlexEnd;   return true; }
        else if (ICEq(v,"center"))                       { d.enumVal=(int)AlignItems::Center;    return true; }
        else if (ICEq(v,"stretch"))                      { d.enumVal=(int)AlignItems::Stretch;   return true; }
        Error("Unknown align-items '"+v+"'"); return false;
    }
    if (ICEq(p,"align-self"))
    {
        if      (ICEq(v,"auto"))                         { d.enumVal=(int)AlignItems::Auto;      return true; }
        else if (ICEq(v,"flex-start")||ICEq(v,"start")) { d.enumVal=(int)AlignItems::FlexStart; return true; }
        else if (ICEq(v,"flex-end")||ICEq(v,"end"))     { d.enumVal=(int)AlignItems::FlexEnd;   return true; }
        else if (ICEq(v,"center"))                       { d.enumVal=(int)AlignItems::Center;    return true; }
        else if (ICEq(v,"stretch"))                      { d.enumVal=(int)AlignItems::Stretch;   return true; }
        Error("Unknown align-self '"+v+"'"); return false;
    }
    if (ICEq(p,"justify-content"))
    {
        if      (ICEq(v,"flex-start")||ICEq(v,"start")) { d.enumVal=(int)JustifyContent::FlexStart;    return true; }
        else if (ICEq(v,"flex-end")||ICEq(v,"end"))     { d.enumVal=(int)JustifyContent::FlexEnd;      return true; }
        else if (ICEq(v,"center"))                       { d.enumVal=(int)JustifyContent::Center;       return true; }
        else if (ICEq(v,"space-between"))                { d.enumVal=(int)JustifyContent::SpaceBetween; return true; }
        else if (ICEq(v,"space-around"))                 { d.enumVal=(int)JustifyContent::SpaceAround;  return true; }
        else if (ICEq(v,"space-evenly"))                 { d.enumVal=(int)JustifyContent::SpaceEvenly;  return true; }
        Error("Unknown justify-content '"+v+"'"); return false;
    }
    if (ICEq(p,"align-content"))
    {
        if      (ICEq(v,"flex-start")||ICEq(v,"start")) { d.enumVal=(int)AlignContent::FlexStart;    return true; }
        else if (ICEq(v,"flex-end")||ICEq(v,"end"))     { d.enumVal=(int)AlignContent::FlexEnd;      return true; }
        else if (ICEq(v,"center"))                       { d.enumVal=(int)AlignContent::Center;       return true; }
        else if (ICEq(v,"stretch"))                      { d.enumVal=(int)AlignContent::Stretch;      return true; }
        else if (ICEq(v,"space-between"))                { d.enumVal=(int)AlignContent::SpaceBetween; return true; }
        else if (ICEq(v,"space-around"))                 { d.enumVal=(int)AlignContent::SpaceAround;  return true; }
        else if (ICEq(v,"space-evenly"))                 { d.enumVal=(int)AlignContent::SpaceEvenly;  return true; }
        Error("Unknown align-content '"+v+"'"); return false;
    }
    if (ICEq(p,"cursor"))
    {
        if      (ICEq(v,"default"))     { d.enumVal=(int)MouseCursor::Default;      return true; }
        else if (ICEq(v,"text"))        { d.enumVal=(int)MouseCursor::Text;      return true; }
        else if (ICEq(v,"wait"))        { d.enumVal=(int)MouseCursor::Wait;       return true; }
        else if (ICEq(v,"crosshair"))   { d.enumVal=(int)MouseCursor::Crosshair;  return true; }
        else if (ICEq(v,"pointer"))     { d.enumVal=(int)MouseCursor::Pointer;       return true; }
        else if (ICEq(v,"col-resize"))  { d.enumVal=(int)MouseCursor::ColResize;   return true; }
        else if (ICEq(v,"row-resize"))  { d.enumVal=(int)MouseCursor::RowResize;   return true; }
        else if (ICEq(v,"ew-resize"))   { d.enumVal=(int)MouseCursor::EwResize;    return true; }
        else if (ICEq(v,"ns-resize"))   { d.enumVal=(int)MouseCursor::NsResize;    return true; }
        else if (ICEq(v,"nwse-resize")) { d.enumVal=(int)MouseCursor::NwseResize; return true; }
        else if (ICEq(v,"nesw-resize")) { d.enumVal=(int)MouseCursor::NeswResize; return true; }
        else if (ICEq(v,"move"))        { d.enumVal=(int)MouseCursor::Move;  return true; }
        else if (ICEq(v,"not-allowed")) { d.enumVal=(int)MouseCursor::NotAllowed; return true; }
        else if (ICEq(v,"none"))        { d.enumVal=(int)MouseCursor::None;       return true; }
        Error("Unknown cursor '"+v+"'"); return false;
    }
    if (ICEq(p,"text-align"))
    {
        if      (ICEq(v,"start"))  { d.enumVal=(int)TextAlign::Start;  return true; }
        else if (ICEq(v,"center")) { d.enumVal=(int)TextAlign::Center; return true; }
        else if (ICEq(v,"end"))    { d.enumVal=(int)TextAlign::End;    return true; }
        else if (ICEq(v,"left"))   { d.enumVal=(int)TextAlign::Left;   return true; }
        else if (ICEq(v,"right"))  { d.enumVal=(int)TextAlign::Right;  return true; }
        else if (ICEq(v,"top"))    { d.enumVal=(int)TextAlign::Top;    return true; }
        else if (ICEq(v,"bottom")) { d.enumVal=(int)TextAlign::Bottom; return true; }
        Error("Unknown text-align '"+v+"'"); return false;
    }
    if (ICEq(p,"-interstice-vertical-trim"))
    {
        if (!m_lex.Check(TT::Ident)) { Error("Expected identifier for '-interstice-vertical-trim'"); return false; }
        const std::string vt = m_lex.Consume().text;
        if      (ICEq(vt,"default")||ICEq(vt,"normal")) { d.enumVal=(int)VerticalTrim::Default;        return true; }
        else if (ICEq(vt,"cap-to-baseline"))            { d.enumVal=(int)VerticalTrim::CapToBaseline;  return true; }
        Error("Unknown -interstice-vertical-trim '"+vt+"'"); return false;
    }
    if (ICEq(p,"-interstice-vertical-text-align"))
    {
        // Vertical position of the text (+ icon) block in its rect; `middle` = `center`.
        if      (ICEq(v,"start")||ICEq(v,"top"))             { d.enumVal=(int)TextAlign::Start;  return true; }
        else if (ICEq(v,"center")||ICEq(v,"middle"))         { d.enumVal=(int)TextAlign::Center; return true; }
        else if (ICEq(v,"end")||ICEq(v,"bottom"))            { d.enumVal=(int)TextAlign::End;    return true; }
        Error("Unknown -interstice-vertical-text-align '"+v+"'"); return false;
    }
    if (ICEq(p,"text-wrap"))
    {
        if      (ICEq(v,"nowrap"))    { d.enumVal=(int)TextWrap::NoWrap;   return true; }
        else if (ICEq(v,"wrap"))      { d.enumVal=(int)TextWrap::Wrap;     return true; }
        else if (ICEq(v,"word-char")) { d.enumVal=(int)TextWrap::WordChar; return true; }
        Error("Unknown text-wrap '"+v+"'"); return false;
    }
    if (ICEq(p,"text-overflow"))
    {
        if      (ICEq(v,"clip"))     { d.enumVal=(int)TextOverflow::Clip;     return true; }
        else if (ICEq(v,"ellipsis")) { d.enumVal=(int)TextOverflow::Ellipsis; return true; }
        Error("Unknown text-overflow '"+v+"'"); return false;
    }
    if (ICEq(p,"direction"))
    {
        if      (ICEq(v,"auto"))                         { d.enumVal=(int)TextDirection::Auto;        return true; }
        else if (ICEq(v,"initial"))                      { d.enumVal=(int)TextDirection::Auto;        return true; }
        else if (ICEq(v,"ltr"))     { d.enumVal=(int)TextDirection::LeftToRight; return true; }
        else if (ICEq(v,"rtl"))     { d.enumVal=(int)TextDirection::RightToLeft; return true; }
        Error("Unknown direction '"+v+"'"); return false;
    }
    if (ICEq(p,"-interstice-text-baseline") || ICEq(p,"dominant-baseline") || ICEq(p,"vertical-align"))
    {
        if      (ICEq(v,"auto"))         { d.enumVal=(int)TextBaseline::Alphabetic;   return true; }
        else if (ICEq(v,"baseline")||ICEq(v,"alphabetic")) { d.enumVal=(int)TextBaseline::Alphabetic; return true; }
        else if (ICEq(v,"ideographic"))  { d.enumVal=(int)TextBaseline::Ideographic;  return true; }
        else if (ICEq(v,"central"))      { d.enumVal=(int)TextBaseline::Central;       return true; }
        else if (ICEq(v,"hanging"))      { d.enumVal=(int)TextBaseline::Hanging;       return true; }
        else if (ICEq(v,"mathematical")) { d.enumVal=(int)TextBaseline::Mathematical;  return true; }
        else if (ICEq(v,"middle"))       { d.enumVal=(int)TextBaseline::Middle;        return true; }
        else if (ICEq(v,"text-top"))     { d.enumVal=(int)TextBaseline::TextTop;       return true; }
        else if (ICEq(v,"text-bottom"))  { d.enumVal=(int)TextBaseline::TextBottom;    return true; }
        Error("Unknown "+p+" '"+v+"'"); return false;
    }
    if (ICEq(p,"font-weight"))
    {
        if      (ICEq(v,"normal"))                                              { d.enumVal=(int)FontWeight::Normal;     return true; }
        else if (ICEq(v,"thin"))                                                { d.enumVal=(int)FontWeight::Thin;       return true; }
        else if (ICEq(v,"extra-light")||ICEq(v,"ultralight"))                   { d.enumVal=(int)FontWeight::ExtraLight; return true; }
        else if (ICEq(v,"light"))                                               { d.enumVal=(int)FontWeight::Light;      return true; }
        else if (ICEq(v,"medium"))                                              { d.enumVal=(int)FontWeight::Medium;     return true; }
        else if (ICEq(v,"demi-bold")||ICEq(v,"semi-bold")||ICEq(v,"semibold"))  { d.enumVal=(int)FontWeight::DemiBold;   return true; }
        else if (ICEq(v,"bold"))                                                { d.enumVal=(int)FontWeight::Bold;       return true; }
        else if (ICEq(v,"extra-bold")||ICEq(v,"ultrabold"))                     { d.enumVal=(int)FontWeight::ExtraBold;  return true; }
        else if (ICEq(v,"black")||ICEq(v,"heavy"))                              { d.enumVal=(int)FontWeight::Black;      return true; }
        else if (ICEq(v,"extra-black")||ICEq(v,"ultra-black"))                  { d.enumVal=(int)FontWeight::ExtraBlack; return true; }
        Error("Unknown font-weight '"+v+"'"); return false;
    }
    if (ICEq(p,"font-style"))
    {
        if      (ICEq(v,"normal"))  { d.enumVal=(int)FontStyle::Normal;  return true; }
        else if (ICEq(v,"italic"))  { d.enumVal=(int)FontStyle::Italic;  return true; }
        else if (ICEq(v,"oblique")) { d.enumVal=(int)FontStyle::Oblique; return true; }
        Error("Unknown font-style '"+v+"'"); return false;
    }
    if (ICEq(p,"font-stretch"))
    {
        if      (ICEq(v,"normal"))           { d.enumVal=(int)FontStretch::Normal;         return true; }
        else if (ICEq(v,"ultra-condensed"))  { d.enumVal=(int)FontStretch::UltraCondensed; return true; }
        else if (ICEq(v,"extra-condensed"))  { d.enumVal=(int)FontStretch::ExtraCondensed; return true; }
        else if (ICEq(v,"condensed"))        { d.enumVal=(int)FontStretch::Condensed;      return true; }
        else if (ICEq(v,"semi-condensed"))   { d.enumVal=(int)FontStretch::SemiCondensed;  return true; }
        else if (ICEq(v,"semi-expanded"))    { d.enumVal=(int)FontStretch::SemiExpanded;   return true; }
        else if (ICEq(v,"expanded"))         { d.enumVal=(int)FontStretch::Expanded;       return true; }
        else if (ICEq(v,"extra-expanded"))   { d.enumVal=(int)FontStretch::ExtraExpanded;  return true; }
        else if (ICEq(v,"ultra-expanded"))   { d.enumVal=(int)FontStretch::UltraExpanded;  return true; }
        Error("Unknown font-stretch '"+v+"'"); return false;
    }
    if (ICEq(p,"-interstice-icon-side"))
    {
        if      (ICEq(v,"left"))   { d.enumVal=(int)IconSide::Left;   return true; }
        else if (ICEq(v,"right"))  { d.enumVal=(int)IconSide::Right;  return true; }
        else if (ICEq(v,"top"))    { d.enumVal=(int)IconSide::Top;    return true; }
        else if (ICEq(v,"bottom")) { d.enumVal=(int)IconSide::Bottom; return true; }
        Error("Unknown -interstice-icon-side '"+v+"'"); return false;
    }
    if (ICEq(p,"text-underline-style")   || ICEq(p,"text-overline-style") ||
        ICEq(p,"text-throughline-style") || ICEq(p,"text-bottomline-style"))
    {
        if      (ICEq(v,"solid"))  { d.enumVal=(int)DecorationStyle::Solid;  return true; }
        else if (ICEq(v,"double")) { d.enumVal=(int)DecorationStyle::Double; return true; }
        else if (ICEq(v,"dotted")) { d.enumVal=(int)DecorationStyle::Dotted; return true; }
        else if (ICEq(v,"dashed")) { d.enumVal=(int)DecorationStyle::Dashed; return true; }
        else if (ICEq(v,"wavy"))   { d.enumVal=(int)DecorationStyle::Wavy;   return true; }
        Error("Unknown decoration style '"+v+"'"); return false;
    }

    // --- justify-items (grid-only) -------------------------------------------
    if (ICEq(p,"justify-items"))
    {
        if      (ICEq(v,"flex-start")||ICEq(v,"start")) { d.enumVal=(int)JustifyContent::FlexStart;    return true; }
        else if (ICEq(v,"flex-end")||ICEq(v,"end"))     { d.enumVal=(int)JustifyContent::FlexEnd;      return true; }
        else if (ICEq(v,"center"))                      { d.enumVal=(int)JustifyContent::Center;       return true; }
        else if (ICEq(v,"space-between"))               { d.enumVal=(int)JustifyContent::SpaceBetween; return true; }
        else if (ICEq(v,"space-around"))                { d.enumVal=(int)JustifyContent::SpaceAround;  return true; }
        else if (ICEq(v,"space-evenly"))                { d.enumVal=(int)JustifyContent::SpaceEvenly;  return true; }
        else if (ICEq(v,"stretch"))                     { d.enumVal=kJustifyItemsStretch;              return true; }
        Error("Unknown justify-items '"+v+"'"); return false;
    }

    Error("Unknown property '" + p + "'");
    return false;
}

// ============================================================================
// Parser::ApplyDecl  (out-of-class definition)
// ============================================================================

void Parser::ApplyDecl(Style& s, int si, const StyleDecl& d)
{
    if (si < 0 || si >= Style::kStateCount) si = 0;
    const std::string& p = d.prop;

    // Per-state visual
    if (ICEq(p,"background-color"))    { s.states[si].backgroundColor = d.colorStyleVal; return; }
    if (ICEq(p,"border-color"))        { s.states[si].SetBorderColor(d.colorStyleVal); return; }
    if (ICEq(p,"border-top-color"))    { s.states[si].borderColors[0] = d.colorStyleVal; return; }
    if (ICEq(p,"border-right-color"))  { s.states[si].borderColors[1] = d.colorStyleVal; return; }
    if (ICEq(p,"border-bottom-color")) { s.states[si].borderColors[2] = d.colorStyleVal; return; }
    if (ICEq(p,"border-left-color"))   { s.states[si].borderColors[3] = d.colorStyleVal; return; }
    if (ICEq(p,"-interstice-icon-color")) { s.states[si].iconColor = d.colorStyleVal; return; }
    if (ICEq(p,"color"))
    {
        s.states[si].textColor       = d.colorStyleVal;
        s.states[si].textStyle.color = d.color4bVal;
        return;
    }

    // Per-state text style
    if (ICEq(p,"font-size"))       { s.states[si].textStyle.fontSize      = d.fVal;                return; }
    if (ICEq(p,"font-weight"))     { s.states[si].textStyle.weight        = (FontWeight)d.enumVal; return; }
    if (ICEq(p,"font-style"))      { s.states[si].textStyle.style         = (FontStyle)d.enumVal;  return; }
    if (ICEq(p,"font-family"))     { s.states[si].textStyle.family        = (FontFamily)d.enumVal;
                                     s.states[si].textStyle.fontId        = d.fontIdVal;           return; }
    if (ICEq(p,"-interstice-language")){ s.states[si].textStyle.language      = d.languageVal;         return; }
    if (ICEq(p,"font-stretch"))    { s.states[si].textStyle.stretch       = (FontStretch)d.enumVal;return; }
    if (ICEq(p,"line-height"))     { s.states[si].textStyle.lineHeight    = d.fVal;
                                     s.states[si].textStyle.lineHeightType = (LineHeight)d.enumVal; return; }
    if (ICEq(p,"letter-spacing"))  { s.states[si].textStyle.letterSpacing = d.fVal;                return; }
    if (ICEq(p,"word-spacing"))    { s.states[si].textStyle.wordSpacing   = d.fVal;                return; }

    // Text decorations
    auto setDecoC = [&](TextDecoration& dec){ dec.color     = d.color4bVal; };
    auto setDecoT = [&](TextDecoration& dec){ dec.thickness = (uint16_t)std::clamp((int)d.fVal,0,65535); };
    auto setDecoO = [&](TextDecoration& dec){ dec.offset    = (uint16_t)std::clamp((int)d.fVal,0,65535); };
    auto setDecoS = [&](TextDecoration& dec){ dec.style     = (DecorationStyle)d.enumVal; };

    if (ICEq(p,"text-underline-color"))       { setDecoC(s.states[si].textStyle.underLine);   return; }
    if (ICEq(p,"text-underline-thickness"))   { setDecoT(s.states[si].textStyle.underLine);   return; }
    if (ICEq(p,"text-underline-offset"))      { setDecoO(s.states[si].textStyle.underLine);   return; }
    if (ICEq(p,"text-underline-style"))       { setDecoS(s.states[si].textStyle.underLine);   return; }
    if (ICEq(p,"text-overline-color"))        { setDecoC(s.states[si].textStyle.overLine);    return; }
    if (ICEq(p,"text-overline-thickness"))    { setDecoT(s.states[si].textStyle.overLine);    return; }
    if (ICEq(p,"text-overline-offset"))       { setDecoO(s.states[si].textStyle.overLine);    return; }
    if (ICEq(p,"text-overline-style"))        { setDecoS(s.states[si].textStyle.overLine);    return; }
    if (ICEq(p,"text-throughline-color"))     { setDecoC(s.states[si].textStyle.throughLine); return; }
    if (ICEq(p,"text-throughline-thickness")) { setDecoT(s.states[si].textStyle.throughLine); return; }
    if (ICEq(p,"text-throughline-offset"))    { setDecoO(s.states[si].textStyle.throughLine); return; }
    if (ICEq(p,"text-throughline-style"))     { setDecoS(s.states[si].textStyle.throughLine); return; }
    if (ICEq(p,"text-bottomline-color"))      { setDecoC(s.states[si].textStyle.bottomLine);  return; }
    if (ICEq(p,"text-bottomline-thickness"))  { setDecoT(s.states[si].textStyle.bottomLine);  return; }
    if (ICEq(p,"text-bottomline-offset"))     { setDecoO(s.states[si].textStyle.bottomLine);  return; }
    if (ICEq(p,"text-bottomline-style"))      { setDecoS(s.states[si].textStyle.bottomLine);  return; }

    // Layout (state-independent)
    if (ICEq(p,"margin"))        { s.margin       = d.edges;   return; }
    if (ICEq(p,"padding"))       { s.padding      = d.edges;   return; }
    if (ICEq(p,"border-width"))  { s.borderWidths = d.edges;   return; }
    if (ICEq(p,"border-radius")) { s.borderRadius = d.corners; return; }

    if (ICEq(p,"margin-top"))    { s.margin.top    = d.uVal; return; }
    if (ICEq(p,"margin-right"))  { s.margin.right  = d.uVal; return; }
    if (ICEq(p,"margin-bottom")) { s.margin.bottom = d.uVal; return; }
    if (ICEq(p,"margin-left"))   { s.margin.left   = d.uVal; return; }

    if (ICEq(p,"padding-top"))    { s.padding.top    = d.uVal; return; }
    if (ICEq(p,"padding-right"))  { s.padding.right  = d.uVal; return; }
    if (ICEq(p,"padding-bottom")) { s.padding.bottom = d.uVal; return; }
    if (ICEq(p,"padding-left"))   { s.padding.left   = d.uVal; return; }

    if (ICEq(p,"border-top-width"))    { s.borderWidths.top    = d.uVal; return; }
    if (ICEq(p,"border-right-width"))  { s.borderWidths.right  = d.uVal; return; }
    if (ICEq(p,"border-bottom-width")) { s.borderWidths.bottom = d.uVal; return; }
    if (ICEq(p,"border-left-width"))   { s.borderWidths.left   = d.uVal; return; }

    if (ICEq(p,"border-top-left-radius"))     { s.borderRadius.topLeft     = d.uVal; return; }
    if (ICEq(p,"border-top-right-radius"))    { s.borderRadius.topRight    = d.uVal; return; }
    if (ICEq(p,"border-bottom-right-radius")) { s.borderRadius.bottomRight = d.uVal; return; }
    if (ICEq(p,"border-bottom-left-radius"))  { s.borderRadius.bottomLeft  = d.uVal; return; }

    if (ICEq(p,"-interstice-border-dash-size"))   { s.borderDashSize   = d.fVal; return; }
    if (ICEq(p,"-interstice-border-dash-offset")) { s.borderDashOffset = d.fVal; return; }
    if (ICEq(p,"border-style"))
    {
        if      (d.enumVal == 0) { s.borderDashSize = 0; s.borderDashOffset = 0; } // solid
        else if (d.enumVal == 1) { s.borderDashSize = 6; s.borderDashOffset = 0; } // dashed
        else if (d.enumVal == 2) { s.borderDashSize = 2; s.borderDashOffset = 0; } // dotted
        return;
    }

    if (ICEq(p,"break-after"))
    {
        if      (d.enumVal == 0) s.breakBehavior = BreakBehavior::Auto;
        else if (d.enumVal == 1) s.breakBehavior = BreakBehavior::Always;  // always / right
        else if (d.enumVal == 2) s.breakBehavior = BreakBehavior::Before;  // left
        else if (d.enumVal == 3) s.breakBehavior = BreakBehavior::Avoid;   // avoid
        return;
    }
    if (ICEq(p,"-interstice-clamp-scroll"))   { s.clampScroll  = (bool)d.enumVal; return; }

    if (ICEq(p,"display"))    { s.display    = (Display)d.enumVal;     return; }
    // `overflow` sets BOTH axes; `overflow-x` / `overflow-y` target one.
    if (ICEq(p,"overflow"))   { s.overflowX  = (Overflow)d.enumVal; s.overflowY = (Overflow)d.enumVal; return; }
    if (ICEq(p,"overflow-x")) { s.overflowX  = (Overflow)d.enumVal;    return; }
    if (ICEq(p,"overflow-y")) { s.overflowY  = (Overflow)d.enumVal;    return; }
    if (ICEq(p,"visibility")) { s.visibility = (Visibility)d.enumVal;  return; }
    if (ICEq(p,"cursor"))     { s.cursor     = (MouseCursor)d.enumVal; return; }
    if (ICEq(p,"-interstice-icon-side"))  { s.iconSide   = (IconSide)d.enumVal;    return; }

    if (ICEq(p,"-interstice-icon-text-gap"))
    {
        s.iconTextGap = (uint16_t)std::clamp((int)d.fVal, 0, 65535); return;
    }

    if (ICEq(p,"-interstice-icon-size"))
    {
        s.iconSize = (uint16_t)std::clamp((int)d.fVal, 0, 65535); return;
    }

    if (ICEq(p,"-interstice-icon-offset-y"))
    {
        s.iconOffsetY = (int16_t)std::clamp((int)d.fVal, -32768, 32767); return;
    }

    // Grid container properties
    if (ICEq(p,"column-gap"))    { s.layoutData.grid.column.gap = (int16_t)std::clamp(d.iVal, 0, 32767); return; }
    if (ICEq(p,"row-gap"))       { s.layoutData.grid.row.gap    = (int16_t)std::clamp(d.iVal, 0, 32767); return; }
    // Stretch fills the cell (the default); anything else places the child in it.
    if (ICEq(p,"justify-items"))
    {
        auto& grid = s.layoutData.grid;
        if (d.enumVal == kJustifyItemsStretch) { grid.alignItemsSelf = AlignItems::Stretch; return; }
        grid.alignItemsSelf = AlignItems::FlexStart;
        grid.justifyItems   = (JustifyContent)d.enumVal;
        return;
    }

    // Grid item properties
    if (ICEq(p,"-interstice-grid-column-track")) { s.layoutData.grid.column.track = d.gridTrackVal; return; }
    if (ICEq(p,"-interstice-grid-row-track"))    { s.layoutData.grid.row.track    = d.gridTrackVal; return; }
    if (ICEq(p,"grid-column-start")) { s.layoutData.grid.column.start = (int16_t)d.iVal; return; }
    if (ICEq(p,"grid-row-start"))    { s.layoutData.grid.row.start    = (int16_t)d.iVal; return; }
    if (ICEq(p,"grid-column"))
    {
        if (d.iVal != -1)            s.layoutData.grid.column.start = (int16_t)d.iVal;
        if (d.hasSecondNumber)       s.layoutData.grid.column.span  = (int16_t)std::max(1, (int)d.uVal);
        return;
    }
    if (ICEq(p,"grid-row"))
    {
        if (d.iVal != -1)            s.layoutData.grid.row.start = (int16_t)d.iVal;
        if (d.hasSecondNumber)       s.layoutData.grid.row.span  = (int16_t)std::max(1, (int)d.uVal);
        return;
    }

    if (ICEq(p,"flex-direction"))  { s.layoutData.flex.direction      = (FlexDirection)d.enumVal;  return; }
    if (ICEq(p,"flex-wrap"))       { s.layoutData.flex.wrap           = (FlexWrap)d.enumVal;        return; }
    // align-items / align-self / justify-content apply to both flex and grid
    if (ICEq(p,"align-items"))     { s.layoutData.flex.alignItems     = (AlignItems)d.enumVal;
                                     s.layoutData.grid.alignItems     = (AlignItems)d.enumVal;      return; }
    if (ICEq(p,"align-self"))      { s.alignSelf = (AlignItems)d.enumVal; return; }
    if (ICEq(p,"justify-content")) { s.layoutData.flex.justifyContent = (JustifyContent)d.enumVal;
                                     s.layoutData.grid.justifyContent = (JustifyContent)d.enumVal;  return; }
    if (ICEq(p,"align-content"))   { s.layoutData.flex.alignContent   = (AlignContent)d.enumVal;    return; }
    if (ICEq(p,"flex-basis"))      { s.layoutData.flex.flexBasis      = (int16_t)d.iVal;            return; }

    // CSS size properties
    if (ICEq(p,"width"))      { s.layoutData.width     = d.iVal; return; }
    if (ICEq(p,"height"))     { s.layoutData.height    = d.iVal; return; }
    if (ICEq(p,"min-width"))  { s.layoutData.minWidth  = d.iVal; return; }
    if (ICEq(p,"max-width"))  { s.layoutData.maxWidth  = d.iVal; return; }
    if (ICEq(p,"min-height")) { s.layoutData.minHeight = d.iVal; return; }
    if (ICEq(p,"max-height")) { s.layoutData.maxHeight = d.iVal; return; }

    if (ICEq(p,"gap"))         { s.layoutData.flex.gap        = (uint16_t)std::clamp((int)d.fVal,0,65535); return; }
    if (ICEq(p,"flex-grow"))   { s.layoutData.flex.flexGrow   = (uint16_t)std::clamp((int)d.fVal, 0, 65535); return; }
    if (ICEq(p,"flex-shrink")) { s.layoutData.flex.flexShrink = (uint16_t)std::clamp((int)d.fVal, 0, 65535); return; }
    if (ICEq(p,"flex"))
    {
        s.layoutData.flex.flexGrow   = (uint16_t)std::clamp((int)d.fVal, 0, 65535);
        s.layoutData.flex.flexShrink = (uint16_t)std::clamp((int)d.uVal2, 0, 65535);
        s.layoutData.flex.flexBasis  = (int16_t)d.iVal;
        return;
    }

    if (ICEq(p,"text-align"))                 { s.textLayoutStyle.horizontalAlignment = (TextAlign)d.enumVal;  return; }
    if (ICEq(p,"-interstice-vertical-text-align")){ s.textLayoutStyle.verticalAlignment   = (TextAlign)d.enumVal;  return; }
    if (ICEq(p,"-interstice-vertical-trim"))      { s.textLayoutStyle.verticalTrim        = (VerticalTrim)d.enumVal; return; }
    if (ICEq(p,"text-wrap"))               { s.textLayoutStyle.textWrapping        = (TextWrap)d.enumVal;      return; }
    if (ICEq(p,"text-overflow"))           { s.textLayoutStyle.textOverflow        = (TextOverflow)d.enumVal;  return; }
    if (ICEq(p,"direction"))               { s.textLayoutStyle.textDirection       = (TextDirection)d.enumVal; return; }
    if (ICEq(p,"-interstice-text-baseline") || ICEq(p,"dominant-baseline") || ICEq(p,"vertical-align"))
                                           { s.textLayoutStyle.baseline            = (TextBaseline)d.enumVal;  return; }
}
}
