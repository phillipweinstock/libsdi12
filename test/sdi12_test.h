/**
 * @file sdi12_test.h
 * @brief Zero-dependency, single-header test framework for libsdi12.
 *
 * Provides Unity-compatible macros so the same test source files compile
 * with any C99/C11 compiler (gcc, clang, MSVC, arm-none-eabi-gcc, …)
 * without requiring PlatformIO or any external test framework.
 *
 * Usage:
 *   In EXACTLY ONE translation unit (typically test_main.c), define the
 *   implementation macro before including this header:
 *
 *       #define SDI12_TEST_IMPLEMENTATION
 *       #include "sdi12_test.h"
 *
 *   All other .c files simply include:
 *
 *       #include "sdi12_test.h"
 *
 * Compiling:
 *       gcc -std=c11 -I.. test_main.c test_crc.c ... ../sdi12_crc.c ... -lm -o test_sdi12
 *       ./test_sdi12
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SDI12_TEST_H
#define SDI12_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 *  State — defined once via SDI12_TEST_IMPLEMENTATION, extern everywhere else
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifdef SDI12_TEST_IMPLEMENTATION
  int    sdi12t_count    = 0;
  int    sdi12t_failures = 0;
  jmp_buf sdi12t_abort;
#else
  extern int    sdi12t_count;
  extern int    sdi12t_failures;
  extern jmp_buf sdi12t_abort;
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 *  User-provided hooks (same signature as Unity)
 * ═══════════════════════════════════════════════════════════════════════════ */

extern void setUp(void);
extern void tearDown(void);

/* ═══════════════════════════════════════════════════════════════════════════
 *  Runner macros
 * ═══════════════════════════════════════════════════════════════════════════ */

#define UNITY_BEGIN() \
    do { sdi12t_count = 0; sdi12t_failures = 0; } while (0)

#define UNITY_END() \
    (printf("\n-----------------------\n" \
            "%d Tests %d Failures 0 Ignored\n", \
            sdi12t_count, sdi12t_failures), \
     printf("%s\n", sdi12t_failures ? "FAIL" : "OK"), \
     sdi12t_failures)

#define RUN_TEST(func) do { \
    sdi12t_count++; \
    setUp(); \
    if (setjmp(sdi12t_abort) == 0) { \
        func(); \
        printf("  PASS: %s\n", #func); \
    } else { \
        sdi12t_failures++; \
    } \
    tearDown(); \
} while (0)

/* ═══════════════════════════════════════════════════════════════════════════
 *  Internal failure helper
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SDI12T_FAIL(msg) do { \
    printf("  FAIL: %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
    longjmp(sdi12t_abort, 1); \
} while (0)

#define SDI12T_FAIL_FMT(fmt, ...) do { \
    printf("  FAIL: %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__); \
    longjmp(sdi12t_abort, 1); \
} while (0)

/* ═══════════════════════════════════════════════════════════════════════════
 *  Assertions — Unity-compatible API
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Boolean ────────────────────────────────────────────────────────────── */

#define TEST_ASSERT_TRUE(cond) do { \
    if (!(cond)) SDI12T_FAIL("Expected TRUE"); \
} while (0)

#define TEST_ASSERT_TRUE_MESSAGE(cond, msg) do { \
    if (!(cond)) SDI12T_FAIL(msg); \
} while (0)

#define TEST_ASSERT_FALSE(cond) do { \
    if ((cond)) SDI12T_FAIL("Expected FALSE"); \
} while (0)

#define TEST_ASSERT_FALSE_MESSAGE(cond, msg) do { \
    if ((cond)) SDI12T_FAIL(msg); \
} while (0)

/* ── Integer equality ───────────────────────────────────────────────────── */

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) SDI12T_FAIL_FMT("Expected %lld, got %lld", _e, _a); \
} while (0)

#define TEST_ASSERT_EQUAL_MESSAGE(expected, actual, msg) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) SDI12T_FAIL_FMT("Expected %lld, got %lld (%s)", _e, _a, (msg)); \
} while (0)

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    TEST_ASSERT_EQUAL((expected), (actual))

#define TEST_ASSERT_NOT_EQUAL(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e == _a) SDI12T_FAIL_FMT("Expected not %lld", _e); \
} while (0)

#define TEST_ASSERT_NOT_EQUAL_MESSAGE(expected, actual, msg) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e == _a) SDI12T_FAIL_FMT("Expected not %lld (%s)", _e, (msg)); \
} while (0)

/* ── Hex ────────────────────────────────────────────────────────────────── */

#define TEST_ASSERT_EQUAL_HEX16(expected, actual) do { \
    uint16_t _e = (uint16_t)(expected); \
    uint16_t _a = (uint16_t)(actual); \
    if (_e != _a) SDI12T_FAIL_FMT("Expected 0x%04X, got 0x%04X", _e, _a); \
} while (0)

#define TEST_ASSERT_NOT_EQUAL_HEX16(expected, actual) do { \
    uint16_t _e = (uint16_t)(expected); \
    uint16_t _a = (uint16_t)(actual); \
    if (_e == _a) SDI12T_FAIL_FMT("Expected not 0x%04X", _e); \
} while (0)

/* ── Char ───────────────────────────────────────────────────────────────── */

#define TEST_ASSERT_EQUAL_CHAR(expected, actual) do { \
    char _e = (char)(expected); \
    char _a = (char)(actual); \
    if (_e != _a) SDI12T_FAIL_FMT("Expected '%c' (0x%02X), got '%c' (0x%02X)", \
        _e, (unsigned char)_e, _a, (unsigned char)_a); \
} while (0)

/* ── String ─────────────────────────────────────────────────────────────── */

#define TEST_ASSERT_EQUAL_STRING(expected, actual) do { \
    const char *_e = (expected); \
    const char *_a = (actual); \
    if (_e == NULL && _a == NULL) break; \
    if (_e == NULL || _a == NULL || strcmp(_e, _a) != 0) \
        SDI12T_FAIL_FMT("Expected \"%s\", got \"%s\"", \
            _e ? _e : "(null)", _a ? _a : "(null)"); \
} while (0)

/* ── Float ──────────────────────────────────────────────────────────────── */

#define TEST_ASSERT_EQUAL_FLOAT(expected, actual) do { \
    float _e = (float)(expected); \
    float _a = (float)(actual); \
    if (_e != _a) SDI12T_FAIL_FMT("Expected %f, got %f", (double)_e, (double)_a); \
} while (0)

#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual) do { \
    float _d = (float)(delta); \
    float _e = (float)(expected); \
    float _a = (float)(actual); \
    if (fabsf(_a - _e) > _d) \
        SDI12T_FAIL_FMT("Expected %f +/- %f, got %f", \
            (double)_e, (double)_d, (double)_a); \
} while (0)

/* ── Pointer ────────────────────────────────────────────────────────────── */

#define TEST_ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) SDI12T_FAIL("Expected non-NULL pointer"); \
} while (0)

#define TEST_ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) SDI12T_FAIL("Expected NULL pointer"); \
} while (0)

/* ── Relational ─────────────────────────────────────────────────────────── */

#define TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual) do { \
    long long _t = (long long)(threshold); \
    long long _a = (long long)(actual); \
    if (_a < _t) SDI12T_FAIL_FMT("Expected >= %lld, got %lld", _t, _a); \
} while (0)

#define TEST_ASSERT_LESS_OR_EQUAL(threshold, actual) do { \
    long long _t = (long long)(threshold); \
    long long _a = (long long)(actual); \
    if (_a > _t) SDI12T_FAIL_FMT("Expected <= %lld, got %lld", _t, _a); \
} while (0)

/* ═══════════════════════════════════════════════════════════════════════════ */

#ifdef __cplusplus
}
#endif

#endif /* SDI12_TEST_H */
