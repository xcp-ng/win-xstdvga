// SPDX-License-Identifier: BSD-2-Clause

// Copyright 2026 Vates.

#pragma once

static consteval bool BddStringLiteralContains(PCSTR Haystack, PCSTR Needle) {
    if (!*Needle)
        return true;
    for (; *Haystack; ++Haystack) {
        PCSTR h = Haystack;
        PCSTR n = Needle;
        while (*h && *n && *h == *n) {
            ++h;
            ++n;
        }
        if (!*n)
            return true;
    }
    return false;
}

#if DBG
#define BDD_LOG_SAFE(level, fmt, ...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, level, fmt "\n", __VA_ARGS__)
#else
#define BDD_LOG_SAFE(level, fmt, ...) \
    if constexpr (!BddStringLiteralContains(fmt, "%p")) { \
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, level, fmt "\n", __VA_ARGS__); \
    }
#endif

#define BDD_LOG_ERROR(fmt, ...) BDD_LOG_SAFE(DPFLTR_ERROR_LEVEL, fmt, __VA_ARGS__)
#define BDD_LOG_WARNING(fmt, ...) BDD_LOG_SAFE(DPFLTR_WARNING_LEVEL, fmt, __VA_ARGS__)
#define BDD_LOG_TRACE(fmt, ...) BDD_LOG_SAFE(DPFLTR_TRACE_LEVEL, fmt, __VA_ARGS__)
#define BDD_LOG_INFO(fmt, ...) BDD_LOG_SAFE(DPFLTR_INFO_LEVEL, fmt, __VA_ARGS__)

#define BDD_LOG_ASSERTION(fmt, ...) \
    do { \
        BDD_LOG_ERROR(fmt, __VA_ARGS__); \
        NT_ASSERT(FALSE); \
    } while (0)
#define BDD_ASSERT(exp) \
    do { \
        if (!(exp)) { \
            BDD_LOG_ASSERTION(#exp); \
        } \
    } while (0)

#if DBG
#define BDD_ASSERT_CHK(exp) BDD_ASSERT(exp)
#else
#define BDD_ASSERT_CHK(exp) _Analysis_assume_(exp)
#endif
