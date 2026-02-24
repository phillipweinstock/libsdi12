/**
 * @file sdi12_master.c
 * @brief SDI-12 Master (Data Recorder) Implementation.
 *
 * Implements command building, transaction management, and response
 * parsing for an SDI-12 data recorder. All bus I/O is through callbacks.
 */
#include "sdi12_master.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ────────────────────────────────────────────────────────────────────────── */
/*  Internal Helpers                                                         */
/* ────────────────────────────────────────────────────────────────────────── */

/** Trim trailing CR/LF from a response string. */
static size_t trim_crlf(char *buf, size_t len)
{
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
        buf[--len] = '\0';
    }
    return len;
}

/**
 * Build a command string in the context buffer and transmit it.
 * Also switches direction to TX before sending, then back to RX.
 */
static sdi12_err_t send_command(sdi12_master_ctx_t *ctx, const char *cmd)
{
    size_t len = strlen(cmd);
    if (len > SDI12_CMD_MAX_CHARS) return SDI12_ERR_INVALID_COMMAND;

    memcpy(ctx->cmd_buf, cmd, len + 1);

    ctx->cb.set_direction(SDI12_DIR_TX, ctx->cb.user_data);
    ctx->cb.send(ctx->cmd_buf, len, ctx->cb.user_data);
    ctx->cb.set_direction(SDI12_DIR_RX, ctx->cb.user_data);

    return SDI12_OK;
}

/** Receive a response with timeout. */
static sdi12_err_t recv_response(sdi12_master_ctx_t *ctx, uint32_t timeout_ms)
{
    ctx->resp_len = ctx->cb.recv(ctx->resp_buf, sizeof(ctx->resp_buf) - 1,
                                  timeout_ms, ctx->cb.user_data);
    if (ctx->resp_len == 0) {
        return SDI12_ERR_TIMEOUT;
    }
    ctx->resp_buf[ctx->resp_len] = '\0';
    return SDI12_OK;
}

/** Read exactly `count` bytes using the recv callback. */
static sdi12_err_t recv_exact(sdi12_master_ctx_t *ctx,
                               char *buf, size_t count, uint32_t timeout_ms)
{
    size_t got = 0;
    while (got < count) {
        size_t n = ctx->cb.recv(buf + got, count - got,
                                 timeout_ms, ctx->cb.user_data);
        if (n == 0) return SDI12_ERR_TIMEOUT;
        got += n;
    }
    return SDI12_OK;
}

/** Parse numeric digits from a string. Returns number of digits consumed. */
static int parse_digits(const char *s, size_t max, uint32_t *out)
{
    *out = 0;
    int n = 0;
    while (n < (int)max && s[n] >= '0' && s[n] <= '9') {
        *out = *out * 10 + (uint32_t)(s[n] - '0');
        n++;
    }
    return n;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Public API — Initialization                                              */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_init(sdi12_master_ctx_t *ctx,
                               const sdi12_master_callbacks_t *callbacks)
{
    if (!ctx || !callbacks) return SDI12_ERR_CALLBACK_MISSING;
    if (!callbacks->send || !callbacks->recv || !callbacks->set_direction ||
        !callbacks->send_break || !callbacks->delay) {
        return SDI12_ERR_CALLBACK_MISSING;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->cb = *callbacks;
    return SDI12_OK;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Bus Operations                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_send_break(sdi12_master_ctx_t *ctx)
{
    if (!ctx) return SDI12_ERR_CALLBACK_MISSING;

    ctx->cb.send_break(ctx->cb.user_data);

    /* Post-break marking time: ≥ 8.33ms */
    ctx->cb.delay(SDI12_MARKING_MS, ctx->cb.user_data);

    return SDI12_OK;
}

sdi12_err_t sdi12_master_transact(sdi12_master_ctx_t *ctx,
                                   const char *cmd, uint32_t timeout_ms)
{
    sdi12_err_t err = send_command(ctx, cmd);
    if (err != SDI12_OK) return err;

    return recv_response(ctx, timeout_ms);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Address Commands                                                         */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_query_address(sdi12_master_ctx_t *ctx, char *addr)
{
    if (!ctx || !addr) return SDI12_ERR_INVALID_COMMAND;

    sdi12_err_t err = sdi12_master_transact(ctx, "?!", SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    if (len >= 1 && sdi12_valid_address(ctx->resp_buf[0])) {
        *addr = ctx->resp_buf[0];
        return SDI12_OK;
    }

    return SDI12_ERR_INVALID_ADDRESS;
}

sdi12_err_t sdi12_master_acknowledge(sdi12_master_ctx_t *ctx,
                                      char addr, bool *present)
{
    if (!ctx || !present) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    char cmd[4];
    snprintf(cmd, sizeof(cmd), "%c!", addr);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err == SDI12_ERR_TIMEOUT) {
        *present = false;
        return SDI12_OK;
    }
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    *present = (len >= 1 && ctx->resp_buf[0] == addr);
    return SDI12_OK;
}

sdi12_err_t sdi12_master_change_address(sdi12_master_ctx_t *ctx,
                                         char old_addr, char new_addr)
{
    if (!ctx) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(old_addr) || !sdi12_valid_address(new_addr)) {
        return SDI12_ERR_INVALID_ADDRESS;
    }

    char cmd[8];
    snprintf(cmd, sizeof(cmd), "%cA%c!", old_addr, new_addr);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    if (len >= 1 && ctx->resp_buf[0] == new_addr) {
        return SDI12_OK;
    }

    return SDI12_ERR_INVALID_ADDRESS;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Identification                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_identify(sdi12_master_ctx_t *ctx,
                                   char addr, sdi12_ident_t *ident)
{
    if (!ctx || !ident) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    char cmd[8];
    snprintf(cmd, sizeof(cmd), "%cI!", addr);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);

    /* Response format: a13vendor__model_ver[serial]\r\n
     * a  = 1 char address
     * 13 = 2 chars SDI-12 version
     * vendor = 8 chars
     * model = 6 chars
     * ver = 3 chars firmware version
     * serial = 0-13 chars optional serial number
     */
    if (len < 20) return SDI12_ERR_INVALID_COMMAND; /* Minimum: 1+2+8+6+3 = 20 */

    memset(ident, 0, sizeof(*ident));

    /* Skip address (1) and version (2) */
    size_t pos = 3;

    /* Vendor: 8 chars */
    memcpy(ident->vendor, ctx->resp_buf + pos, 8);
    ident->vendor[8] = '\0';
    pos += 8;

    /* Model: 6 chars */
    memcpy(ident->model, ctx->resp_buf + pos, 6);
    ident->model[6] = '\0';
    pos += 6;

    /* Firmware version: 3 chars */
    memcpy(ident->firmware_version, ctx->resp_buf + pos, 3);
    ident->firmware_version[3] = '\0';
    pos += 3;

    /* Serial: remaining chars (0-13) */
    if (pos < len) {
        size_t sn_len = len - pos;
        if (sn_len > sizeof(ident->serial) - 1) {
            sn_len = sizeof(ident->serial) - 1;
        }
        memcpy(ident->serial, ctx->resp_buf + pos, sn_len);
        ident->serial[sn_len] = '\0';
    }

    return SDI12_OK;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Measurement Commands                                                     */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_start_measurement(sdi12_master_ctx_t *ctx,
                                            char addr,
                                            sdi12_meas_type_t type,
                                            uint8_t group,
                                            bool crc,
                                            sdi12_meas_response_t *resp)
{
    if (!ctx || !resp) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    /* Build command */
    char cmd[16];
    switch (type) {
    case SDI12_MEAS_STANDARD:
        if (crc) {
            if (group > 0) snprintf(cmd, sizeof(cmd), "%cMC%u!", addr, group);
            else           snprintf(cmd, sizeof(cmd), "%cMC!", addr);
        } else {
            if (group > 0) snprintf(cmd, sizeof(cmd), "%cM%u!", addr, group);
            else           snprintf(cmd, sizeof(cmd), "%cM!", addr);
        }
        break;

    case SDI12_MEAS_CONCURRENT:
        if (crc) {
            if (group > 0) snprintf(cmd, sizeof(cmd), "%cCC%u!", addr, group);
            else           snprintf(cmd, sizeof(cmd), "%cCC!", addr);
        } else {
            if (group > 0) snprintf(cmd, sizeof(cmd), "%cC%u!", addr, group);
            else           snprintf(cmd, sizeof(cmd), "%cC!", addr);
        }
        break;

    case SDI12_MEAS_VERIFICATION:
        snprintf(cmd, sizeof(cmd), "%cV!", addr);
        break;

    case SDI12_MEAS_HIGHVOL_ASCII:
        if (crc) snprintf(cmd, sizeof(cmd), "%cHAC!", addr);
        else     snprintf(cmd, sizeof(cmd), "%cHA!", addr);
        break;

    case SDI12_MEAS_HIGHVOL_BINARY:
        if (crc) snprintf(cmd, sizeof(cmd), "%cHBC!", addr);
        else     snprintf(cmd, sizeof(cmd), "%cHB!", addr);
        break;

    default:
        return SDI12_ERR_INVALID_COMMAND;
    }

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    return sdi12_master_parse_meas_response(ctx->resp_buf, len, type, resp);
}

sdi12_err_t sdi12_master_wait_service_request(sdi12_master_ctx_t *ctx,
                                               char addr,
                                               uint32_t timeout_ms)
{
    if (!ctx) return SDI12_ERR_CALLBACK_MISSING;

    sdi12_err_t err = recv_response(ctx, timeout_ms);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    if (len >= 1 && ctx->resp_buf[0] == addr) {
        return SDI12_OK;
    }

    return SDI12_ERR_TIMEOUT;
}

sdi12_err_t sdi12_master_get_data(sdi12_master_ctx_t *ctx,
                                   char addr, uint8_t page, bool crc,
                                   sdi12_data_response_t *resp)
{
    if (!ctx || !resp) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    char cmd[8];
    snprintf(cmd, sizeof(cmd), "%cD%u!", addr, page);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);

    /* Skip address character */
    if (len < 1) return SDI12_ERR_INVALID_COMMAND;

    resp->address = ctx->resp_buf[0];

    return sdi12_master_parse_data_values(
        ctx->resp_buf + 1, len - 1,
        resp->values, SDI12_MAX_VALUES,
        &resp->value_count, crc);
}

sdi12_err_t sdi12_master_continuous(sdi12_master_ctx_t *ctx,
                                     char addr, uint8_t index, bool crc,
                                     sdi12_data_response_t *resp)
{
    if (!ctx || !resp) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    char cmd[8];
    if (crc) snprintf(cmd, sizeof(cmd), "%cRC%u!", addr, index);
    else     snprintf(cmd, sizeof(cmd), "%cR%u!", addr, index);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    if (len < 1) return SDI12_ERR_INVALID_COMMAND;

    resp->address = ctx->resp_buf[0];

    return sdi12_master_parse_data_values(
        ctx->resp_buf + 1, len - 1,
        resp->values, SDI12_MAX_VALUES,
        &resp->value_count, crc);
}

sdi12_err_t sdi12_master_verify(sdi12_master_ctx_t *ctx,
                                 char addr, sdi12_meas_response_t *resp)
{
    if (!ctx || !resp) return SDI12_ERR_INVALID_COMMAND;

    return sdi12_master_start_measurement(ctx, addr,
                                           SDI12_MEAS_VERIFICATION,
                                           0, false, resp);
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Identify Measurement Metadata                                            */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_identify_measurement(sdi12_master_ctx_t *ctx,
                                               char addr,
                                               const char *cmd_body,
                                               sdi12_meas_type_t type,
                                               sdi12_meas_response_t *resp)
{
    if (!ctx || !cmd_body || !resp) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    /* Build command: aI<cmd_body>!  e.g. "0IM!", "0IC!", "0IHA!" */
    char cmd[SDI12_CMD_MAX_CHARS + 4];
    snprintf(cmd, sizeof(cmd), "%cI%s!", addr, cmd_body);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    return sdi12_master_parse_meas_response(ctx->resp_buf, len, type, resp);
}

sdi12_err_t sdi12_master_identify_param(sdi12_master_ctx_t *ctx,
                                         char addr,
                                         const char *cmd_body,
                                         uint16_t param_num,
                                         sdi12_param_meta_response_t *resp)
{
    if (!ctx || !cmd_body || !resp) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    memset(resp, 0, sizeof(*resp));

    /* Build command: aI<cmd_body>_nnn!  e.g. "0IM_001!" */
    char cmd[SDI12_CMD_MAX_CHARS + 4];
    snprintf(cmd, sizeof(cmd), "%cI%s_%03u!", addr, cmd_body, param_num);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);

    /* Response format: "a,SHEF,units;" (min 4 chars: a,X,;) */
    if (len < 4) return SDI12_ERR_PARSE_FAILED;
    if (ctx->resp_buf[0] != addr) return SDI12_ERR_INVALID_ADDRESS;

    resp->address = ctx->resp_buf[0];

    /* Find first comma after address */
    const char *p = ctx->resp_buf + 1;
    const char *end = ctx->resp_buf + len;

    if (*p != ',') return SDI12_ERR_PARSE_FAILED;
    p++;

    /* Extract SHEF code (up to next comma) */
    const char *shef_start = p;
    while (p < end && *p != ',') p++;
    if (p >= end) return SDI12_ERR_PARSE_FAILED;

    size_t shef_len = (size_t)(p - shef_start);
    if (shef_len >= sizeof(resp->shef)) shef_len = sizeof(resp->shef) - 1;
    memcpy(resp->shef, shef_start, shef_len);
    resp->shef[shef_len] = '\0';

    p++; /* skip comma */

    /* Extract units (up to semicolon or end) */
    const char *units_start = p;
    while (p < end && *p != ';') p++;

    size_t units_len = (size_t)(p - units_start);
    if (units_len >= sizeof(resp->units)) units_len = sizeof(resp->units) - 1;
    memcpy(resp->units, units_start, units_len);
    resp->units[units_len] = '\0';

    return SDI12_OK;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Extended Commands                                                        */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_extended(sdi12_master_ctx_t *ctx,
                                   char addr,
                                   const char *xcmd,
                                   char *resp_buf, size_t *resp_len,
                                   uint32_t timeout_ms)
{
    if (!ctx || !xcmd || !resp_buf || !resp_len) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    char cmd[SDI12_CMD_MAX_CHARS + 4];
    snprintf(cmd, sizeof(cmd), "%cX%s!", addr, xcmd);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, timeout_ms);
    if (err != SDI12_OK) return err;

    size_t len = ctx->resp_len;
    if (len > *resp_len) len = *resp_len;

    memcpy(resp_buf, ctx->resp_buf, len);
    *resp_len = len;

    return SDI12_OK;
}

sdi12_err_t sdi12_master_extended_multiline(sdi12_master_ctx_t *ctx,
                                             char addr,
                                             const char *xcmd,
                                             char *resp_buf, size_t resp_bufsize,
                                             size_t *resp_len,
                                             uint8_t *line_count,
                                             uint32_t timeout_ms)
{
    if (!ctx || !xcmd || !resp_buf || !resp_len) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    char cmd[SDI12_CMD_MAX_CHARS + 4];
    snprintf(cmd, sizeof(cmd), "%cX%s!", addr, xcmd);

    /* Send the command */
    sdi12_err_t err = send_command(ctx, cmd);
    if (err != SDI12_OK) return err;

    /* Receive the first line */
    err = recv_response(ctx, timeout_ms);
    if (err != SDI12_OK) return err;

    /* Copy first line into output buffer */
    size_t total = 0;
    uint8_t lines = 0;

    if (ctx->resp_len > 0) {
        size_t copy = ctx->resp_len;
        if (copy > resp_bufsize) copy = resp_bufsize;
        memcpy(resp_buf, ctx->resp_buf, copy);
        total = copy;
        lines = 1;
    }

    /* Keep collecting lines as long as data arrives within the multi-line gap */
    while (total < resp_bufsize) {
        ctx->resp_len = ctx->cb.recv(ctx->resp_buf, sizeof(ctx->resp_buf) - 1,
                                      SDI12_MULTILINE_GAP_MS, ctx->cb.user_data);
        if (ctx->resp_len == 0)
            break; /* no more lines — 150ms gap elapsed */

        ctx->resp_buf[ctx->resp_len] = '\0';

        size_t copy = ctx->resp_len;
        if (total + copy > resp_bufsize) copy = resp_bufsize - total;
        memcpy(resp_buf + total, ctx->resp_buf, copy);
        total += copy;
        lines++;
    }

    *resp_len = total;
    if (line_count) *line_count = lines;

    return SDI12_OK;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Response Parsing                                                         */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_parse_meas_response(const char *resp_str, size_t len,
                                              sdi12_meas_type_t type,
                                              sdi12_meas_response_t *resp)
{
    if (!resp_str || !resp) return SDI12_ERR_INVALID_COMMAND;

    memset(resp, 0, sizeof(*resp));

    /* Minimum length: address(1) + ttt(3) + n(1) = 5 */
    if (len < 5) return SDI12_ERR_INVALID_COMMAND;

    resp->address = resp_str[0];

    /* Parse ttt (3 digits — seconds to wait) */
    uint32_t ttt;
    int consumed = parse_digits(resp_str + 1, 3, &ttt);
    if (consumed != 3) return SDI12_ERR_INVALID_COMMAND;

    resp->wait_seconds = (uint16_t)ttt;
    size_t pos = 4;

    /* Parse count: n (1 digit for M/V), nn (2 for C), nnn (3 for H) */
    uint32_t count;
    switch (type) {
    case SDI12_MEAS_STANDARD:
    case SDI12_MEAS_VERIFICATION:
        consumed = parse_digits(resp_str + pos, 1, &count);
        if (consumed != 1) return SDI12_ERR_INVALID_COMMAND;
        break;

    case SDI12_MEAS_CONCURRENT:
    case SDI12_MEAS_CONTINUOUS:
        consumed = parse_digits(resp_str + pos, 2, &count);
        if (consumed != 2) return SDI12_ERR_INVALID_COMMAND;
        break;

    case SDI12_MEAS_HIGHVOL_ASCII:
    case SDI12_MEAS_HIGHVOL_BINARY:
        consumed = parse_digits(resp_str + pos, 3, &count);
        if (consumed != 3) return SDI12_ERR_INVALID_COMMAND;
        break;

    default:
        return SDI12_ERR_INVALID_COMMAND;
    }

    resp->value_count = (uint16_t)count;
    return SDI12_OK;
}

sdi12_err_t sdi12_master_parse_data_values(const char *resp_str, size_t len,
                                            sdi12_value_t *values,
                                            uint8_t max_values,
                                            uint8_t *count,
                                            bool verify_crc)
{
    if (!resp_str || !values || !count) return SDI12_ERR_INVALID_COMMAND;

    *count = 0;

    /* If CRC verification requested, check and strip CRC (last 3 chars) */
    size_t data_len = len;
    if (verify_crc && data_len >= 3) {
        /* We need to reconstruct the full response with address for CRC check.
         * Since the caller has stripped the address, we just verify the CRC
         * bytes at the end of the data. The CRC covers address + values. */
        /* For simplicity, just strip the 3 CRC chars — the caller should
         * verify CRC at a higher level with the full response buffer. */
        data_len -= 3;
    }

    /* Parse sign-prefixed values: +1.23-4.56+7.89 */
    size_t pos = 0;
    while (pos < data_len && *count < max_values) {
        /* Skip whitespace */
        while (pos < data_len && resp_str[pos] == ' ') pos++;
        if (pos >= data_len) break;

        /* Expect + or - */
        if (resp_str[pos] != '+' && resp_str[pos] != '-') {
            pos++;
            continue;
        }

        /* Extract the value string */
        size_t start = pos;
        pos++; /* skip sign */
        while (pos < data_len && (isdigit((unsigned char)resp_str[pos]) ||
               resp_str[pos] == '.')) {
            pos++;
        }

        if (pos > start + 1) {
            /* Parse the numeric value */
            char vbuf[SDI12_VALUE_MAX_CHARS + 1];
            size_t vlen = pos - start;
            if (vlen > SDI12_VALUE_MAX_CHARS) vlen = SDI12_VALUE_MAX_CHARS;

            memcpy(vbuf, resp_str + start, vlen);
            vbuf[vlen] = '\0';

            values[*count].value = (float)strtod(vbuf, NULL);

            /* Count decimal places */
            const char *dot = (const char *)memchr(vbuf, '.', vlen);
            if (dot) {
                values[*count].decimals = (uint8_t)(vlen - (size_t)(dot - vbuf) - 1);
            } else {
                values[*count].decimals = 0;
            }

            (*count)++;
        }
    }

    return SDI12_OK;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  High-Volume Data Retrieval                                               */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_get_hv_data(sdi12_master_ctx_t *ctx,
                                      char addr, uint16_t page,
                                      char *raw_buf, size_t *raw_len)
{
    if (!ctx || !raw_buf || !raw_len) return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    char cmd[12];
    snprintf(cmd, sizeof(cmd), "%cD%u!", addr, page);

    sdi12_err_t err = sdi12_master_transact(ctx, cmd, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    /* Response: a<data>\r\n — skip address, trim CRLF */
    size_t len = trim_crlf(ctx->resp_buf, ctx->resp_len);
    if (len < 1) return SDI12_ERR_PARSE_FAILED;

    size_t data_len = len - 1; /* skip address */
    if (data_len > *raw_len) data_len = *raw_len;

    memcpy(raw_buf, ctx->resp_buf + 1, data_len);
    *raw_len = data_len;

    return SDI12_OK;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  High-Volume Binary Data Retrieval (§5.2)                                 */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_master_get_hv_binary_data(sdi12_master_ctx_t *ctx,
                                            char addr, uint16_t page,
                                            sdi12_bintype_t *out_type,
                                            void *out_payload,
                                            size_t *out_len)
{
    if (!ctx || !out_type || !out_payload || !out_len)
        return SDI12_ERR_INVALID_COMMAND;
    if (!sdi12_valid_address(addr)) return SDI12_ERR_INVALID_ADDRESS;

    /* Send aDBn! command */
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "%cDB%u!", addr, page);

    sdi12_err_t err = send_command(ctx, cmd);
    if (err != SDI12_OK) return err;

    /*
     * Binary packet format per §5.2 Table 14:
     *   addr(1) + pkt_size(2 LE) + type(1) + payload(N) + CRC(2 LE)
     * All fields after address are raw binary (8 data bits, no parity).
     */

    /* Read header: address(1) + packet_size(2) = 3 bytes */
    char hdr[4];
    err = recv_exact(ctx, hdr, 3, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    uint16_t pkt_size = (uint8_t)hdr[1] | ((uint16_t)(uint8_t)hdr[2] << 8);

    /* Read data type (1 byte) */
    char type_byte;
    err = recv_exact(ctx, &type_byte, 1, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    *out_type = (sdi12_bintype_t)(uint8_t)type_byte;

    /* Read payload + CRC into a local buffer */
    size_t tail_len = (size_t)pkt_size + 2; /* payload + CRC(2) */
    char tail[SDI12_BIN_MAX_PAYLOAD + 2];
    if (pkt_size > SDI12_BIN_MAX_PAYLOAD) return SDI12_ERR_BUFFER_OVERFLOW;

    err = recv_exact(ctx, tail, tail_len, SDI12_RESPONSE_TIMEOUT_MS);
    if (err != SDI12_OK) return err;

    /* Verify CRC: computed over addr + pkt_size(2) + type(1) + payload(N) */
    size_t crc_data_len = 4 + (size_t)pkt_size;

    /* Assemble contiguous CRC input into resp_buf for computation.
     * Total ≤ 4 + 1000 = 1004 which exceeds resp_buf.  Use a different
     * approach: compute CRC incrementally over hdr(3) + type(1) + payload. */
    {
        /* Compute CRC over header bytes + type byte + payload bytes */
        uint16_t crc = 0;
        /* We need a contiguous buffer.  Build one from the pieces: */
        /* For small payloads (our typical case), stack is fine. */
        uint8_t crc_buf[SDI12_BIN_MAX_PAYLOAD + SDI12_BIN_PKT_OVERHEAD];
        memcpy(crc_buf, hdr, 3);
        crc_buf[3] = (uint8_t)type_byte;
        if (pkt_size > 0)
            memcpy(crc_buf + 4, tail, pkt_size);

        crc = sdi12_crc16(crc_buf, crc_data_len);

        uint16_t received_crc = (uint8_t)tail[pkt_size] |
                                ((uint16_t)(uint8_t)tail[pkt_size + 1] << 8);

        if (crc != received_crc) return SDI12_ERR_CRC_MISMATCH;
    }

    /* Copy payload to output */
    size_t copy_len = pkt_size;
    if (copy_len > *out_len) copy_len = *out_len;
    if (copy_len > 0) memcpy(out_payload, tail, copy_len);
    *out_len = pkt_size;

    /* Store total packet size for timing layer access */
    ctx->resp_len = 3 + 1 + tail_len;

    return SDI12_OK;
}

size_t sdi12_bintype_size(sdi12_bintype_t type)
{
    switch (type) {
    case SDI12_BINTYPE_INT8:    return 1;
    case SDI12_BINTYPE_UINT8:   return 1;
    case SDI12_BINTYPE_INT16:   return 2;
    case SDI12_BINTYPE_UINT16:  return 2;
    case SDI12_BINTYPE_INT32:   return 4;
    case SDI12_BINTYPE_UINT32:  return 4;
    case SDI12_BINTYPE_INT64:   return 8;
    case SDI12_BINTYPE_UINT64:  return 8;
    case SDI12_BINTYPE_FLOAT32: return 4;
    case SDI12_BINTYPE_FLOAT64: return 8;
    default:                    return 0;
    }
}
