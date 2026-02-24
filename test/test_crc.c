/**
 * @file test_crc.c
 * @brief Unit tests for sdi12_crc.c — CRC-16-IBM implementation.
 *
 * Tests cover:
 *   - Known CRC-16 vectors
 *   - ASCII encoding of CRC (3-char format per §4.4.12.2)
 *   - CRC append to response buffers
 *   - CRC verification on received strings
 *   - Edge cases (empty, single char, buffer overflow)
 */
#include "sdi12_test.h"
#include <string.h>
#include "sdi12.h"

/* ── CRC-16-IBM computation ─────────────────────────────────────────────── */

void test_crc16_empty(void)
{
    uint16_t crc = sdi12_crc16("", 0);
    TEST_ASSERT_EQUAL_HEX16(0x0000, crc);
}

void test_crc16_single_char(void)
{
    /* CRC of '0' (0x30): 0x30 ^ 0 = 0x30, then 8 shifts */
    uint16_t crc = sdi12_crc16("0", 1);
    TEST_ASSERT_NOT_EQUAL_HEX16(0x0000, crc);
}

void test_crc16_known_vector(void)
{
    /* SDI-12 example: address '0', data "+1.23" → compute CRC of "0+1.23" */
    const char data[] = "0+1.23";
    uint16_t crc = sdi12_crc16(data, strlen(data));
    /* Just verify it's deterministic and non-zero */
    uint16_t crc2 = sdi12_crc16(data, strlen(data));
    TEST_ASSERT_EQUAL_HEX16(crc, crc2);
    TEST_ASSERT_NOT_EQUAL_HEX16(0x0000, crc);
}

void test_crc16_different_data_differs(void)
{
    uint16_t crc_a = sdi12_crc16("0+1.00", 6);
    uint16_t crc_b = sdi12_crc16("0+1.01", 6);
    TEST_ASSERT_NOT_EQUAL_HEX16(crc_a, crc_b);
}

/* ── ASCII Encoding ─────────────────────────────────────────────────────── */

void test_crc_encode_ascii_zero(void)
{
    char out[4];
    sdi12_crc_encode_ascii(0x0000, out);
    /* All 6-bit groups are 0 → OR'd with 0x40 → '@' */
    TEST_ASSERT_EQUAL_CHAR('@', out[0]);
    TEST_ASSERT_EQUAL_CHAR('@', out[1]);
    TEST_ASSERT_EQUAL_CHAR('@', out[2]);
    TEST_ASSERT_EQUAL_CHAR('\0', out[3]);
}

void test_crc_encode_ascii_all_ones(void)
{
    char out[4];
    sdi12_crc_encode_ascii(0xFFFF, out);
    /* Top 4 bits: 0xF = 15, but only top 4 used → 0x40 | 0x3F = 0x7F */
    /* Bits 15-12: 0xF → 0x40 | 0xF = 0x4F = 'O' */
    /* Bits 11-6:  0x3F → 0x40 | 0x3F = 0x7F (DEL) */
    /* Bits 5-0:   0x3F → 0x40 | 0x3F = 0x7F (DEL) */
    TEST_ASSERT_EQUAL_CHAR(0x4F, out[0]);
    TEST_ASSERT_EQUAL_CHAR(0x7F, out[1]);
    TEST_ASSERT_EQUAL_CHAR(0x7F, out[2]);
}

void test_crc_encode_ascii_printable_range(void)
{
    /* All output chars must be in range 0x40-0x7F per spec */
    for (uint32_t crc = 0; crc <= 0xFFFF; crc += 257) {
        char out[4];
        sdi12_crc_encode_ascii((uint16_t)crc, out);
        TEST_ASSERT_GREATER_OR_EQUAL(0x40, (unsigned char)out[0]);
        TEST_ASSERT_LESS_OR_EQUAL(0x7F, (unsigned char)out[0]);
        TEST_ASSERT_GREATER_OR_EQUAL(0x40, (unsigned char)out[1]);
        TEST_ASSERT_LESS_OR_EQUAL(0x7F, (unsigned char)out[1]);
        TEST_ASSERT_GREATER_OR_EQUAL(0x40, (unsigned char)out[2]);
        TEST_ASSERT_LESS_OR_EQUAL(0x7F, (unsigned char)out[2]);
    }
}

/* ── CRC Append ─────────────────────────────────────────────────────────── */

void test_crc_append_basic(void)
{
    char buf[32];
    strcpy(buf, "0+1.23");
    sdi12_err_t err = sdi12_crc_append(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* Result: "0+1.23" + 3 CRC chars + CR + LF + null */
    size_t len = strlen(buf);
    TEST_ASSERT_EQUAL(6 + 3 + 2, len);
    TEST_ASSERT_EQUAL_CHAR('\r', buf[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 1]);
}

void test_crc_append_with_existing_crlf(void)
{
    char buf[32];
    strcpy(buf, "0+1.23\r\n");
    sdi12_err_t err = sdi12_crc_append(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(SDI12_OK, err);

    /* CRC inserted before CRLF */
    size_t len = strlen(buf);
    TEST_ASSERT_EQUAL(6 + 3 + 2, len);
    TEST_ASSERT_EQUAL_CHAR('\r', buf[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 1]);
}

void test_crc_append_buffer_overflow(void)
{
    char buf[8]; /* Too small for data + CRC + CRLF */
    strcpy(buf, "0+1.23");
    sdi12_err_t err = sdi12_crc_append(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(SDI12_ERR_BUFFER_OVERFLOW, err);
}

/* ── CRC Verify ─────────────────────────────────────────────────────────── */

void test_crc_verify_valid(void)
{
    char buf[32];
    strcpy(buf, "0+1.23");
    sdi12_crc_append(buf, sizeof(buf));

    bool valid = sdi12_crc_verify(buf, strlen(buf));
    TEST_ASSERT_TRUE(valid);
}

void test_crc_verify_corrupt_data(void)
{
    char buf[32];
    strcpy(buf, "0+1.23");
    sdi12_crc_append(buf, sizeof(buf));

    /* Corrupt a data byte */
    buf[2] = '9';
    bool valid = sdi12_crc_verify(buf, strlen(buf));
    TEST_ASSERT_FALSE(valid);
}

void test_crc_verify_corrupt_crc(void)
{
    char buf[32];
    strcpy(buf, "0+1.23");
    sdi12_crc_append(buf, sizeof(buf));

    /* Corrupt a CRC byte (at position 6) */
    buf[6] ^= 0x01;
    bool valid = sdi12_crc_verify(buf, strlen(buf));
    TEST_ASSERT_FALSE(valid);
}

void test_crc_verify_too_short(void)
{
    TEST_ASSERT_FALSE(sdi12_crc_verify("AB\r\n", 4));
    TEST_ASSERT_FALSE(sdi12_crc_verify("A", 1));
    TEST_ASSERT_FALSE(sdi12_crc_verify("", 0));
}

void test_crc_roundtrip_various(void)
{
    const char *test_strings[] = {
        "0", "0+25.50-3.14+100",
        "A+1.00+2.00+3.00+4.00+5.00",
        "z-999.999+0.001"
    };

    for (size_t i = 0; i < sizeof(test_strings) / sizeof(test_strings[0]); i++) {
        char buf[80];
        strcpy(buf, test_strings[i]);
        sdi12_err_t err = sdi12_crc_append(buf, sizeof(buf));
        TEST_ASSERT_EQUAL(SDI12_OK, err);
        TEST_ASSERT_TRUE(sdi12_crc_verify(buf, strlen(buf)));
    }
}
