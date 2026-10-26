#pragma once

#include <gui_core/style/gui_style_sheet.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// CSS lexer + parser internals, shared by gui_style_sheet.cpp (top-level API)
// and gui_style_sheet_properties.cpp (per-property value parsing + apply).

namespace interstice {

// ============================================================================
// Lexer
// ============================================================================

enum class TT
{
    Ident, Number, Hash, StringLit, AtKeyword,
    Dot, Star, Colon, Semicolon, Comma,
    OpenBrace, CloseBrace, OpenParen, CloseParen,
    Percent, Slash,
    Eof, Unknown
};

struct Token
{
    TT          type = TT::Eof;
    std::string text;
    int         line = 1;
    int         col  = 1;
};

class Lexer
{
public:
    explicit Lexer(std::string_view src) : m_src(src) {}

    const Token& Peek()
    {
        if (!m_peeked) { m_peekBuf = ReadNext(); m_peeked = true; }
        return m_peekBuf;
    }

    Token Consume()
    {
        if (m_peeked) { m_peeked = false; return std::move(m_peekBuf); }
        return ReadNext();
    }

    bool Consume(TT t) { if (Peek().type == t) { Consume(); return true; } return false; }

    bool Check(TT t) { return Peek().type == t; }
    int  Line()      { return Peek().line; }
    int  Col()       { return Peek().col; }

private:
    std::string_view m_src;
    size_t           m_pos    = 0;
    int              m_line   = 1;
    int              m_col    = 1;
    bool             m_peeked = false;
    Token            m_peekBuf;

    char Cur()  const { return m_pos     < m_src.size() ? m_src[m_pos]     : '\0'; }
    char Next() const { return m_pos + 1 < m_src.size() ? m_src[m_pos + 1] : '\0'; }

    char Advance()
    {
        char c = Cur(); ++m_pos;
        if (c == '\n') { ++m_line; m_col = 1; } else ++m_col;
        return c;
    }

    void SkipWS()
    {
        for (;;)
        {
            if (std::isspace((unsigned char)Cur())) { Advance(); continue; }
            if (Cur() == '/' && Next() == '*')
            {
                Advance(); Advance();
                while (m_pos < m_src.size())
                    if (Cur() == '*' && Next() == '/') { Advance(); Advance(); break; }
                    else Advance();
                continue;
            }
            if (Cur() == '/' && Next() == '/')
            {
                while (m_pos < m_src.size() && Cur() != '\n') Advance();
                continue;
            }
            break;
        }
    }

    Token ReadNext()
    {
        SkipWS();
        if (m_pos >= m_src.size()) return { TT::Eof, "", m_line, m_col };

        int sl = m_line, sc = m_col;
        char c = Cur();

        switch (c)
        {
        case '{': Advance(); return { TT::OpenBrace,  "{", sl, sc };
        case '}': Advance(); return { TT::CloseBrace, "}", sl, sc };
        case '(': Advance(); return { TT::OpenParen,  "(", sl, sc };
        case ')': Advance(); return { TT::CloseParen, ")", sl, sc };
        case ':': Advance(); return { TT::Colon,      ":", sl, sc };
        case ';': Advance(); return { TT::Semicolon,  ";", sl, sc };
        case ',': Advance(); return { TT::Comma,      ",", sl, sc };
        case '.':
            if (std::isdigit((unsigned char)Next())) break; // .35: a number
            Advance(); return { TT::Dot, ".", sl, sc };
        case '*': Advance(); return { TT::Star,       "*", sl, sc };
        case '%': Advance(); return { TT::Percent,    "%", sl, sc };
        case '/': Advance(); return { TT::Slash,      "/", sl, sc };
        }

        if (c == '#')
        {
            Advance();
            std::string t;
            while (std::isalnum((unsigned char)Cur()) || Cur() == '_' || Cur() == '-')
                t += Advance();
            return { TT::Hash, std::move(t), sl, sc };
        }

        if (c == '@')
        {
            Advance();
            std::string t;
            while (std::isalnum((unsigned char)Cur()) || Cur() == '-' || Cur() == '_')
                t += Advance();
            return { TT::AtKeyword, std::move(t), sl, sc };
        }

        if (c == '"' || c == '\'')
        {
            char d = Advance();
            std::string t;
            while (m_pos < m_src.size() && Cur() != d) t += Advance();
            if (m_pos < m_src.size()) Advance();
            return { TT::StringLit, std::move(t), sl, sc };
        }

        if (std::isdigit((unsigned char)c) || ((c == '-' || c == '.') && std::isdigit((unsigned char)Next())))
        {
            std::string t;
            if (c == '-') t += Advance();
            if (Cur() == '.') t += '0';
            while (std::isdigit((unsigned char)Cur())) t += Advance();
            if (Cur() == '.') { t += Advance(); while (std::isdigit((unsigned char)Cur())) t += Advance(); }
            return { TT::Number, std::move(t), sl, sc };
        }

        if (std::isalpha((unsigned char)c) || c == '_' || c == '-')
        {
            std::string t;
            while (std::isalnum((unsigned char)Cur()) || Cur() == '-' || Cur() == '_')
                t += Advance();
            return { TT::Ident, std::move(t), sl, sc };
        }

        std::string s(1, c); Advance();
        return { TT::Unknown, std::move(s), sl, sc };
    }
};

// ============================================================================
// Helpers
// ============================================================================

inline bool ICEq(const std::string& a, const char* b)
{
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)a[i]) != (unsigned char)b[i]) return false;
    return true;
}

inline int HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// ============================================================================
// StyleDecl - flat parsed declaration value (file-scope so all methods see it)
// ============================================================================

struct StyleDecl
{
    std::string prop;

    color4b    color4bVal    {};
    ColorStyle colorStyleVal {};
    bool       isColorStyle  = false;

    BoxEdges   edges   {};
    BoxCorners corners {};

    float     fVal         = 0.0f;
    int32_t   iVal         = 0;
    uint16_t  uVal         = 0;
    int32_t   enumVal      = 0;
    FontID      fontIdVal    = FontID::Invalid;
    LanguageTag languageVal;
    GridTrack gridTrackVal = {};
    BoxShadow shadowVal[kMaxBoxShadows] {};
    // Optional second integer value for properties that accept two numbers (e.g., flex-grow: <main> <cross>)
    uint16_t uVal2 = 0;
    bool hasSecondNumber = false;
};

// ============================================================================
// Parser
// ============================================================================

class Parser
{
public:
    explicit Parser(Lexer& lex) : m_lex(lex) {}

    StyleSheet Parse()
    {
        while (!m_lex.Check(TT::Eof))
        {
            if (m_lex.Check(TT::Colon))
            {
                if (!ParseRootBlock()) SkipToNextRule();
            }
            else if (m_lex.Check(TT::AtKeyword))
            {
                if (!ParseAtRule()) SkipToNextRule();
            }
            else
            {
                if (!ParseRule()) SkipToNextRule();
            }
        }
        return std::move(m_result);
    }

private:
    Lexer&     m_lex;
    StyleSheet m_result;

    // ---- Error helpers -----------------------------------------------------

    void Error(const std::string& msg, int line = -1, int col = -1)
    {
        m_result.errors.push_back({ msg,
            line < 0 ? m_lex.Line() : line,
            col  < 0 ? m_lex.Col()  : col });
    }

    bool Expect(TT t, const char* desc)
    {
        if (m_lex.Consume(t)) return true;
        const Token& tok = m_lex.Peek();
        std::ostringstream ss;
        ss << "Expected " << desc << ", got '" << tok.text << "'";
        Error(ss.str(), tok.line, tok.col);
        return false;
    }

    // ---- Numbers -----------------------------------------------------------
    // Never throw: a missing or out-of-range number is reported and reads as 0.

    static constexpr float kMaxNumber = 1.0e9f;

    float ToFloat(const Token& tok)
    {
        float v = 0.0f;
        const char* end = tok.text.data() + tok.text.size();
        const auto [ptr, ec] = std::from_chars(tok.text.data(), end, v);
        if (tok.type != TT::Number || ec != std::errc{} || !(std::fabs(v) <= kMaxNumber))
        {
            Error("Invalid number '" + tok.text + "'", tok.line, tok.col);
            return 0.0f;
        }
        return v;
    }

    float ConsumeFloat() { return ToFloat(m_lex.Consume()); }
    int   ConsumeInt()   { return static_cast<int>(ConsumeFloat()); }

    // ---- Recovery ----------------------------------------------------------

    void SkipToNextRule()
    {
        while (!m_lex.Check(TT::Eof) && !m_lex.Check(TT::Dot) && !m_lex.Check(TT::Star) &&
               !m_lex.Check(TT::Colon) && !m_lex.Check(TT::AtKeyword))
        {
            if (m_lex.Consume(TT::OpenBrace))
            {
                int depth = 1;
                while (!m_lex.Check(TT::Eof) && depth > 0)
                {
                    if      (m_lex.Consume(TT::OpenBrace))  ++depth;
                    else if (m_lex.Consume(TT::CloseBrace)) --depth;
                    else    m_lex.Consume();
                }
                return;
            }
            m_lex.Consume();
        }
    }

    void SkipDecl()
    {
        while (!m_lex.Check(TT::Eof) &&
               !m_lex.Check(TT::Semicolon) &&
               !m_lex.Check(TT::CloseBrace))
            m_lex.Consume();
        m_lex.Consume(TT::Semicolon);
    }

    // A style first named after a `*` rule starts from what that rule set.
    Style& GetOrCreate(const std::string& name)
    {
        return m_result.styles.try_emplace(name, m_result.baseStyle).first->second;
    }

    // ---- :root block -------------------------------------------------------

    bool ParseRootBlock()
    {
        m_lex.Consume(TT::Colon); // consume ':'
        if (!m_lex.Check(TT::Ident) || !ICEq(m_lex.Peek().text, "root"))
        {
            Error("Expected 'root' after ':'");
            return false;
        }
        m_lex.Consume(); // consume 'root'
        if (!Expect(TT::OpenBrace, "'{'")) return false;

        while (!m_lex.Check(TT::CloseBrace) && !m_lex.Check(TT::Eof))
        {
            if (!m_lex.Check(TT::Ident))
            {
                m_lex.Consume();
                continue;
            }

            Token propTok = m_lex.Consume();
            const std::string& name = propTok.text;

            if (name.size() < 3 || name[0] != '-' || name[1] != '-')
            {
                Error("Expected custom property (--name) in :root block, got '" + name + "'", propTok.line, propTok.col);
                SkipDecl();
                continue;
            }

            if (!Expect(TT::Colon, "':'")) { SkipDecl(); continue; }

            // `none` is a box-shadow value.
            if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "none"))
            {
                m_lex.Consume();
                m_lex.Consume(TT::Semicolon);
                m_result.customProperties[name] = CustomProperty{ .hasShadow = true };
                continue;
            }

            // Try 1..4 numbers (optional px/em) first; multi-value vars feed shorthands like border-width.
            // Numbers followed by a color are a box-shadow.
            if (m_lex.Check(TT::Number))
            {
                CustomProperty prop{};
                float values[4] {};
                while (prop.dimCount < 4 && m_lex.Check(TT::Number))
                {
                    float val = ConsumeFloat();
                    if (m_lex.Check(TT::Ident) && (ICEq(m_lex.Peek().text,"px") || ICEq(m_lex.Peek().text,"em")))
                        m_lex.Consume(); // consume unit
                    values[prop.dimCount] = val;
                    prop.dims[prop.dimCount++] = (uint16_t)std::clamp((int)val, 0, 65535);
                }
                if (!m_lex.Check(TT::Semicolon) && !m_lex.Check(TT::CloseBrace))
                {
                    const int lengthCount = prop.dimCount;
                    prop = CustomProperty{ .hasShadow = true };
                    if (!ParseShadowList(prop.shadows, values, lengthCount)) { SkipDecl(); continue; }
                    m_lex.Consume(TT::Semicolon);
                    m_result.customProperties[name] = prop;
                    continue;
                }
                m_lex.Consume(TT::Semicolon);
                prop.number    = prop.dims[0];
                prop.hasNumber = (prop.dimCount >= 1);
                m_result.customProperties[name] = prop;
                continue;
            }

            // Otherwise parse as color/gradient
            StyleDecl d;
            d.prop = name;
            if (!ParseColorStyleValue(d)) { SkipDecl(); continue; }
            m_lex.Consume(TT::Semicolon);

            m_result.customProperties[name] = CustomProperty {
                .color      = d.color4bVal,
                .colorStyle = d.colorStyleVal,
                .hasColor   = true
            };
        }
        Expect(TT::CloseBrace, "'}'");
        return true;
    }

    // ---- @font-face ---------------------------------------------------------

    bool ParseAtRule()
    {
        const Token at = m_lex.Consume();
        if (!ICEq(at.text, "font-face"))
        {
            Error("Unknown at-rule '@" + at.text + "'", at.line, at.col);
            return false;
        }
        if (!Expect(TT::OpenBrace, "'{'")) return false;

        FontFace face;
        while (!m_lex.Check(TT::CloseBrace) && !m_lex.Check(TT::Eof))
        {
            if (!m_lex.Check(TT::Ident)) { m_lex.Consume(); continue; }
            Token descTok = m_lex.Consume();
            if (!Expect(TT::Colon, "':'")) { SkipDecl(); continue; }

            bool ok = false;
            if      (ICEq(descTok.text, "font-family")) ok = ParseFamilyName(face.family);
            else if (ICEq(descTok.text, "src"))         ok = ParseFontSrc(face.src);
            else Error("Unsupported @font-face descriptor '" + descTok.text + "'", descTok.line, descTok.col);
            if (!ok) { SkipDecl(); continue; }
            m_lex.Consume(TT::Semicolon);
        }
        Expect(TT::CloseBrace, "'}'");

        if (face.family.empty() || face.src.empty())
            Error("@font-face needs both 'font-family' and 'src'", at.line, at.col);
        else
            m_result.fontFaces.push_back(std::move(face));
        return true;
    }

    // A quoted string, or unquoted words joined by single spaces (CSS family-name).
    bool ParseFamilyName(std::string& out)
    {
        if (m_lex.Check(TT::StringLit)) { out = m_lex.Consume().text; return true; }
        if (!m_lex.Check(TT::Ident))    { Error("Expected font family name"); return false; }
        out = m_lex.Consume().text;
        while (m_lex.Check(TT::Ident))
            out += ' ' + m_lex.Consume().text;
        return true;
    }

    // url("path") or "path", optionally followed by format(...), which is ignored.
    bool ParseFontSrc(std::string& out)
    {
        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "url"))
        {
            m_lex.Consume();
            if (!Expect(TT::OpenParen, "'('")) return false;
            if (!m_lex.Check(TT::StringLit)) { Error("Expected a quoted path in url()"); return false; }
            out = m_lex.Consume().text;
            if (!Expect(TT::CloseParen, "')'")) return false;
        }
        else if (m_lex.Check(TT::StringLit))
            out = m_lex.Consume().text;
        else
        {
            Error("Expected url(\"path\") for 'src'");
            return false;
        }

        if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "format"))
        {
            m_lex.Consume();
            if (!Expect(TT::OpenParen, "'('")) return false;
            while (!m_lex.Check(TT::CloseParen) && !m_lex.Check(TT::Eof)) m_lex.Consume();
            if (!Expect(TT::CloseParen, "')'")) return false;
        }
        if (m_lex.Check(TT::Comma)) { Error("Only one source per @font-face is supported"); return false; }
        return true;
    }

    static bool GenericFontFamily(const std::string& name, FontFamily& out)
    {
        if      (ICEq(name, "default"))    out = FontFamily::Default;
        else if (ICEq(name, "emoji"))      out = FontFamily::Emoji;
        else if (ICEq(name, "sans-serif")) out = FontFamily::SansSerif;
        else if (ICEq(name, "serif"))      out = FontFamily::Serif;
        else if (ICEq(name, "monospace"))  out = FontFamily::Monospace;
        else if (ICEq(name, "math"))       out = FontFamily::Math;
        else return false;
        return true;
    }

    // font-family: a comma-separated list. The first named family becomes the
    // font; the first generic keyword is used while that family isn't registered.
    bool ParseFontFamilyList(StyleDecl& d)
    {
        bool hasGeneric = false;
        d.enumVal   = (int)FontFamily::Default;
        d.fontIdVal = FontID::Invalid;
        do
        {
            const bool quoted = m_lex.Check(TT::StringLit);
            std::string name;
            if (!ParseFamilyName(name)) return false;

            FontFamily generic {};
            if (!quoted && GenericFontFamily(name, generic))
            {
                if (!hasGeneric) { d.enumVal = (int)generic; hasGeneric = true; }
            }
            else if (d.fontIdVal == FontID::Invalid)
                d.fontIdVal = FontIDFromName(name);
        } while (m_lex.Consume(TT::Comma));
        return true;
    }

    // ---- Rule --------------------------------------------------------------

    bool ParseRule()
    {
        struct Selector { std::string name; int stateIndex; int notMask = 0; bool focusVisible = false; };
        std::vector<Selector> selectors;

        do {
            Selector sel;
            sel.stateIndex = 0;
            if (m_lex.Consume(TT::Star))
                sel.name = "*";
            else
            {
                m_lex.Consume(TT::Dot);
                if (!m_lex.Check(TT::Ident)) { Error("Expected style name"); return false; }
                sel.name = m_lex.Consume().text;
            }

            while (m_lex.Check(TT::Colon))
            {
                m_lex.Consume();

                // Double-colon pseudo-element (e.g., ::selection, ::highlight(selected))
                if (m_lex.Check(TT::Colon))
                {
                    m_lex.Consume();
                    if (!m_lex.Check(TT::Ident)) { Error("Expected pseudo-element name after '::'"); break; }
                    std::string pseudo = m_lex.Consume().text;
                    if (ICEq(pseudo, "selection"))
                        sel.stateIndex |= Style::kSelection;
                    else if (ICEq(pseudo, "highlight"))
                    {
                        if (!m_lex.Consume(TT::OpenParen)) { Error("Expected '(' after '::highlight'"); continue; }
                        if (!m_lex.Check(TT::Ident))       { Error("Expected highlight name"); m_lex.Consume(TT::CloseParen); continue; }
                        std::string name = m_lex.Consume().text;
                        if (ICEq(name, "selected"))
                            sel.stateIndex |= Style::kSelected;
                        else
                            Error("Unknown highlight name '" + name + "'");
                        if (!m_lex.Consume(TT::CloseParen)) Error("Expected ')' after '::highlight(" + name + "'");
                    }
                    else
                        Error("Unknown pseudo-element '::" + pseudo + "'");
                    continue;
                }

                if (!m_lex.Check(TT::Ident)) { Error("Expected pseudo-state name"); break; }
                std::string pseudo = m_lex.Consume().text;

                // :focus-visible sets the focus ring, shown while focus is from the keyboard.
                if (ICEq(pseudo, "focus-visible"))
                {
                    sel.focusVisible = true;
                    continue;
                }

                // :not(:pseudo) negation
                if (ICEq(pseudo, "not"))
                {
                    if (!m_lex.Consume(TT::OpenParen)) { Error("Expected '(' after ':not'"); break; }
                    if (!m_lex.Consume(TT::Colon))     { Error("Expected ':' inside ':not()'"); m_lex.Consume(TT::CloseParen); break; }
                    if (!m_lex.Check(TT::Ident))       { Error("Expected pseudo-state inside ':not()'"); m_lex.Consume(TT::CloseParen); break; }
                    std::string inner = m_lex.Consume().text;
                    int bit = PseudoToBit(inner);
                    if (bit < 0 || bit >= Style::kDisabled)
                        Error("Cannot negate ':" + inner + "' — only :hover, :active, :focus are supported in :not()");
                    else
                        sel.notMask |= bit;
                    if (!m_lex.Consume(TT::CloseParen)) Error("Expected ')' after ':not(:" + inner + "'");
                    continue;
                }

                int bit = PseudoToBit(pseudo);
                if (bit < 0) Error("Unknown pseudo-state ':" + pseudo + "'");
                else if (bit == Style::kDisabled)
                    sel.stateIndex = bit; // exclusive special state, cannot combine
                else if (sel.stateIndex < Style::kDisabled)
                    sel.stateIndex |= bit;
            }
            selectors.push_back(std::move(sel));
        } while (m_lex.Consume(TT::Comma));

        if (!Expect(TT::OpenBrace, "'{'")) return false;

        std::vector<StyleDecl> decls;
        while (!m_lex.Check(TT::CloseBrace) && !m_lex.Check(TT::Eof))
        {
            if (!m_lex.Check(TT::Ident)) { m_lex.Consume(); continue; }
            Token propTok = m_lex.Consume();
            if (!Expect(TT::Colon, "':'")) { SkipDecl(); continue; }

            StyleDecl d;
            d.prop = propTok.text;
            if (!ParseDeclValue(d)) { SkipDecl(); continue; }
            m_lex.Consume(TT::Semicolon);
            decls.push_back(std::move(d));
        }
        Expect(TT::CloseBrace, "'}'");

        for (const auto& sel : selectors)
        {
            if (!sel.focusVisible)
                for (const StyleDecl& d : decls)
                    if (ICEq(d.prop, "box-shadow") && (d.shadowVal[0].inset || d.shadowVal[1].inset))
                        Error("An inset box-shadow is only supported in ':focus-visible'");
            if (sel.focusVisible)
            {
                if (sel.stateIndex != 0 || sel.notMask != 0)
                    Error("':focus-visible' can't be combined with other states");
                else if (sel.name == "*")
                {
                    ApplyFocusVisible(m_result.baseStyle, decls);
                    for (auto& [name, style] : m_result.styles)
                        ApplyFocusVisible(style, decls);
                }
                else
                    ApplyFocusVisible(GetOrCreate(sel.name), decls);
                continue;
            }
            // `*` applies to every style so far and to the base later ones start from.
            if (sel.name == "*")
            {
                ApplyDecls(m_result.baseStyle, sel.stateIndex, sel.notMask, decls);
                for (auto& [name, style] : m_result.styles)
                    ApplyDecls(style, sel.stateIndex, sel.notMask, decls);
            }
            else
                ApplyDecls(GetOrCreate(sel.name), sel.stateIndex, sel.notMask, decls);
        }
        return true;
    }

    void ApplyDecls(Style& style, int stateIndex, int notMask, const std::vector<StyleDecl>& decls)
    {
        for (const StyleDecl& d : decls)
        {
            if (stateIndex == Style::kDisabled)
            {
                // Disabled cannot be combined with other states
                ApplyDecl(style, Style::kDisabled, d);
            }
            else
            {
                // Apply to every state slot whose bits are a superset of the selector.
                // e.g. :hover         -> slots 2,3,6,7,10,11,14,15 (all with hover bit set)
                //     :hover:selected -> slots 10,11,14,15
                //     ::selection     -> slots 16..31 (all selection-modifier slots)
                //     :focus::selection -> slots 20,21,22,23,28,29,30,31
                // Ordering in the CSS file is what controls priority.
                for (int i = 0; i < Style::kDisabled; ++i)
                {
                    if ((i & stateIndex) == stateIndex && (i & notMask) == 0)
                    {
                        ApplyDecl(style, i, d);
                    }
                }
                // The disabled slot has no other state bits: a rule without any
                // (the base rule, or a :not(...)) applies to it as well.
                if (stateIndex == 0)
                    ApplyDecl(style, Style::kDisabled, d);
            }
        }
    }

    // :focus-visible holds one box-shadow, the focus ring.
    void ApplyFocusVisible(Style& style, const std::vector<StyleDecl>& decls)
    {
        for (const StyleDecl& d : decls)
        {
            if (!ICEq(d.prop, "box-shadow"))
                Error("Only box-shadow is supported in ':focus-visible', not '" + d.prop + "'");
            else if (!d.shadowVal[1].IsNone())
                Error("The ':focus-visible' box-shadow takes one layer");
            else if (d.shadowVal[0].inset && (d.shadowVal[0].x != 0 || d.shadowVal[0].y != 0 ||
                                              d.shadowVal[0].blur != 0 || d.shadowVal[0].spread <= 0))
                Error("An inset focus ring is an outline: inset 0 0 0 <width> <color>");
            else
                style.focusRing = d.shadowVal[0];
        }
    }

    static int PseudoToBit(const std::string& p)
    {
        if (ICEq(p, "normal"))    return 0;
        if (ICEq(p, "active"))    return Style::kActive;
        if (ICEq(p, "hover"))     return Style::kHover;
        if (ICEq(p, "focus"))     return Style::kFocus;
        if (ICEq(p, "disabled"))  return Style::kDisabled;
        if (ICEq(p, "selected"))  return Style::kSelected;
        return -1;
    }

    // ---- Color helpers (called from ParseDeclValue) ------------------------

    bool ParseHexColor(const std::string& hex, color4b& out)
    {
        auto h = [](char c) { return HexDigit(c); };
        switch (hex.size())
        {
        case 3:
            out = { (byte)(h(hex[0])*17), (byte)(h(hex[1])*17),
                    (byte)(h(hex[2])*17), 255 };
            return true;
        case 4:
            out = { (byte)(h(hex[0])*17), (byte)(h(hex[1])*17),
                    (byte)(h(hex[2])*17), (byte)(h(hex[3])*17) };
            return true;
        case 6:
            out = { (byte)(h(hex[0])*16+h(hex[1])), (byte)(h(hex[2])*16+h(hex[3])),
                    (byte)(h(hex[4])*16+h(hex[5])), 255 };
            return true;
        case 8:
            out = { (byte)(h(hex[0])*16+h(hex[1])), (byte)(h(hex[2])*16+h(hex[3])),
                    (byte)(h(hex[4])*16+h(hex[5])), (byte)(h(hex[6])*16+h(hex[7])) };
            return true;
        }
        Error("Invalid hex color '#" + hex + "'");
        return false;
    }

    static bool NamedColor(const std::string& name, color4b& out)
    {
        struct NC { const char* n; uint8_t r, g, b, a; };
        static const NC kT[] = {
            { "transparent", 0,   0,   0,   0   }, { "white",    255, 255, 255, 255 },
            { "black",       0,   0,   0,   255 }, { "red",      255, 0,   0,   255 },
            { "green",       0,   128, 0,   255 }, { "lime",     0,   255, 0,   255 },
            { "blue",        0,   0,   255, 255 }, { "yellow",   255, 255, 0,   255 },
            { "cyan",        0,   255, 255, 255 }, { "aqua",     0,   255, 255, 255 },
            { "magenta",     255, 0,   255, 255 }, { "fuchsia",  255, 0,   255, 255 },
            { "orange",      255, 165, 0,   255 }, { "purple",   128, 0,   128, 255 },
            { "gray",        128, 128, 128, 255 }, { "grey",     128, 128, 128, 255 },
            { "silver",      192, 192, 192, 255 }, { "navy",     0,   0,   128, 255 },
            { "teal",        0,   128, 128, 255 }, { "maroon",   128, 0,   0,   255 },
            { "olive",       128, 128, 0,   255 }, { "coral",    255, 127, 80,  255 },
            { "salmon",      250, 128, 114, 255 }, { "gold",     255, 215, 0,   255 },
            { "pink",        255, 192, 203, 255 }, { "brown",    165, 42,  42,  255 },
            { "indigo",      75,  0,   130, 255 }, { "violet",   238, 130, 238, 255 },
            { "crimson",     220, 20,  60,  255 }, { "darkgray", 169, 169, 169, 255 },
            { "darkgrey",    169, 169, 169, 255 }, { "lightgray",211, 211, 211, 255 },
            { "lightgrey",   211, 211, 211, 255 },
        };
        for (const auto& nc : kT)
            if (ICEq(name, nc.n)) { out = { nc.r, nc.g, nc.b, nc.a }; return true; }
        return false;
    }

    bool ParseColorValue(color4b& out)
    {
        if (m_lex.Check(TT::Hash))
            return ParseHexColor(m_lex.Consume().text, out);

        if (m_lex.Check(TT::Ident))
        {
            const std::string fn = m_lex.Peek().text;
            if (ICEq(fn, "rgb") || ICEq(fn, "rgba"))
            {
                bool alpha = ICEq(fn, "rgba");
                m_lex.Consume();
                if (!Expect(TT::OpenParen, "'('")) return false;
                if (!m_lex.Check(TT::Number)) { Error("Expected number in rgb()"); return false; }
                int r = ConsumeInt(); m_lex.Consume(TT::Comma);
                int g = ConsumeInt(); m_lex.Consume(TT::Comma);
                int b = ConsumeInt();
                int a = 255;
                if (alpha)
                {
                    m_lex.Consume(TT::Comma);
                    float fa = ConsumeFloat();
                    a = (fa <= 1.0f) ? (int)(fa * 255.0f + 0.5f) : (int)fa;
                }
                if (!Expect(TT::CloseParen, "')'")) return false;
                out = { (byte)std::clamp(r,0,255), (byte)std::clamp(g,0,255),
                        (byte)std::clamp(b,0,255), (byte)std::clamp(a,0,255) };
                return true;
            }
            if (ICEq(fn, "var"))
            {
                m_lex.Consume(); // consume "var"
                if (!Expect(TT::OpenParen, "'('")) return false;
                if (!m_lex.Check(TT::Ident)) { Error("Expected variable name in var()"); return false; }
                std::string varName = m_lex.Consume().text;
                if (varName.size() < 3 || varName[0] != '-' || varName[1] != '-')
                {
                    Error("Variable name must start with '--': " + varName);
                    return false;
                }
                if (!Expect(TT::CloseParen, "')'")) return false;
                auto it = m_result.customProperties.find(varName);
                if (it == m_result.customProperties.end())
                {
                    Error("Undefined variable '" + varName + "'");
                    return false;
                }
                out = it->second.color;
                return true;
            }
            std::string nm = m_lex.Consume().text;
            if (NamedColor(nm, out)) return true;
            Error("Unknown color '" + nm + "'");
            return false;
        }
        Error("Expected color value");
        return false;
    }

    bool ParseColorStops(Gradient& grad)
    {
        // Track which stops have explicit offsets for later distribution
        bool hasExplicitOffset[Gradient::kMaxStops] = {};

        for (int idx = 0; !m_lex.Check(TT::CloseParen) && !m_lex.Check(TT::Eof); ++idx)
        {
            if (idx > 0 && !m_lex.Consume(TT::Comma)) break;
            if (m_lex.Check(TT::CloseParen)) break;

            color4b col {};
            if (!ParseColorValue(col)) return false;

            float offset = -1.0f; // sentinel: no explicit offset
            bool explicit_offset = false;
            if (m_lex.Check(TT::Number))
            {
                offset = ConsumeFloat() / 100.0f;
                m_lex.Consume(TT::Percent);
                explicit_offset = true;
            }
            if (grad.stopCount < Gradient::kMaxStops)
            {
                int stopIdx = grad.stopCount;
                grad.AddStop(col, offset >= 0.0f ? offset : 0.0f);
                hasExplicitOffset[stopIdx] = explicit_offset;
            }
        }

        // Distribute offsets for stops without explicit positions per CSS spec:
        // First stop defaults to 0%, last to 100%, intermediates are evenly distributed
        if (grad.stopCount > 0)
        {
            if (!hasExplicitOffset[0])
                grad.stops[0].SetOffset(0.0f);
            if (grad.stopCount > 1 && !hasExplicitOffset[grad.stopCount - 1])
                grad.stops[grad.stopCount - 1].SetOffset(1.0f);

            // Distribute intermediate stops without explicit offsets
            for (int i = 1; i < grad.stopCount - 1; ++i)
            {
                if (hasExplicitOffset[i]) continue;

                // Find next stop with explicit offset
                int nextExplicit = i + 1;
                while (nextExplicit < grad.stopCount - 1 && !hasExplicitOffset[nextExplicit])
                    ++nextExplicit;

                // If last stop, it has been given 100%
                float prevOffset = grad.stops[i - 1].GetOffset();
                float nextOffset = grad.stops[nextExplicit].GetOffset();
                int span = nextExplicit - (i - 1);
                for (int j = i; j < nextExplicit; ++j)
                {
                    grad.stops[j].SetOffset(prevOffset + (nextOffset - prevOffset) * float(j - (i - 1)) / float(span));
                }
                i = nextExplicit - 1; // skip distributed stops
            }
        }
        return true;
    }

    bool ParseColorStyleValue(StyleDecl& d)
    {
        if (m_lex.Check(TT::Ident))
        {
            const std::string fn = m_lex.Peek().text;
            if (ICEq(fn, "var"))
            {
                m_lex.Consume(); // consume "var"
                if (!Expect(TT::OpenParen, "'('")) return false;
                if (!m_lex.Check(TT::Ident)) { Error("Expected variable name in var()"); return false; }
                std::string varName = m_lex.Consume().text;
                if (varName.size() < 3 || varName[0] != '-' || varName[1] != '-')
                {
                    Error("Variable name must start with '--': " + varName);
                    return false;
                }
                if (!Expect(TT::CloseParen, "')'")) return false;
                auto it = m_result.customProperties.find(varName);
                if (it == m_result.customProperties.end())
                {
                    Error("Undefined variable '" + varName + "'");
                    return false;
                }
                d.color4bVal    = it->second.color;
                d.colorStyleVal = it->second.colorStyle;
                d.isColorStyle  = true;
                return true;
            }
            if (ICEq(fn, "linear-gradient") || ICEq(fn, "radial-gradient"))
            {
                bool isLinear = ICEq(fn, "linear-gradient");
                m_lex.Consume();
                if (!Expect(TT::OpenParen, "'('")) return false;

                float angle = 0.0f;
                if (m_lex.Check(TT::Number))
                {
                    float maybeAngle = ToFloat(m_lex.Peek());
                    m_lex.Consume(); // consume number
                    if (m_lex.Check(TT::Ident))
                    {
                        // Degrees per unit: deg, grad (400 per turn), rad, turn.
                        const std::string unit = m_lex.Peek().text;
                        const float perUnit = ICEq(unit,"deg")  ? 1.0f
                                            : ICEq(unit,"grad") ? 0.9f
                                            : ICEq(unit,"rad")  ? 57.29577951f
                                            : ICEq(unit,"turn") ? 360.0f
                                            : 0.0f;
                        if (perUnit != 0.0f)
                        {
                            m_lex.Consume();
                            m_lex.Consume(TT::Comma);
                            angle = maybeAngle * perUnit;
                        }
                    }
                    else if (m_lex.Check(TT::Comma))
                    {
                        m_lex.Consume(TT::Comma);
                        angle = maybeAngle;
                    }
                }

                Gradient grad = isLinear ? Gradient::Linear(angle) : Gradient::Radial(angle);
                if (!ParseColorStops(grad)) return false;
                if (!Expect(TT::CloseParen, "')'")) return false;

                if (grad.stopCount > 0)
                {
                    color4b b4 = grad.stops[0].GetColor4b();
                    d.color4bVal = b4;
                }
                d.colorStyleVal.type     = isLinear ? ColorType::LinearGradient
                                                    : ColorType::RadialGradient;
                d.colorStyleVal.gradient = std::move(grad);
                d.isColorStyle           = true;
                return true;
            }
        }
        color4b col {};
        if (!ParseColorValue(col)) return false;
        d.color4bVal = col;
        d.colorStyleVal.type     = ColorType::SolidColor;
        d.colorStyleVal.gradient = Gradient::Solid(col);
        d.isColorStyle           = true;
        return true;
    }

    bool ParseDim(uint16_t& out)
    {
        if (m_lex.Check(TT::Number))
        {
            int v = (int)ConsumeFloat();
            if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "px")) m_lex.Consume();
            out = (uint16_t)std::clamp(v, 0, 65535);
            return true;
        }
        float f = 0;
        if (TryParseNumericVar(f)) { out = (uint16_t)std::clamp((int)f, 0, 65535); return true; }
        Error("Expected dimension"); return false;
    }

    // Try to parse var(--name) that resolves to a numeric value.
    // Returns false (without error) if the next token is not var().
    bool TryParseNumericVar(float& out)
    {
        if (!m_lex.Check(TT::Ident) || !ICEq(m_lex.Peek().text, "var")) return false;
        m_lex.Consume(); // consume "var"
        if (!Expect(TT::OpenParen, "'('")) return false;
        if (!m_lex.Check(TT::Ident)) { Error("Expected variable name in var()"); return false; }
        std::string varName = m_lex.Consume().text;
        if (varName.size() < 3 || varName[0] != '-' || varName[1] != '-')
        {
            Error("Variable name must start with '--': " + varName);
            return false;
        }
        if (!Expect(TT::CloseParen, "')'")) return false;
        auto it = m_result.customProperties.find(varName);
        if (it == m_result.customProperties.end())
        {
            Error("Undefined variable '" + varName + "'");
            return false;
        }
        if (!it->second.hasNumber)
        {
            Error("Variable '" + varName + "' is not a numeric value");
            return false;
        }
        if (it->second.dimCount > 1)
        {
            Error("Variable '" + varName + "' has " + std::to_string(it->second.dimCount)
                  + " values; expected a single dimension here");
            return false;
        }
        out = it->second.number;
        return true;
    }

    // True if the next token is a Number or a var() function call.
    bool CheckDimOrVar() const
    {
        return m_lex.Check(TT::Number)
            || (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "var"));
    }

    // Read 1..4 dimensions into v[]. A var() with dimCount > 1 is expanded into
    // multiple slots; a single number or single-value var fills one slot.
    bool ParseDimList(uint16_t v[4], int& n)
    {
        n = 0;
        while (n < 4 && CheckDimOrVar())
        {
            if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "var"))
            {
                m_lex.Consume(); // var
                if (!Expect(TT::OpenParen, "'('")) return false;
                if (!m_lex.Check(TT::Ident)) { Error("Expected variable name in var()"); return false; }
                std::string varName = m_lex.Consume().text;
                if (varName.size() < 3 || varName[0] != '-' || varName[1] != '-')
                {
                    Error("Variable name must start with '--': " + varName);
                    return false;
                }
                if (!Expect(TT::CloseParen, "')'")) return false;
                auto it = m_result.customProperties.find(varName);
                if (it == m_result.customProperties.end())
                {
                    Error("Undefined variable '" + varName + "'");
                    return false;
                }
                if (it->second.dimCount == 0)
                {
                    Error("Variable '" + varName + "' is not a numeric value");
                    return false;
                }
                for (int i = 0; i < it->second.dimCount && n < 4; ++i)
                    v[n++] = it->second.dims[i];
            }
            else
            {
                if (!ParseDim(v[n++])) return false;
            }
        }
        if (!n) { Error("Expected dimension"); return false; }
        return true;
    }

    bool ParseEdges(BoxEdges& out)
    {
        uint16_t v[4] {}; int n = 0;
        if (!ParseDimList(v, n)) return false;
        if      (n == 1) out = BoxEdges(v[0]);
        else if (n == 2) out = BoxEdges(v[0], v[1]);
        else if (n == 3) out = BoxEdges(v[0], v[1], v[2], v[1]);
        else             out = BoxEdges(v[0], v[1], v[2], v[3]);
        return true;
    }

    bool ParseCorners(BoxCorners& out)
    {
        uint16_t v[4] {}; int n = 0;
        if (!ParseDimList(v, n)) return false;
        if      (n == 1) out = BoxCorners(v[0]);
        else if (n == 2) out = BoxCorners(v[0], v[1], v[1], v[0]);
        else if (n == 3) out = BoxCorners(v[0], v[1], v[2], v[0]);
        else             out = BoxCorners(v[0], v[1], v[2], v[3]);
        return true;
    }

    // var(--name): its custom property, or nullptr (reported) when undefined.
    const CustomProperty* ConsumeVar()
    {
        m_lex.Consume(); // var
        if (!Expect(TT::OpenParen, "'('")) return nullptr;
        if (!m_lex.Check(TT::Ident)) { Error("Expected variable name in var()"); return nullptr; }
        const std::string varName = m_lex.Consume().text;
        if (!Expect(TT::CloseParen, "')'")) return nullptr;
        auto it = m_result.customProperties.find(varName);
        if (it == m_result.customProperties.end()) { Error("Undefined variable '" + varName + "'"); return nullptr; }
        return &it->second;
    }

    // box-shadow: none | <layer> [, <layer>]. A layer is [inset] <x> <y> [<blur> [<spread>]]
    // with a color before or after, or a var() holding shadows. `pre` holds lengths
    // of the first layer that were already read.
    bool ParseShadowList(BoxShadow (&out)[kMaxBoxShadows], const float* pre = nullptr, int preCount = 0)
    {
        for (BoxShadow& b : out) b = {};
        if (preCount == 0 && m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "none"))
        {
            m_lex.Consume();
            return true;
        }

        int count = 0;
        auto add = [&](const BoxShadow& b) {
            if (count == kMaxBoxShadows)
            {
                Error("box-shadow takes at most " + std::to_string(kMaxBoxShadows) + " layers");
                return false;
            }
            out[count++] = b;
            return true;
        };
        do
        {
            float len[4] {};
            int   n = 0;
            for (; n < preCount; ++n) len[n] = pre[n];
            preCount = 0;
            color4b color {};
            bool    hasColor = false;
            bool    fromVar  = false;
            bool    inset    = false;
            for (;;)
            {
                if (m_lex.Check(TT::Number))
                {
                    if (n == 4) { Error("box-shadow takes at most 4 lengths per layer"); return false; }
                    len[n++] = ConsumeFloat();
                    if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "px")) m_lex.Consume();
                }
                else if (!inset && m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "inset"))
                {
                    m_lex.Consume();
                    inset = true;
                }
                else if (m_lex.Check(TT::Ident) && ICEq(m_lex.Peek().text, "var"))
                {
                    const CustomProperty* v = ConsumeVar();
                    if (!v) return false;
                    if (v->hasShadow && n == 0 && !hasColor && !inset)
                    {
                        for (const BoxShadow& b : v->shadows)
                            if (!b.IsNone() && !add(b)) return false;
                        fromVar = true;
                        break;
                    }
                    if (!v->hasColor || hasColor) { Error("Expected a length or a color in box-shadow"); return false; }
                    color    = v->color;
                    hasColor = true;
                }
                else if (!hasColor && (m_lex.Check(TT::Hash) || m_lex.Check(TT::Ident)))
                {
                    if (!ParseColorValue(color)) return false;
                    hasColor = true;
                }
                else
                    break;
            }
            if (fromVar) continue;
            if (n < 2 || !hasColor) { Error("box-shadow needs an x and a y offset and a color"); return false; }
            if (len[2] < 0.0f)      { Error("box-shadow blur can't be negative"); return false; }

            auto i16 = [](float v) { return int16_t(std::clamp(int(std::lround(v)), -32768, 32767)); };
            BoxShadow b;
            b.x      = i16(len[0]);
            b.y      = i16(len[1]);
            b.blur   = uint16_t(i16(len[2]));
            b.spread = i16(len[3]);
            b.color  = color;
            b.inset  = inset;
            if (!b.IsNone() && !add(b)) return false;
        } while (m_lex.Consume(TT::Comma));
        return true;
    }

    // Parses a GridTrack value: auto | <number>[fr|px]
    bool ParseGridTrack(GridTrack& out)
    {
        if (m_lex.Check(TT::Ident))
        {
            if (!ICEq(m_lex.Peek().text, "auto")) { Error("Expected auto, Npx, or Nfr for grid track"); return false; }
            m_lex.Consume();
            out = GridTrack::Auto();
            return true;
        }
        if (!m_lex.Check(TT::Number)) { Error("Expected auto, Npx, or Nfr for grid track"); return false; }
        float val = ConsumeFloat();
        if (m_lex.Check(TT::Ident))
        {
            const std::string& unit = m_lex.Peek().text;
            if      (ICEq(unit, "fr")) { m_lex.Consume(); out = GridTrack::Fractional(val); return true; }
            else if (ICEq(unit, "px")) { m_lex.Consume(); out = GridTrack::Fixed(val);       return true; }
        }
        out = GridTrack::Fixed(val); // bare number -> pixels
        return true;
    }

    // ---- ParseDeclValue ----------------------------------------------------
    // Defined in gui_style_sheet_properties.cpp.

    bool ParseDeclValue(StyleDecl& d);

    // ---- ApplyDecl ---------------------------------------------------------
    // Defined in gui_style_sheet_properties.cpp.

    static void ApplyDecl(Style& s, int si, const StyleDecl& d);
};
}
