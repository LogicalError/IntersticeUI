#include "my_app.h"

#include <cstdio>

// [main]
int main()
{
    my_app::MyApp app;
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
        return 1;
    }
    return 0;
}
// [main]
