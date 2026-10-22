#pragma once

// Check for C++20 support
#if __cplusplus >= 202002L|| (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#define SUPPORT_CPP20
#else
#undef SUPPORT_CPP20
#endif

// Feature detection for C++20 iterator concepts / ranges
#if defined(_HAS_CXX20) || defined(__cpp_lib_ranges) || (__cplusplus >= 202002L)
#define GUI_HAS_CPP20_ITERATORS 1
#else
#define GUI_HAS_CPP20_ITERATORS 0
#endif

#ifdef SUPPORT_CPP20
#include <source_location>
#endif