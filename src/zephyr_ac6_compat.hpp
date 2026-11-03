// Copyright 2026 Arm Limited and/or its affiliates.
// SPDX-License-Identifier: Apache-2.0
#pragma once

// AC6 libc++ uses these errno values in std::errc enumerators. Zephyr's
// armstdc errno.c supplies exactly these values, but the AC6 header exposes
// them as extern const int in AEABI portability mode, not constant expressions.
// Keep portability mode (and Zephyr's thread-local errno) for all other APIs.
#include <cerrno>
#undef EDOM
#undef ERANGE
#undef EILSEQ
#define EDOM 33
#define ERANGE 34
#define EILSEQ 138

// libc++ likewise needs constant LC_* masks. Use the native Arm locale
// constants from the installed header, without changing other C library ABIs.
#pragma push_macro("_AEABI_PORTABILITY_LEVEL")
#undef _AEABI_PORTABILITY_LEVEL
#define _AEABI_PORTABILITY_LEVEL 0
#include <clocale>
#pragma pop_macro("_AEABI_PORTABILITY_LEVEL")
