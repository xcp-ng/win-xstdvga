// SPDX-License-Identifier: MS-PL

// Based on the Microsoft KMDOD example
// Copyright (c) 2010 Microsoft Corporation
// Copyright 2026 Vates.

#pragma once

#define BDD_LOG_ERROR(...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__)

// Warnings
#define BDD_LOG_WARNING(...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, __VA_ARGS__)

// Events (i.e. low-frequency tracing)
#define BDD_LOG_EVENT(...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_TRACE_LEVEL, __VA_ARGS__)

// Information (i.e. high-frequency tracing)
#define BDD_LOG_INFORMATION(...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL, __VA_ARGS__)

// Low resource logging macros.
#define BDD_LOG_LOW_RESOURCE(...) DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_WARNING_LEVEL, __VA_ARGS__)

// Assertion logging macros.
#define BDD_LOG_ASSERTION(...) \
    do { \
        DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__); \
        NT_ASSERT(FALSE); \
    } while (0)
#define BDD_ASSERT(exp) \
    { \
        if (!(exp)) { \
            BDD_LOG_ASSERTION(#exp); \
        } \
    }

#if DBG
#define BDD_ASSERT_CHK(exp) BDD_ASSERT(exp)
#else
#define BDD_ASSERT_CHK(exp) \
    do { \
        _Analysis_assume_(exp); \
    } while (0)
#endif
