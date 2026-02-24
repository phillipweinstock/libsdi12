/**
 * @file test_master.c
 * @brief Unit tests for sdi12_master.c — master (data recorder) response parsing.
 *
 * Tests the pure parsing functions that need no I/O.
 *
 * Tests cover:
 *   - parse_meas_response for M (atttn), C (atttnn), H (atttnnn)
 *   - parse_data_values for sign-prefixed numeric extraction
 *   - Edge cases: zero values, max values, negative values
 *   - CRC strip behavior
 *   - Invalid/truncated inputs
 */
#include "sdi12_test.h"
#include <string.h>
#include <math.h>
#include "sdi12.h"
#include "sdi12_master.h"

/* ── Measurement Response Parsing ───────────────────────────────────────── */

void test_parse_meas_m_basic(void)
{
    sdi12_meas_response_t resp;
    sdi12_err_t err = sdi12_master_parse_meas_response(
        "00005", 5, SDI12_MEAS_STANDARD, &resp);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_CHAR('0', resp.address);
    TEST_ASSERT_EQUAL(0, resp.wait_seconds);
    TEST_ASSERT_EQUAL(5, resp.value_count);
}

void test_parse_meas_m_with_wait(void)
{
    sdi12_meas_response_t resp;
    sdi12_err_t err = sdi12_master_parse_meas_response(
        "01203", 5, SDI12_MEAS_STANDARD, &resp);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(120, resp.wait_seconds);
    TEST_ASSERT_EQUAL(3, resp.value_count);
}

void test_parse_meas_m_max_wait(void)
{
    sdi12_meas_response_t resp;
    sdi12_err_t err = sdi12_master_parse_meas_response(
        "09999", 5, SDI12_MEAS_STANDARD, &resp);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(999, resp.wait_seconds);
    TEST_ASSERT_EQUAL(9, resp.value_count);
}

void test_parse_meas_c_basic(void)
{
    sdi12_meas_response_t resp;
    sdi12_err_t err = sdi12_master_parse_meas_response(
        "000005", 6, SDI12_MEAS_CONCURRENT, &resp);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(0, resp.wait_seconds);
    TEST_ASSERT_EQUAL(5, resp.value_count);
}

void test_parse_meas_c_two_digit_count(void)
{
    sdi12_meas_response_t resp;
    sdi12_err_t err = sdi12_master_parse_meas_response(
        "006015", 6, SDI12_MEAS_CONCURRENT, &resp);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(60, resp.wait_seconds);
    TEST_ASSERT_EQUAL(15, resp.value_count);
}

void test_parse_meas_h_three_digit_count(void)
{
    sdi12_meas_response_t resp;
    sdi12_err_t err = sdi12_master_parse_meas_response(
        "0010100", 7, SDI12_MEAS_HIGHVOL_ASCII, &resp);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(10, resp.wait_seconds);
    TEST_ASSERT_EQUAL(100, resp.value_count);
}

void test_parse_meas_v_same_as_m(void)
{
    sdi12_meas_response_t resp;
    sdi12_err_t err = sdi12_master_parse_meas_response(
        "00003", 5, SDI12_MEAS_VERIFICATION, &resp);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(3, resp.value_count);
}

void test_parse_meas_too_short(void)
{
    sdi12_meas_response_t resp;
    /* Less than 5 chars for M = invalid */
    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_COMMAND,
        sdi12_master_parse_meas_response("000", 3, SDI12_MEAS_STANDARD, &resp));
}

void test_parse_meas_null_args(void)
{
    sdi12_meas_response_t resp;
    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_COMMAND,
        sdi12_master_parse_meas_response(NULL, 5, SDI12_MEAS_STANDARD, &resp));
    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_COMMAND,
        sdi12_master_parse_meas_response("00005", 5, SDI12_MEAS_STANDARD, NULL));
}

void test_parse_meas_different_addresses(void)
{
    sdi12_meas_response_t resp;
    sdi12_master_parse_meas_response("A0005", 5, SDI12_MEAS_STANDARD, &resp);
    TEST_ASSERT_EQUAL_CHAR('A', resp.address);

    sdi12_master_parse_meas_response("z0003", 5, SDI12_MEAS_STANDARD, &resp);
    TEST_ASSERT_EQUAL_CHAR('z', resp.address);
}

/* ── Data Value Parsing ─────────────────────────────────────────────────── */

void test_parse_values_single_positive(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    sdi12_err_t err = sdi12_master_parse_data_values(
        "+1.23", 5, vals, 10, &count, false);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.23f, vals[0].value);
    TEST_ASSERT_EQUAL(2, vals[0].decimals);
}

void test_parse_values_single_negative(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    sdi12_master_parse_data_values("-4.56", 5, vals, 10, &count, false);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -4.56f, vals[0].value);
}

void test_parse_values_multiple(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    const char *data = "+1.23-4.56+7.89";
    sdi12_master_parse_data_values(data, strlen(data), vals, 10, &count, false);
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.23f, vals[0].value);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -4.56f, vals[1].value);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.89f, vals[2].value);
}

void test_parse_values_integer(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    sdi12_master_parse_data_values("+42", 3, vals, 10, &count, false);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 42.0f, vals[0].value);
    TEST_ASSERT_EQUAL(0, vals[0].decimals);
}

void test_parse_values_zero(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    sdi12_master_parse_data_values("+0.00", 5, vals, 10, &count, false);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vals[0].value);
}

void test_parse_values_empty_string(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    sdi12_master_parse_data_values("", 0, vals, 10, &count, false);
    TEST_ASSERT_EQUAL(0, count);
}

void test_parse_values_max_capacity(void)
{
    sdi12_value_t vals[2];
    uint8_t count = 0;
    sdi12_master_parse_data_values("+1+2+3+4", 8, vals, 2, &count, false);
    /* Only 2 should fit */
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 1.0f, vals[0].value);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 2.0f, vals[1].value);
}

void test_parse_values_with_crc_strip(void)
{
    /* When verify_crc=true, last 3 chars are stripped as CRC */
    sdi12_value_t vals[10];
    uint8_t count = 0;
    /* "+1.23ABC" — last 3 "ABC" stripped → parse "+1.23" */
    sdi12_master_parse_data_values("+1.23ABC", 8, vals, 10, &count, true);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.23f, vals[0].value);
}

void test_parse_values_large_value(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    sdi12_master_parse_data_values("+999.999", 8, vals, 10, &count, false);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 999.999f, vals[0].value);
    TEST_ASSERT_EQUAL(3, vals[0].decimals);
}

void test_parse_values_mixed_signs(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    const char *data = "+25.50-3.14+100+0.001-999";
    sdi12_master_parse_data_values(data, strlen(data), vals, 10, &count, false);
    TEST_ASSERT_EQUAL(5, count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.50f, vals[0].value);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.14f, vals[1].value);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, vals[2].value);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.001f, vals[3].value);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -999.0f, vals[4].value);
}

void test_parse_values_null_args(void)
{
    sdi12_value_t vals[10];
    uint8_t count = 0;
    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_COMMAND,
        sdi12_master_parse_data_values(NULL, 5, vals, 10, &count, false));
    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_COMMAND,
        sdi12_master_parse_data_values("+1", 2, NULL, 10, &count, false));
    TEST_ASSERT_EQUAL(SDI12_ERR_INVALID_COMMAND,
        sdi12_master_parse_data_values("+1", 2, vals, 10, NULL, false));
}
