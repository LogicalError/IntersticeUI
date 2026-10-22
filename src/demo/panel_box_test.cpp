#include "application.h"

//
// This file is for testing box rendering and style features, 
// so we can quickly see if they work
//

namespace GUI
{

    void Application::RenderBoxTestPanel()
    {
        BeginHorizontalWrap();

        // wide boxes
        int2 size = { styleManager.Px(100), styleManager.Px(50) };
        for (int i = 0; i < demo.testBoxStyleCount; i++)
        {
            AddBox(size, boxStyleRefs[i]);
        }

        // border test box
        AddBox(size, borderTestStyleRef);

        // high boxes
        size = { styleManager.Px(50), styleManager.Px(100) };
        for (int i = 0; i < demo.testBoxStyleCount; i++)
        {
            AddBox(size, boxStyleRefs[i]);
        }

        // border test box
        AddBox(size, borderTestStyleRef);

        EndHorizontalWrap();
    }

}