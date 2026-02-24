/**
 * @file example_crc.c
 * @brief Example: Using the CRC-16-IBM functions standalone.
 *
 * The CRC module can be used independently of the sensor/master APIs.
 * This is useful for validating logged data, building custom protocols,
 * or verifying sensor responses offline.
 *
 * Compile and run:
 *   gcc -std=c11 -I.. -o example_crc example_crc.c ../sdi12_crc.c -lm
 *   ./example_crc
 */
#include "sdi12.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    printf("=== libsdi12 CRC-16-IBM Example ===\n\n");

    /* ── 1. Compute a raw CRC ──────────────────────────────────────────── */
    const char *data = "0+22.50+55.3+101.3";
    uint16_t crc = sdi12_crc16(data, strlen(data));
    printf("1. CRC of \"%s\" = 0x%04X\n", data, crc);

    /* ── 2. Encode CRC as 3 ASCII characters ───────────────────────────── */
    char encoded[4];
    sdi12_crc_encode_ascii(crc, encoded);
    printf("2. Encoded as 3 ASCII chars: \"%s\" (0x%02X 0x%02X 0x%02X)\n",
           encoded,
           (unsigned char)encoded[0],
           (unsigned char)encoded[1],
           (unsigned char)encoded[2]);

    /* ── 3. Append CRC + CRLF to a response buffer ────────────────────── */
    char buf[64];
    strcpy(buf, "0+22.50+55.3+101.3");
    printf("3. Before append: \"%s\" (%zu bytes)\n", buf, strlen(buf));

    sdi12_err_t err = sdi12_crc_append(buf, sizeof(buf));
    if (err == SDI12_OK) {
        /* Replace \r\n with visible chars for display */
        size_t len = strlen(buf);
        printf("   After append:  \"%.*s\\r\\n\" (%zu bytes)\n",
               (int)(len - 2), buf, len);
    }

    /* ── 4. Verify a CRC-bearing response ──────────────────────────────── */
    bool valid = sdi12_crc_verify(buf, strlen(buf));
    printf("4. CRC verify: %s\n", valid ? "PASS" : "FAIL");

    /* ── 5. Corrupt a byte and verify again ────────────────────────────── */
    buf[5] = '9';  /* corrupt the data */
    bool corrupt = sdi12_crc_verify(buf, strlen(buf));
    printf("5. After corruption: %s (expected FAIL)\n",
           corrupt ? "PASS" : "FAIL");

    /* ── 6. Buffer overflow protection ─────────────────────────────────── */
    char tiny[8] = "0+1.23";
    err = sdi12_crc_append(tiny, sizeof(tiny));
    printf("6. Append to 8-byte buffer: %s (expected OVERFLOW)\n",
           err == SDI12_ERR_BUFFER_OVERFLOW ? "OVERFLOW" : "OK");

    printf("\nDone.\n");
    return 0;
}
