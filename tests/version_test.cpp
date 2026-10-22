#include <gtest/gtest.h>

#include <gui_core/gui_version.h>

#include <string>

// The version header is generated from the CMake project version.
TEST(Version, ComponentsMatchTheString)
{
    const std::string fromParts = std::to_string(INTERSTICE_VERSION_MAJOR) + "." +
                                  std::to_string(INTERSTICE_VERSION_MINOR) + "." +
                                  std::to_string(INTERSTICE_VERSION_PATCH);
    EXPECT_EQ(fromParts, INTERSTICE_VERSION_STRING);
}
