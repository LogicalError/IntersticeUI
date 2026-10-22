#include "application.h"

#include <gui_core/diagnostics/gui_logging.h>

#include <cstdlib>
#include <string_view>

static constexpr GUI::Diagnostics::LogCategory kLogDemo = GUI::Diagnostics::MakeLogCategory("demo");

//
// This project is for testing and prototyping GUI features, so we can iterate on them quickly without worrying about organization / structure / etc. 
//

int main(int argc, char* argv[])
{
    GUI::Application* application = new GUI::Application();

    // --ui-scale <factor>: zoom the UI on top of the OS display scale (e.g. 1.5).
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--ui-scale")
            application->SetUIScale(std::strtof(argv[i + 1], nullptr));

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
				LOG_ERROR(kLogDemo, "Application::Run failed");
				fflush(stderr);
            }
        } else
        {
            LOG_ERROR(kLogDemo, "Application::Initialize failed");
            fflush(stderr);
        }
    }
    catch (const std::exception& e)
    {
        LOG_CRITICAL(kLogDemo, "Unhandled exception: {}", e.what());
        fflush(stderr);
        returnCode = EXIT_FAILURE;
    }
	delete application;
    return returnCode;
}

