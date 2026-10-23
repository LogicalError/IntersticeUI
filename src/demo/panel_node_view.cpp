#include "application.h"

//
// Demo panel for the node view (RenderNodeView, controls_node_view.cpp). The
// graph data is built in application.cpp.
//

namespace interstice
{

    void Application::RenderNodeViewPanel()
    {
        RenderNodeView(demo.nodeView);
    }

}
