#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>

#include <gui_core/gui_id.h>
#include <gui_core/gui_base_types.h>
#include <gui_core/text/gui_text_system.h>   // IconID

// Plain state holders for several controls (no frame callbacks or logic).

namespace GUI
{
    // In-place rename of a tree / list / grid row (F2, Ctrl+Shift+Click or a slow re-click);
    // the target row emits a text field instead of its label. One rename at a time.
    class RenameSubsystem
    {
    public:
        GUI::ID      targetID   = GUI::ID::Invalid();
        bool         isActive   = false;
        bool         firstFrame = false; // skip stale pending_text_committed on activation frame
        std::string* editBuffer = nullptr;

        void Start(GUI::ID id, std::string* buffer, std::string_view text)
        {
            targetID = id; isActive = true; firstFrame = true;
            editBuffer = buffer; *buffer = text;
        }
        void Cancel()
        {
            targetID = GUI::ID::Invalid(); isActive = false; firstFrame = false; editBuffer = nullptr;
        }
    };

    // Slider track widths written by the sliders' custom draws and read next frame
    // (keyboard step sizes, drag mapping).
    class SliderFieldTrackSubsystem
    {
    public:
        // 0 until the slider has drawn once.
        int GetWidth(GUI::ID id) const
        {
            auto it = m_widths.find(id);
            return (it != m_widths.end()) ? it->second : 0;
        }

        // For the custom-draw lambdas to write into.
        std::unordered_map<GUI::ID, int>& Widths() { return m_widths; }

    private:
        std::unordered_map<GUI::ID, int> m_widths;
    };

    // Set when a dock tab drag starts, so any target leaf can emit a phantom tab during
    // construction. Cleared at frame start when no drag is active.
    struct DockTabDragInfo
    {
        GUI::ID    panelID  = GUI::ID::Invalid();
        std::string title;
        IconID     icon     = IconID::Invalid;
        int        tabWidth = 0; // width of the dragged tab from originalRects
    };

    // The drop-zone overlay shown while a dock tab is dragged; it slides between zones.
    struct DockDropZoneAnim
    {
        bool    hasTarget = false;  // was previous frame showing a drop zone overlay?
        IntRect currentRect{};      // interpolated rect (what we render)
        IntRect targetRect{};       // destination rect
        std::chrono::steady_clock::time_point lastTime{};
    };

    // Used directly by controls_docking.cpp.
    class DockTabDragSubsystem
    {
    public:
        DockTabDragInfo&  Info()         { return m_info; }
        DockDropZoneAnim& DropZoneAnim() { return m_dropZoneAnim; }

    private:
        DockTabDragInfo  m_info{};
        DockDropZoneAnim m_dropZoneAnim{};
    };

    struct ToolbarDockState;

    // A floating toolbar window dragged by its thumb; one drag at a time.
    // currentMousePos is read by the construction-time drop visualization.
    class ToolbarWindowDragSubsystem
    {
    public:
        bool              active          = false;
        ID                windowID        = ID::Invalid();
        int2              startMousePos {};
        int2              currentMousePos {};
        IntRect           startRect {};
        ToolbarDockState* sourceDockState = nullptr;
    };

    // Last frame's screen rects of menu triggers and popups, written by AddPostLayout
    // callbacks. BeginMenu sizes the popup from them (trigger width when it opens
    // above/below, content width when flipped to the side).
    class MenuRectsSubsystem
    {
    public:
        // nullptr if absent.
        const GUI::IntRect* FindTriggerRect(GUI::ID id) const
        {
            auto it = m_triggerRects.find(id);
            return (it == m_triggerRects.end()) ? nullptr : &it->second;
        }
        const GUI::IntRect* FindWindowRect(GUI::ID id) const
        {
            auto it = m_windowRects.find(id);
            return (it == m_windowRects.end()) ? nullptr : &it->second;
        }

        // For the AddPostLayout lambdas to write into.
        std::unordered_map<GUI::ID, GUI::IntRect>& TriggerRects() { return m_triggerRects; }
        std::unordered_map<GUI::ID, GUI::IntRect>& WindowRects()  { return m_windowRects; }

    private:
        std::unordered_map<GUI::ID, GUI::IntRect> m_triggerRects;
        std::unordered_map<GUI::ID, GUI::IntRect> m_windowRects;
    };
}
