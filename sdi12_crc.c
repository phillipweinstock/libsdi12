/**
 * @file sdi12_crc.c
 * @brief SDI-12 CRC-16-IBM implementation.
 *
 * Pure C, no dependencies beyond stdint/stddef/string.
 * Implements the CRC algorithm specified in SDI-12 v1.4 §4.4.12.
 */
#include "sdi12.h"
#include <string.h>

/*
 * CRC-16-IBM (reflected polynomial 0xA001).
 *
 * From the spec:
 *   Initial CRC = 0x0000
 *   For each character:
 *     CRC ^= character
 *     For 8 bits:
 *       if (CRC & 1): CRC = (CRC >> 1) ^ 0xA001
 *       else:          CRC >>= 1
 */
uint16_t sdi12_crc16(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0x0000;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)p[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void sdi12_crc_encode_ascii(uint16_t crc, char out[4])
{
    out[0] = (char)(0x40 | (crc >> 12));
    out[1] = (char)(0x40 | ((crc >> 6) & 0x3F));
    out[2] = (char)(0x40 | (crc & 0x3F));
    out[3] = '\0';
}

sdi12_err_t sdi12_crc_append(char *buf, size_t buflen)
{
    size_t slen = strlen(buf);

    /* Find where CR/LF is (if present) to insert CRC before it. */
    size_t data_end = slen;
    if (slen >= 2 && buf[slen - 2] == '\r' && buf[slen - 1] == '\n') {
        data_end = slen - 2;
    }

    /* Need room for 3 CRC chars + CR + LF + null */
    if (data_end + 3 + 2 + 1 > buflen) {
        return SDI12_ERR_BUFFER_OVERFLOW;
    }

    uint16_t crc = sdi12_crc16(buf, data_end);
    char encoded[4];
    sdi12_crc_encode_ascii(crc, encoded);

    /* Insert CRC before CR/LF */
    buf[data_end]     = encoded[0];
    buf[data_end + 1] = encoded[1];
    buf[data_end + 2] = encoded[2];
    buf[data_end + 3] = '\r';
    buf[data_end + 4] = '\n';
    buf[data_end + 5] = '\0';

    return SDI12_OK;
}

bool sdi12_crc_verify(const char *buf, size_t len)
{
    /* Minimum valid: a + 3 CRC + CR + LF = 6 chars */
    if (len < 6) {
        return false;
    }

    /* Strip CR/LF */
    size_t end = len;
    if (buf[end - 1] == '\n') end--;
    if (end > 0 && buf[end - 1] == '\r') end--;

    /* Last 3 chars before CR/LF are the CRC */
    if (end < 3) {
        return false;
    }

    size_t data_end = end - 3;
    uint16_t computed = sdi12_crc16(buf, data_end);

    char expected[4];
    sdi12_crc_encode_ascii(computed, expected);

    return buf[data_end]     == expected[0] &&
           buf[data_end + 1] == expected[1] &&
           buf[data_end + 2] == expected[2];
}
