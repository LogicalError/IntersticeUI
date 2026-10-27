#pragma once

namespace tachyon
{
    /// @addtogroup layout
    /// @{

    class ScrollManagement;
    class ElementStorage;
    class OSWindowManager;
    class ITextSystem;

    /// Computes element sizes and positions from styles, content and constraints, in five
    /// passes run after construction:
    ///
    ///     1. Horizontal sizing      - bottom-up natural widths
    ///     2. Vertical sizing        - bottom-up natural heights (wrap rows approximate)
    ///     3. Horizontal positioning - top-down flex distribution, X positions
    ///     4. Vertical sizing        - natural heights again, with final widths
    ///     5. Vertical positioning   - top-down flex distribution, Y positions
    ///
    /// Flex grow / shrink happens in the positioning passes, where the parent's final size
    /// is known; it cascades from ancestor to descendant.
    class LayoutEngine
    {
    public:
        explicit LayoutEngine(ITextSystem& textSystem, 
                              ScrollManagement& scrollManagement) noexcept
            : m_textSystem(textSystem)
            , m_scrollManagement(scrollManagement)
        {
        }        

        /// Lays out every element in `storage`; call after EndConstruction.
        void Layout(OSWindowManager& osWindowManager, ElementStorage& storage);

    private:
        ITextSystem& m_textSystem;
        ScrollManagement& m_scrollManagement;
        
        void HorizontalSizingPass(ElementStorage& storage);
        void VerticalSizingPass(ElementStorage& storage);
        void HorizontalPositioningPass(OSWindowManager& osWindowManager, ElementStorage& storage);
        void VerticalPositioningPass(OSWindowManager& osWindowManager, ElementStorage& storage);
    };
    /// @}
}

