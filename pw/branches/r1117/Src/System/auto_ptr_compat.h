#pragma once
// std::auto_ptr exists only up to C++11 and is removed in C++17 (MSVC removes it
// when /std:c++17 is on — the macro _HAS_CXX17 tells us; libstdc++ removes it at
// -std=c++17). The reference toolchain of the Windows client is still VS2008,
// where std::unique_ptr does not exist, so NV_AUTO_PTR(T) keeps one spelling in
// the code and picks the right template per toolchain.
//
// It is a macro, not an alias template, on purpose: alias templates (using X = )
// are C++11 and the VS2008 reference compiler does not have them.
//
// Sites using it only do: construct from a raw pointer, ->, .get(), and assign
// from a temporary — all of that std::unique_ptr supports. The difference
// (auto_ptr copies silently steal ownership, unique_ptr forbids copying) is a
// feature here: an accidental ownership transfer now fails to compile.

#include <memory>

#if (defined(_HAS_CXX17) && _HAS_CXX17) || (defined(__cplusplus) && __cplusplus >= 201703L)
#  define NV_AUTO_PTR(T) std::unique_ptr<T>
#else
#  define NV_AUTO_PTR(T) std::auto_ptr<T>
#endif
