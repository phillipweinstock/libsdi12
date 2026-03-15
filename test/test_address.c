/**
 * @file test_address.c
 * @brief Unit tests for SDI-12 address validation.
 *
 * Tests cover:
 *   - Valid addresses: '0'-'9', 'A'-'Z', 'a'-'z'
 *   - Invalid addresses: special chars, control chars, space, punctuation
 *   - Boundary characters
 */
#include "sdi12_test.h"
#include "sdi12.h"

void test_valid_digits(void)
{
    for (char c = '0'; c <= '9'; c++) {
        TEST_ASSERT_TRUE_MESSAGE(sdi12_valid_address(c), "Digit should be valid");
    }
}

void test_valid_uppercase(void)
{
    for (char c = 'A'; c <= 'Z'; c++) {
        TEST_ASSERT_TRUE_MESSAGE(sdi12_valid_address(c), "Uppercase should be valid");
    }
}

void test_valid_lowercase(void)
{
    for (char c = 'a'; c <= 'z'; c++) {
        TEST_ASSERT_TRUE_MESSAGE(sdi12_valid_address(c), "Lowercase should be valid");
    }
}

void test_invalid_special_chars(void)
{
    const char invalid[] = "!@#$%^&*()-+=[]{}|;:'\",.<>?/\\`~ ";
    for (size_t i = 0; i < sizeof(invalid) - 1; i++) {
        TEST_ASSERT_FALSE_MESSAGE(sdi12_valid_address(invalid[i]),
                                  "Special char should be invalid");
    }
}

void test_invalid_control_chars(void)
{
    for (char c = 0; c < ' '; c++) {
        TEST_ASSERT_FALSE_MESSAGE(sdi12_valid_address(c),
                                  "Control char should be invalid");
    }
}

void test_invalid_boundaries(void)
{
    /* Characters just outside valid ranges */
    TEST_ASSERT_FALSE(sdi12_valid_address('/' )); /* before '0' */
    TEST_ASSERT_FALSE(sdi12_valid_address(':' )); /* after '9' */
    TEST_ASSERT_FALSE(sdi12_valid_address('@' )); /* before 'A' */
    TEST_ASSERT_FALSE(sdi12_valid_address('[' )); /* after 'Z' */
    TEST_ASSERT_FALSE(sdi12_valid_address('`' )); /* before 'a' */
    TEST_ASSERT_FALSE(sdi12_valid_address('{' )); /* after 'z' */
    TEST_ASSERT_FALSE(sdi12_valid_address(0x7F)); /* DEL */
}

void test_total_valid_count(void)
{
    int count = 0;
    for (int c = 0; c < 128; c++) {
        if (sdi12_valid_address((char)c)) count++;
    }
    TEST_ASSERT_EQUAL_INT(62, count); /* 10 + 26 + 26 = 62 per spec */
}
