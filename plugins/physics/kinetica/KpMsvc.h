#pragma once
// Force-included into kinetica-physics's own sources on MSVC only (CMakeLists.txt): the GCC and Clang builtin they call
// that MSVC lacks. A prefetch is a hint to the processor - without it the same results, a little slower.
#define __builtin_prefetch(...) ((void)0)
