#pragma once

// Check for C++20 support
#if __cplusplus >= 202002L|| (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#define TACHYON_SUPPORT_CPP20
#else
#undef TACHYON_SUPPORT_CPP20
#endif

// Feature detection for C++20 iterator concepts / ranges
#if defined(_HAS_CXX20) || defined(__cpp_lib_ranges) || (__cplusplus >= 202002L)
#define TACHYON_HAS_CPP20_ITERATORS 1
#else
#define TACHYON_HAS_CPP20_ITERATORS 0
#endif

#ifdef TACHYON_SUPPORT_CPP20
#include <source_location>
#endif