#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>

#include <cstdlib>
#include <string_view>

static constexpr interstice::Diagnostics::LogCategory kLogDemo = interstice::Diagnostics::MakeLogCategory("demo");

//
// This project is for testing and prototyping GUI features, so we can iterate on them quickly without worrying about organization / structure / etc. 
//

int main(int argc, char* argv[])
{
    interstice::Application* application = new interstice::Application();

    // --ui-scale <factor>: zoom the UI on top of the OS display scale (e.g. 1.5).
    // --theme <palette>: start with another palette, e.g. themes/light.css.
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--ui-scale")
            application->SetUIScale(std::strtof(argv[i + 1], nullptr));
        else if (std::string_view(argv[i]) == "--theme")
            application->RequestReloadTheme(argv[i + 1], application->GetGlobalFontScale());
    }

    int returnCode = EXIT_FAILURE;
    try
    {
        if (application->Initialize())
        {
            bool runResult = application->Run();
            application->Cleanup();
            if (runResult)
            {
				returnCode = EXIT_SUCCESS;
			} else
			{
				INTERSTICE_LOG_ERROR(kLogDemo, "Application::Run failed");
				fflush(stderr);
            }
        } else
        {
            INTERSTICE_LOG_ERROR(kLogDemo, "Application::Initialize failed");
            fflush(stderr);
        }
    }
    catch (const std::exception& e)
    {
        INTERSTICE_LOG_CRITICAL(kLogDemo, "Unhandled exception: {}", e.what());
        fflush(stderr);
        returnCode = EXIT_FAILURE;
    }
	delete application;
    return returnCode;
}

