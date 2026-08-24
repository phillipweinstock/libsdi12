/**
 * @file sdi12_sensor.c
 * @brief SDI-12 Sensor (Slave) Implementation.
 *
 * Implements the full SDI-12 v1.4 sensor command set:
 *   a!, ?!, aI!, aM!/aMC!/aM1-9!/aMC1-9!, aC!/aCC!/aC1-9!/aCC1-9!,
 *   aD0-9!, aR0-9!/aRC0-9!, aV!, aAb!, aH!/aHA!/aHB!, aIM/aIC metadata,
 *   aX extended commands.
 *
 * All I/O through callbacks — zero hardware dependencies.
 */
#include "sdi12_sensor.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ────────────────────────────────────────────────────────────────────────── */
/*  Internal Helpers                                                         */
/* ────────────────────────────────────────────────────────────────────────── */

/** Count parameters in a specific measurement group. */
static uint8_t count_group(const sdi12_sensor_ctx_t *ctx, uint8_t group)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < ctx->param_count; i++) {
        if (ctx->params[i].active && ctx->params[i].group == group) {
            n++;
        }
    }
    return n;
}

/** Collect parameter indices for a given group into an array. */
static uint8_t collect_group_indices(const sdi12_sensor_ctx_t *ctx,
                                      uint8_t group,
                                      uint8_t *indices, uint8_t max)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < ctx->param_count && n < max; i++) {
        if (ctx->params[i].active && ctx->params[i].group == group) {
            indices[n++] = i;
        }
    }
    return n;
}

/** Largest magnitude a data value can carry: 7 digits per §4.4.8 Table 11. */
#define SDI12_VALUE_LIMIT 9999999.0f

/**
 * Format a single value with mandatory sign prefix per SDI-12 spec.
 *
 * Hand-rolled fixed-point formatting: printf's %f is LC_NUMERIC-
 * sensitive, and a host application calling setlocale() must not make
 * the sensor emit "+3,14" on the wire. Integer conversions (%lu) are
 * locale-safe.
 *
 * The requested decimals are reduced so integer digits + decimals never
 * exceed the spec's 7-digit total — the value is ROUNDED to the
 * precision that fits, never truncated as a string.
 */
static int format_value(char *buf, size_t buflen, sdi12_value_t val)
{
    float v = val.value;

    /* Non-finite input (dead ADC, failed conversion) or a magnitude
     * beyond the 7-digit cap: saturate to the ±9999999 sentinel.
     * The negated comparison is deliberate: it is also true for NaN. */
    if (!(fabsf(v) <= SDI12_VALUE_LIMIT)) {
        return snprintf(buf, buflen, "%c9999999", v < 0.0f ? '-' : '+');
    }

    char sign = v >= 0.0f ? '+' : '-';
    double absval = fabsf(v);  /* -0.0f must print "+0.00", not "+-0.00" */

    /* How many digits does the integer part need? */
    unsigned int_digits = 1;
    {
        double t = absval;
        while (t >= 10.0 && int_digits < 7) { t /= 10.0; int_digits++; }
    }

    unsigned decimals = val.decimals;
    if (decimals > 7u - int_digits) decimals = 7u - int_digits;
    if (decimals > 6) decimals = 6;   /* 9-char cap: sign + d + '.' + 6 */

    /* Scale to an integer with round-half-up. Total digits ≤ 7, so the
     * scaled value fits comfortably in unsigned long everywhere. */
    unsigned long scale = 1;
    for (unsigned i = 0; i < decimals; i++) scale *= 10;
    unsigned long scaled = (unsigned long)(absval * (double)scale + 0.5);

    /* Rounding can carry into an extra digit (9.99 -> 10.0): drop
     * decimals until the total fits 7 digits again. */
    while (decimals > 0 && scaled >= 10000000UL) {
        scaled = (scaled + 5) / 10;
        decimals--;
    }
    if (scaled > 9999999UL) scaled = 9999999UL;

    /* decimals may have shrunk above — recompute the divisor */
    scale = 1;
    for (unsigned i = 0; i < decimals; i++) scale *= 10;

    if (decimals == 0) {
        return snprintf(buf, buflen, "%c%lu", sign, scaled);
    }
    return snprintf(buf, buflen, "%c%lu.%0*lu", sign,
                    scaled / scale, (int)decimals, scaled % scale);
}

/**
 * Format values into D-response pages.
 *
 * Populates resp_buf with the response for the requested page. An empty
 * page yields the bare-address response, which is how the wire protocol
 * signals "no data" (§4.4.8).
 */
static sdi12_err_t format_data_page(sdi12_sensor_ctx_t *ctx,
                                     const sdi12_value_t *vals,
                                     uint8_t nvals,
                                     uint16_t page,
                                     uint16_t max_value_chars,
                                     bool with_crc)
{
    char *buf = ctx->resp_buf;
    size_t buflen = sizeof(ctx->resp_buf);

    buf[0] = ctx->address;
    size_t pos = 1;

    /* Walk every cached value, tracking which page it falls on.
     * page_used counts value chars on the current page for ALL values,
     * not just the ones written, so page boundaries are computed the
     * same way regardless of which page was requested. */
    uint16_t current_page = 0;
    size_t  page_used = 0;

    for (uint8_t i = 0; i < nvals; i++) {
        char vbuf[SDI12_VALUE_MAX_CHARS + 1];
        int vlen = format_value(vbuf, sizeof(vbuf), vals[i]);

        if (vlen <= 0) continue;
        /* snprintf returns the untruncated length — clamp to what was
         * actually written so an overlong value can't smuggle garbage */
        if ((size_t)vlen > sizeof(vbuf) - 1) vlen = (int)(sizeof(vbuf) - 1);

        /* Value doesn't fit on the current page — advance to the next */
        if (page_used + (size_t)vlen > max_value_chars && page_used > 0) {
            current_page++;
            page_used = 0;
        }

        if (current_page > page) break;

        if (current_page == page) {
            /* Room for CRC + CRLF + NUL after this value. Strictly
             * greater-than: a value ending exactly at the limit fits —
             * >= here silently dropped the value landing on the
             * boundary (a page filling exactly 75 chars). */
            if (pos + (size_t)vlen > buflen - 6) {
                break;
            }
            memcpy(buf + pos, vbuf, (size_t)vlen);
            pos += (size_t)vlen;
        }

        page_used += (size_t)vlen;
    }

    buf[pos] = '\0';

    /* Append CRC if it was requested */
    if (with_crc) {
        sdi12_crc_append(buf, buflen);
    } else {
        /* Append CR/LF */
        if (pos + 2 < buflen) {
            buf[pos]     = '\r';
            buf[pos + 1] = '\n';
            buf[pos + 2] = '\0';
        }
    }

    return SDI12_OK;
}

/** Send the response buffer via callback.
 *  Uses ctx->resp_len when set (binary data), else strlen (text). */
static void send_response(sdi12_sensor_ctx_t *ctx)
{
    if (ctx->cb.send_response) {
        size_t len = ctx->resp_len ? ctx->resp_len : strlen(ctx->resp_buf);
        ctx->cb.send_response(ctx->resp_buf, len, ctx->cb.user_data);
    }
}

/** Populate data cache synchronously by reading all params in a group. */
static void read_group_sync(sdi12_sensor_ctx_t *ctx, uint8_t group)
{
    uint8_t indices[SDI12_MAX_PARAMS];
    uint8_t n = collect_group_indices(ctx, group, indices, SDI12_MAX_PARAMS);

    ctx->data_cache_count = 0;
    for (uint8_t i = 0; i < n && ctx->data_cache_count < SDI12_MAX_PARAMS; i++) {
        if (ctx->cb.read_param) {
            ctx->data_cache[ctx->data_cache_count] =
                ctx->cb.read_param(indices[i], ctx->cb.user_data);
            ctx->data_cache_count++;
        }
    }
    ctx->data_available = true;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Command Handlers                                                         */
/* ────────────────────────────────────────────────────────────────────────── */

/** Handle a! / ?! — Acknowledge active / Address query. */
static sdi12_err_t handle_acknowledge(sdi12_sensor_ctx_t *ctx)
{
    snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);
    send_response(ctx);
    return SDI12_OK;
}

/** Handle aI! — Send identification. */
static sdi12_err_t handle_identify(sdi12_sensor_ctx_t *ctx)
{
    snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
             "%c" SDI12_PROTOCOL_VERSION "%-8.8s%-6.6s%-3.3s%s\r\n",
             ctx->address,
             ctx->ident.vendor,
             ctx->ident.model,
             ctx->ident.firmware_version,
             ctx->ident.serial);
    send_response(ctx);
    return SDI12_OK;
}

/** Handle aM!, aMC!, aM1!–aM9!, aMC1!–aMC9!, aV! */
static sdi12_err_t handle_measurement(sdi12_sensor_ctx_t *ctx,
                                       uint8_t group, bool with_crc,
                                       sdi12_meas_type_t type)
{
    ctx->crc_requested = with_crc;
    ctx->pending_meas_type = type;
    ctx->pending_meas_group = group;

    uint8_t n = count_group(ctx, group);

    /* If sensor has no data for this group, respond with zero.
     * §5.4: high-volume commands answer in the atttnnn form.
     * The new measurement command still invalidates any retained data
     * (§4.4.5) — a following D must not serve the previous group's
     * values under this group's name. */
    if (n == 0) {
        ctx->data_available = false;
        ctx->data_cache_count = 0;
        if (type == SDI12_MEAS_STANDARD || type == SDI12_MEAS_VERIFICATION) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c0000\r\n", ctx->address);
        } else if (type == SDI12_MEAS_HIGHVOL_ASCII ||
                   type == SDI12_MEAS_HIGHVOL_BINARY) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c000000\r\n", ctx->address);
        } else {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c00000\r\n", ctx->address);
        }
        send_response(ctx);
        return SDI12_OK;
    }

    /* Check if async measurement is supported */
    if (ctx->cb.start_measurement) {
        uint16_t ttt = ctx->cb.start_measurement(group, type, ctx->cb.user_data);
        if (ttt > 999) ttt = 999;

        if (type == SDI12_MEAS_STANDARD || type == SDI12_MEAS_VERIFICATION) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c%03u%u\r\n", ctx->address, ttt, n > 9 ? 9 : n);
            ctx->state = (ttt > 0) ? SDI12_STATE_MEASURING : SDI12_STATE_DATA_READY;
        } else if (type == SDI12_MEAS_CONCURRENT) {
            uint16_t nn = n > 99 ? 99 : n;
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c%03u%02u\r\n", ctx->address, ttt, nn);
            ctx->state = (ttt > 0) ? SDI12_STATE_MEASURING_C : SDI12_STATE_DATA_READY;
        } else if (type == SDI12_MEAS_HIGHVOL_ASCII || type == SDI12_MEAS_HIGHVOL_BINARY) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c%03u%03u\r\n", ctx->address, ttt, (unsigned)n);
            ctx->state = (ttt > 0) ? SDI12_STATE_MEASURING_C : SDI12_STATE_DATA_READY;
        }

        if (ttt == 0) {
            /* Synchronous — read now */
            read_group_sync(ctx, group);
        } else {
            ctx->data_available = false;
        }
    } else {
        /* No async callback — synchronous measurement (ttt = 0) */
        read_group_sync(ctx, group);

        if (type == SDI12_MEAS_STANDARD || type == SDI12_MEAS_VERIFICATION) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c000%u\r\n", ctx->address, n > 9 ? 9 : n);
        } else if (type == SDI12_MEAS_CONCURRENT) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c000%02u\r\n", ctx->address, n > 99 ? 99 : (int)n);
        } else {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c000%03u\r\n", ctx->address, (unsigned)n);
        }
        ctx->state = SDI12_STATE_DATA_READY;
    }

    send_response(ctx);
    return SDI12_OK;
}

/**
 * Handle aDB0!–aDB999! — Send binary data packet per §5.2.
 *
 * Binary packet format (Table 14):
 *   addr(1) + pkt_size(2 LE) + type(1) + payload(N) + CRC(2 LE)
 * CRC is always present, computed over addr+size+type+payload.
 * No CR/LF terminator.
 */
static sdi12_err_t handle_send_binary_data(sdi12_sensor_ctx_t *ctx,
                                            uint16_t page)
{
    char *pkt = ctx->resp_buf;

    if (!ctx->data_available || ctx->cb.format_binary_page == NULL) {
        /* Empty binary packet: addr + 0x0000 + 0x00 + CRC(2) = 6 bytes */
        pkt[0] = ctx->address;
        pkt[1] = 0x00;  /* pkt_size LSB */
        pkt[2] = 0x00;  /* pkt_size MSB */
        pkt[3] = 0x00;  /* type = invalid */
        uint16_t crc = sdi12_crc16(pkt, 4);
        pkt[4] = (char)(crc & 0xFF);
        pkt[5] = (char)((crc >> 8) & 0xFF);
        ctx->resp_len = 6;
        send_response(ctx);
        return SDI12_OK;
    }

    /*
     * Call the format_binary_page callback.  It writes:
     *   buf[1] = type byte
     *   buf[2..] = raw payload bytes
     *   returns number of bytes written starting at buf[1] (type + payload)
     */
    char tmpbuf[SDI12_MAX_RESPONSE_LEN];
    tmpbuf[0] = ctx->address;
    size_t cb_bytes = ctx->cb.format_binary_page(
        page, ctx->data_cache, ctx->data_cache_count,
        tmpbuf, sizeof(tmpbuf), ctx->cb.user_data);

    if (cb_bytes == 0) {
        /* Empty page */
        pkt[0] = ctx->address;
        pkt[1] = 0x00;
        pkt[2] = 0x00;
        pkt[3] = 0x00;
        uint16_t crc = sdi12_crc16(pkt, 4);
        pkt[4] = (char)(crc & 0xFF);
        pkt[5] = (char)((crc >> 8) & 0xFF);
        ctx->resp_len = 6;
        send_response(ctx);
        return SDI12_OK;
    }

    /* cb_bytes = type(1) + raw_data(N), so payload_size = cb_bytes - 1 */
    if (cb_bytes > sizeof(tmpbuf) - 1)
        cb_bytes = sizeof(tmpbuf) - 1;

    uint8_t data_type = (uint8_t)tmpbuf[1];
    uint16_t payload_size = (uint16_t)(cb_bytes - 1);

    /* The full packet (addr + size + type + payload + CRC) must fit the
     * response buffer — clamp the payload so the CRC never lands past it.
     * Also enforce the spec's 1000-byte per-packet payload cap (§5.2
     * Table 14), which matters when SDI12_MAX_RESPONSE_LEN is enlarged. */
    size_t max_payload = sizeof(ctx->resp_buf) - SDI12_BIN_PKT_OVERHEAD;
    if (max_payload > SDI12_BIN_MAX_PAYLOAD)
        max_payload = SDI12_BIN_MAX_PAYLOAD;
    if (payload_size > max_payload)
        payload_size = (uint16_t)max_payload;

    /* Build binary packet: addr + pkt_size(2 LE) + type + payload + CRC(2 LE) */
    pkt[0] = ctx->address;
    pkt[1] = (char)(payload_size & 0xFF);
    pkt[2] = (char)((payload_size >> 8) & 0xFF);
    pkt[3] = (char)data_type;
    if (payload_size > 0)
        memcpy(pkt + 4, tmpbuf + 2, payload_size);

    size_t data_end = 4 + (size_t)payload_size;
    uint16_t crc = sdi12_crc16(pkt, data_end);
    pkt[data_end]     = (char)(crc & 0xFF);
    pkt[data_end + 1] = (char)((crc >> 8) & 0xFF);

    ctx->resp_len = data_end + 2;
    send_response(ctx);
    return SDI12_OK;
}

/** Handle aD0!–aD9! — Send data. */
static sdi12_err_t handle_send_data(sdi12_sensor_ctx_t *ctx, uint16_t page)
{
    if (!ctx->data_available) {
        /* No data — respond with just address */
        if (ctx->crc_requested) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c", ctx->address);
            sdi12_crc_append(ctx->resp_buf, sizeof(ctx->resp_buf));
        } else {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);
        }
        send_response(ctx);
        return SDI12_OK;
    }

    /* High-volume binary: delegate to user callback if available.
     * The callback writes type byte + payload at buf[1] (see
     * sdi12_format_binary_fn); this path transmits that block verbatim
     * after the address — same convention as the aDBn! path, minus the
     * Table 14 size/CRC framing. */
    if (ctx->pending_meas_type == SDI12_MEAS_HIGHVOL_BINARY &&
        ctx->cb.format_binary_page != NULL) {
        ctx->resp_buf[0] = ctx->address;
        size_t payload = ctx->cb.format_binary_page(
            page, ctx->data_cache, ctx->data_cache_count,
            ctx->resp_buf, sizeof(ctx->resp_buf),
            ctx->cb.user_data);
        size_t pos = 1 + payload;  /* address + binary payload */
        if (pos > sizeof(ctx->resp_buf) - 6)
            pos = sizeof(ctx->resp_buf) - 6; /* room for CRC + CRLF + null */

        if (ctx->crc_requested) {
            /* Append CRC using explicit length (binary may contain NUL) */
            sdi12_crc_append_n(ctx->resp_buf, pos, sizeof(ctx->resp_buf));
            ctx->resp_len = pos + 3 + 2;  /* data + 3 CRC chars + CR + LF */
        } else {
            if (pos + 2 < sizeof(ctx->resp_buf)) {
                ctx->resp_buf[pos]     = '\r';
                ctx->resp_buf[pos + 1] = '\n';
                ctx->resp_buf[pos + 2] = '\0';
            }
            ctx->resp_len = pos + 2;  /* data + CR + LF */
        }
        send_response(ctx);
        return SDI12_OK;
    }

    /* Determine max value chars based on measurement type */
    uint16_t max_chars = SDI12_M_VALUES_MAX_CHARS;
    if (ctx->pending_meas_type == SDI12_MEAS_CONCURRENT ||
        ctx->pending_meas_type == SDI12_MEAS_CONTINUOUS ||
        ctx->pending_meas_type == SDI12_MEAS_HIGHVOL_ASCII) {
        max_chars = SDI12_C_VALUES_MAX_CHARS;
    }

    format_data_page(ctx, ctx->data_cache, ctx->data_cache_count,
                     page, max_chars, ctx->crc_requested);
    send_response(ctx);

    /* After the last page is read, data could be retained until next M/C/V */
    return SDI12_OK;
}

/** Handle aR0!–aR9!, aRC0!–aRC9! — Continuous measurement. */
static sdi12_err_t handle_continuous(sdi12_sensor_ctx_t *ctx,
                                      uint8_t index, bool with_crc)
{
    /* Continuous measurements are independent of the measurement cycle
     * (§4.4.8.1) and may be interleaved between aMC! and aD0! — they
     * must not disturb the pending data set, its CRC state, or the
     * measurement type. Everything here is local. */

    /* R0 = all group 0 params, R1 = group 1 params, etc. */
    uint8_t indices[SDI12_MAX_PARAMS];
    uint8_t n = collect_group_indices(ctx, index, indices, SDI12_MAX_PARAMS);

    if (n == 0) {
        /* Sensor doesn't support this continuous measurement */
        if (with_crc) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c", ctx->address);
            sdi12_crc_append(ctx->resp_buf, sizeof(ctx->resp_buf));
        } else {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);
        }
        ctx->resp_len = 0;
        send_response(ctx);
        return SDI12_OK;
    }

    /* Read the group into a local buffer */
    sdi12_value_t rv[SDI12_MAX_PARAMS];
    for (uint8_t i = 0; i < n; i++) {
        rv[i] = ctx->cb.read_param(indices[i], ctx->cb.user_data);
    }

    /* Format as a single data response (like D0) */
    ctx->resp_len = 0;
    format_data_page(ctx, rv, n, 0, SDI12_C_VALUES_MAX_CHARS, with_crc);
    send_response(ctx);

    return SDI12_OK;
}

/** Handle aAb! — Change address. */
static sdi12_err_t handle_change_address(sdi12_sensor_ctx_t *ctx, char new_addr)
{
    if (!sdi12_valid_address(new_addr)) {
        /* Table 8: unable to change — respond with the ORIGINAL
         * address so the recorder learns the change failed. */
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);
        send_response(ctx);
        return SDI12_OK;
    }

    ctx->address = new_addr;

    if (ctx->cb.save_address) {
        ctx->cb.save_address(new_addr, ctx->cb.user_data);
    }

    snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", new_addr);
    send_response(ctx);
    return SDI12_OK;
}

/** ttt for identify-measurement responses (§6.1). Synchronous sensors
 *  are always 000; async sensors report via the optional meas_duration
 *  callback (metadata must not start a measurement). */
static uint16_t identify_meas_ttt(sdi12_sensor_ctx_t *ctx, uint8_t group,
                                  sdi12_meas_type_t type)
{
    if (!ctx->cb.meas_duration) return 0;
    uint16_t ttt = ctx->cb.meas_duration(group, type, ctx->cb.user_data);
    return ttt > 999 ? 999 : ttt;
}

/**
 * Handle aIM!, aIMC!, aIM_nnn!, aIC!, aIC_nnn!, aIV!, aIHA!, aIHB!
 * — Identify measurement metadata (v1.4).
 */
static sdi12_err_t handle_identify_measurement(sdi12_sensor_ctx_t *ctx,
                                                 const char *cmd, size_t len)
{
    /*
     * Formats:
     *   aIM!     → atttn  (same as M response format, describes what M returns)
     *   aIMC!    → atttn  (same but for MC)
     *   aIM1!    → atttn  (for M1)
     *   aIM_nnn! → a,SHEF,units;  (parameter metadata)
     *   aIC!     → atttnn (describes what C returns)
     *   aIC_nnn! → a,SHEF,units;  (parameter metadata)
     *   aIV!     → atttn
     *   aIHA!    → atttnnn
     *   aIHB!    → atttnnn
     *   aIR0!    → like D0 format description
     *   aIR0_nnn! → a,SHEF,units;
     */

    /* cmd[0] = address, cmd[1] = 'I', cmd[2+] = subcommand */
    if (len < 3) return SDI12_ERR_INVALID_COMMAND;

    char subcmd = cmd[2];

    /* Check for parameter metadata request (contains '_') */
    const char *underscore = (const char *)memchr(cmd + 2, '_', len - 2);
    if (underscore) {
        /* Parse nnn after '_': Table 20 defines exactly three digits.
         * An unbounded digit run previously overflowed a signed int. */
        const char *p = underscore + 1;
        const char *cmd_end = cmd + len;
        if (cmd_end - p != 3) return SDI12_ERR_INVALID_COMMAND;
        int param_num = 0;
        for (int di = 0; di < 3; di++) {
            if (p[di] < '0' || p[di] > '9') return SDI12_ERR_INVALID_COMMAND;
            param_num = param_num * 10 + (p[di] - '0');
        }

        /* Determine which group this refers to */
        uint8_t group = 0;
        if (subcmd == 'M' || subcmd == 'C') {
            /* Check if there's a digit after M/C for the group */
            const char *after_mc = cmd + 3;
            /* Skip 'C' if present (for aMC case) */
            if (*after_mc == 'C') after_mc++;
            if (*after_mc >= '1' && *after_mc <= '9' && after_mc < underscore) {
                group = (uint8_t)(*after_mc - '0');
            }
        } else if (subcmd == 'R') {
            /* aIR0_nnn! or aIRC0_nnn! — group digit after optional 'C' */
            size_t gp = (cmd[3] == 'C') ? 4 : 3;
            if (cmd + gp < underscore && cmd[gp] >= '0' && cmd[gp] <= '9') {
                group = (uint8_t)(cmd[gp] - '0');
            }
        }

        /* Find the param_num-th parameter in this group (1-based) */
        uint8_t indices[SDI12_MAX_PARAMS];
        uint8_t n = collect_group_indices(ctx, group, indices, SDI12_MAX_PARAMS);

        /* Table 20: the response carries a CRC only when the underlying
         * measurement command returns one — the MC/CC/RC families and
         * always for HA/HB. Note the check is on the character AFTER
         * the family letter: aIC_nnn! has no CRC, aICC_nnn! does.
         * The pending-data CRC state is deliberately left alone —
         * metadata queries must not strip the CRC from a retained
         * aMC!/aCC! data set (§4.4.5). */
        bool crc = (subcmd == 'H') ||
                   ((subcmd == 'M' || subcmd == 'C' || subcmd == 'R') &&
                    cmd[3] == 'C');

        if (param_num >= 1 && param_num <= n) {
            uint8_t idx = indices[param_num - 1];

            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c,%s,%s;",
                     ctx->address,
                     ctx->params[idx].meta.shef,
                     ctx->params[idx].meta.units);

            if (crc) {
                sdi12_crc_append(ctx->resp_buf, sizeof(ctx->resp_buf));
            } else {
                size_t slen = strlen(ctx->resp_buf);
                ctx->resp_buf[slen]     = '\r';
                ctx->resp_buf[slen + 1] = '\n';
                ctx->resp_buf[slen + 2] = '\0';
            }
        } else {
            /* Invalid parameter number — respond with just address */
            if (crc) {
                snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c", ctx->address);
                sdi12_crc_append(ctx->resp_buf, sizeof(ctx->resp_buf));
            } else {
                snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);
            }
        }
        send_response(ctx);
        return SDI12_OK;
    }

    /* Non-parameter metadata — return the measurement capability
     * summary. §6.1 Table 19: every response is plain atttn/atttnn/
     * atttnnn<CR><LF> — no CRC, even for the aIMC!/aICC! forms — and
     * must carry the same ttt the real command would report. There is
     * no aIR form (continuous measurements need no start command). */
    uint8_t group = 0;

    switch (subcmd) {
    case 'M': {
        /* aIM!, aIM1!–aIM9!, aIMC!, aIMC1!–aIMC9! */
        bool crc_form = (len > 3 && cmd[3] == 'C');
        size_t digit_pos = crc_form ? 4 : 3;
        bool has_digit = (digit_pos < len &&
                          cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9');
        if (has_digit) group = (uint8_t)(cmd[digit_pos] - '0');
        if (len != digit_pos + (has_digit ? 1u : 0u))
            return SDI12_ERR_INVALID_COMMAND;

        uint8_t n = count_group(ctx, group);
        uint16_t ttt = identify_meas_ttt(ctx, group, SDI12_MEAS_STANDARD);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c%03u%u", ctx->address, ttt, n > 9 ? 9 : n);
    } break;

    case 'C': {
        /* aIC!, aIC1!–aIC9!, aICC!, aICC1!–aICC9! */
        bool crc_form = (len > 3 && cmd[3] == 'C');
        size_t digit_pos = crc_form ? 4 : 3;
        bool has_digit = (digit_pos < len &&
                          cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9');
        if (has_digit) group = (uint8_t)(cmd[digit_pos] - '0');
        if (len != digit_pos + (has_digit ? 1u : 0u))
            return SDI12_ERR_INVALID_COMMAND;

        uint8_t n = count_group(ctx, group);
        uint16_t ttt = identify_meas_ttt(ctx, group, SDI12_MEAS_CONCURRENT);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c%03u%02u", ctx->address, ttt, n > 99 ? 99 : n);
    } break;

    case 'V': {
        if (len != 3) return SDI12_ERR_INVALID_COMMAND;
        uint8_t n = count_group(ctx, 0);
        uint16_t ttt = identify_meas_ttt(ctx, 0, SDI12_MEAS_VERIFICATION);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c%03u%u", ctx->address, ttt, n > 9 ? 9 : n);
    } break;

    case 'H': {
        /* aIHA!, aIHB! only */
        if (len != 4 || (cmd[3] != 'A' && cmd[3] != 'B'))
            return SDI12_ERR_INVALID_COMMAND;
        sdi12_meas_type_t t = (cmd[3] == 'A') ? SDI12_MEAS_HIGHVOL_ASCII
                                              : SDI12_MEAS_HIGHVOL_BINARY;
        uint8_t n = count_group(ctx, 0);
        uint16_t ttt = identify_meas_ttt(ctx, 0, t);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c%03u%03u", ctx->address, ttt, (unsigned)n);
    } break;

    default:
        /* Includes aIR…! — not in Table 19. Stay silent. */
        return SDI12_ERR_INVALID_COMMAND;
    }

    {
        size_t slen = strlen(ctx->resp_buf);
        if (slen + 2 < sizeof(ctx->resp_buf)) {
            ctx->resp_buf[slen]     = '\r';
            ctx->resp_buf[slen + 1] = '\n';
            ctx->resp_buf[slen + 2] = '\0';
        }
    }

    send_response(ctx);
    return SDI12_OK;
}

/** Handle aX...! — Extended commands. */
static sdi12_err_t handle_extended(sdi12_sensor_ctx_t *ctx,
                                    const char *cmd, size_t len)
{
    /* cmd[0]=address, cmd[1]='X', cmd[2..len-1] = extended payload.
     * Handlers receive it as a NUL-terminated string with no trailing
     * '!' (per the sdi12_xcmd_handler_fn contract), so copy it out of
     * the caller's unterminated buffer. */
    char xcmd_buf[SDI12_MAX_COMMAND_LEN + 1];
    size_t xcmd_len = len - 2;
    if (xcmd_len > SDI12_MAX_COMMAND_LEN) xcmd_len = SDI12_MAX_COMMAND_LEN;
    memcpy(xcmd_buf, cmd + 2, xcmd_len);
    xcmd_buf[xcmd_len] = '\0';
    const char *xcmd_str = xcmd_buf;

    /* Search registered extended command handlers */
    for (uint8_t i = 0; i < ctx->xcmd_count; i++) {
        if (!ctx->xcmds[i].active) continue;

        size_t plen = strlen(ctx->xcmds[i].prefix);
        if (xcmd_len >= plen && memcmp(xcmd_str, ctx->xcmds[i].prefix, plen) == 0) {
            ctx->resp_buf[0] = ctx->address;
            ctx->resp_buf[1] = '\0';

            sdi12_err_t err = ctx->xcmds[i].handler(
                xcmd_str, ctx->resp_buf, sizeof(ctx->resp_buf), ctx->cb.user_data);

            if (err == SDI12_OK) {
                /* Ensure CR/LF termination */
                size_t slen = strlen(ctx->resp_buf);
                if (slen < 2 || ctx->resp_buf[slen-2] != '\r' || ctx->resp_buf[slen-1] != '\n') {
                    if (slen + 2 < sizeof(ctx->resp_buf)) {
                        ctx->resp_buf[slen]     = '\r';
                        ctx->resp_buf[slen + 1] = '\n';
                        ctx->resp_buf[slen + 2] = '\0';
                    }
                }
                send_response(ctx);
            }
            return err;
        }
    }

    /* No handler found — respond with just address (fail-safe) */
    snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);
    send_response(ctx);
    return SDI12_OK;
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  Public API                                                               */
/* ────────────────────────────────────────────────────────────────────────── */

sdi12_err_t sdi12_sensor_init(sdi12_sensor_ctx_t *ctx,
                               char address,
                               const sdi12_ident_t *ident,
                               const sdi12_sensor_callbacks_t *callbacks)
{
    if (!ctx || !ident || !callbacks) {
        return SDI12_ERR_CALLBACK_MISSING;
    }
    if (!callbacks->send_response || !callbacks->read_param) {
        return SDI12_ERR_CALLBACK_MISSING;
    }
    if (!sdi12_valid_address(address)) {
        return SDI12_ERR_INVALID_ADDRESS;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->address = address;
    ctx->ident = *ident;
    ctx->cb = *callbacks;
    ctx->state = SDI12_STATE_READY;

    /* Try to load persisted address */
    if (ctx->cb.load_address) {
        char loaded = ctx->cb.load_address(ctx->cb.user_data);
        if (sdi12_valid_address(loaded)) {
            ctx->address = loaded;
        }
    }

    return SDI12_OK;
}

sdi12_err_t sdi12_sensor_register_param(sdi12_sensor_ctx_t *ctx,
                                         uint8_t group,
                                         const char *shef,
                                         const char *units,
                                         uint8_t decimals)
{
    if (!ctx || !shef || !units) return SDI12_ERR_INVALID_COMMAND;
    if (ctx->param_count >= SDI12_MAX_PARAMS) return SDI12_ERR_PARAM_LIMIT;
    if (group >= SDI12_MAX_MEAS_GROUPS) return SDI12_ERR_INVALID_COMMAND;

    sdi12_param_reg_t *p = &ctx->params[ctx->param_count];
    memset(p, 0, sizeof(*p));

    strncpy(p->meta.shef, shef, sizeof(p->meta.shef) - 1);
    strncpy(p->meta.units, units, sizeof(p->meta.units) - 1);
    p->group = group;
    p->decimals = decimals;
    p->active = true;

    ctx->param_count++;
    return SDI12_OK;
}

sdi12_err_t sdi12_sensor_register_xcmd(sdi12_sensor_ctx_t *ctx,
                                        const char *prefix,
                                        sdi12_xcmd_handler_fn handler)
{
    if (!ctx || !prefix || !handler) return SDI12_ERR_INVALID_COMMAND;
    if (ctx->xcmd_count >= SDI12_MAX_XCMDS) return SDI12_ERR_PARAM_LIMIT;

    sdi12_xcmd_reg_t *x = &ctx->xcmds[ctx->xcmd_count];
    memset(x, 0, sizeof(*x));

    strncpy(x->prefix, prefix, sizeof(x->prefix) - 1);
    x->handler = handler;
    x->active = true;

    ctx->xcmd_count++;
    return SDI12_OK;
}

/** Structural grammar check for an address-stripped-'!' command.
 *  Mirrors the dispatcher's per-family rules; used to gate the
 *  concurrent-measurement abort so malformed bus noise cannot abort
 *  a measurement (§4.4.7.1 aborts on VALID commands only). */
static bool command_well_formed(const char *cmd, size_t cmdlen)
{
    if (cmdlen == 1) return true;               /* a! / ?! */

    switch (cmd[1]) {
    case 'I':
        return true;    /* aI! + metadata family (detail-checked later) */
    case 'M': case 'C': {
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        size_t dp = crc ? 3 : 2;
        bool dig = (dp < cmdlen && cmd[dp] >= '1' && cmd[dp] <= '9');
        return cmdlen == dp + (dig ? 1u : 0u);
    }
    case 'V': return cmdlen == 2;
    case 'A': return cmdlen == 3 && isprint((unsigned char)cmd[2]);
    case 'H': return cmdlen == 3 && (cmd[2] == 'A' || cmd[2] == 'B');
    case 'R': {
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        size_t dp = crc ? 3 : 2;
        return cmdlen == dp + 1 &&
               cmd[dp] >= '0' && cmd[dp] <= '9';
    }
    case 'D': {
        size_t i = (cmdlen > 2 && cmd[2] == 'B') ? 3 : 2;
        if (i >= cmdlen) return false;
        for (; i < cmdlen; i++)
            if (cmd[i] < '0' || cmd[i] > '9') return false;
        return true;
    }
    case 'X': return cmdlen > 2;
    default:  return false;
    }
}

sdi12_err_t sdi12_sensor_process(sdi12_sensor_ctx_t *ctx,
                                  const char *cmd, size_t len)
{
    if (!ctx || !cmd || len == 0) return SDI12_ERR_INVALID_COMMAND;

    ctx->resp_len = 0;  /* default: send_response uses strlen (safe for text) */

    /* Strip trailing '!' if present */
    size_t cmdlen = len;
    if (cmd[cmdlen - 1] == '!') {
        cmdlen--;
    }
    if (cmdlen == 0) return SDI12_ERR_INVALID_COMMAND;

    char addr = cmd[0];

    /* Address check: must match or be '?' wildcard */
    bool is_query = (cmdlen == 1 && addr == '?');
    bool is_addressed = (addr == ctx->address);

    if (!is_addressed && !is_query) {
        /* Not for us — concurrent measurement is NOT aborted by commands
           to other sensors per spec. */
        return SDI12_ERR_NOT_ADDRESSED;
    }

    /* §4.4.7.1: a VALID command addressed to us while a concurrent
       measurement is running aborts it. Malformed traffic (bus noise
       that happens to look addressed) must NOT kill the measurement,
       so the grammar check comes first. */
    if (is_addressed && ctx->state == SDI12_STATE_MEASURING_C &&
        command_well_formed(cmd, cmdlen)) {
        ctx->state = SDI12_STATE_READY;
        ctx->data_available = false;
        ctx->data_cache_count = 0;
    }

    /* Trivial commands: a! or ?! */
    if (cmdlen == 1) {
        return handle_acknowledge(ctx);
    }

    /* Dispatch on second character */
    switch (cmd[1]) {

    case 'I': {
        /* aI! — basic identification, or aIM!/aIC!/aIV!/aIH!/aIR! metadata */
        if (cmdlen == 2) {
            return handle_identify(ctx);
        }
        return handle_identify_measurement(ctx, cmd, cmdlen);
    }

    case 'M': {
        /* aM!, aMC!, aM1!–aM9!, aMC1!–aMC9! — nothing else */
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        uint8_t group = 0;
        size_t digit_pos = crc ? 3 : 2;
        bool has_digit = (digit_pos < cmdlen &&
                          cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9');

        if (has_digit) group = (uint8_t)(cmd[digit_pos] - '0');
        /* Trailing characters make the command invalid — stay silent */
        if (cmdlen != digit_pos + (has_digit ? 1u : 0u))
            return SDI12_ERR_INVALID_COMMAND;

        /* Invalidate any prior concurrent measurement data */
        return handle_measurement(ctx, group, crc, SDI12_MEAS_STANDARD);
    }

    case 'C': {
        /* aC!, aCC!, aC1!–aC9!, aCC1!–aCC9! — nothing else */
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        uint8_t group = 0;
        size_t digit_pos = crc ? 3 : 2;
        bool has_digit = (digit_pos < cmdlen &&
                          cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9');

        if (has_digit) group = (uint8_t)(cmd[digit_pos] - '0');
        if (cmdlen != digit_pos + (has_digit ? 1u : 0u))
            return SDI12_ERR_INVALID_COMMAND;

        return handle_measurement(ctx, group, crc, SDI12_MEAS_CONCURRENT);
    }

    case 'D': {
        /* aD0!–aD999!, aDB0!–aDB999! — digits only, nothing trailing */
        size_t i = (cmdlen > 2 && cmd[2] == 'B') ? 3 : 2;
        if (i >= cmdlen) return SDI12_ERR_INVALID_COMMAND;

        uint16_t page = 0;
        for (; i < cmdlen; i++) {
            if (cmd[i] < '0' || cmd[i] > '9')
                return SDI12_ERR_INVALID_COMMAND;
            page = (uint16_t)(page * 10 + (cmd[i] - '0'));
            if (page > 999) return SDI12_ERR_INVALID_COMMAND;
        }

        if (cmd[2] == 'B')
            return handle_send_binary_data(ctx, page);
        return handle_send_data(ctx, page);
    }

    case 'R': {
        /* aR0!–aR9!, aRC0!–aRC9! — the digit is REQUIRED, no extras */
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        size_t digit_pos = crc ? 3 : 2;

        if (cmdlen != digit_pos + 1 ||
            cmd[digit_pos] < '0' || cmd[digit_pos] > '9') {
            return SDI12_ERR_INVALID_COMMAND;
        }

        return handle_continuous(ctx, (uint8_t)(cmd[digit_pos] - '0'), crc);
    }

    case 'V': {
        /* aV! — Verification. Exactly two characters. */
        if (cmdlen != 2) return SDI12_ERR_INVALID_COMMAND;
        return handle_measurement(ctx, 0, false, SDI12_MEAS_VERIFICATION);
    }

    case 'A': {
        /* aAb! — Change address. Exactly three characters. '!' can
         * only be a terminator, so an interior '!' means the command
         * is malformed (silence), not merely unchangeable. */
        if (cmdlen == 3 && cmd[2] != '!' && isprint((unsigned char)cmd[2])) {
            return handle_change_address(ctx, cmd[2]);
        }
        return SDI12_ERR_INVALID_COMMAND;
    }

    case 'H': {
        /* Only aHA! and aHB! exist (§5.1/§5.2). The CRC on HV ASCII
         * data pages is mandatory (§5.1), so aHA! requests it
         * unconditionally; aHB! packets carry a binary CRC by framing. */
        if (cmdlen == 3 && cmd[2] == 'A')
            return handle_measurement(ctx, 0, true, SDI12_MEAS_HIGHVOL_ASCII);
        if (cmdlen == 3 && cmd[2] == 'B')
            return handle_measurement(ctx, 0, false, SDI12_MEAS_HIGHVOL_BINARY);
        return SDI12_ERR_INVALID_COMMAND;
    }

    case 'X': {
        /* aX...! — Extended commands */
        return handle_extended(ctx, cmd, cmdlen);
    }

    default:
        /* Unrecognized command — no response per spec */
        return SDI12_ERR_INVALID_COMMAND;
    }
}

sdi12_err_t sdi12_sensor_measurement_done(sdi12_sensor_ctx_t *ctx,
                                           const sdi12_value_t *values,
                                           uint8_t count)
{
    if (!ctx || (!values && count > 0)) return SDI12_ERR_INVALID_COMMAND;

    /* A break (or addressed command) may have aborted the measurement
     * while the hardware was still busy — §4.4.5.1 empties the buffer,
     * and a late completion must not resurrect it. */
    if (ctx->state != SDI12_STATE_MEASURING &&
        ctx->state != SDI12_STATE_MEASURING_C) {
        return SDI12_ERR_INVALID_COMMAND;
    }

    /* Store the values in the cache */
    uint8_t n = count;
    if (n > SDI12_MAX_PARAMS) n = SDI12_MAX_PARAMS;
    if (n > 0) memcpy(ctx->data_cache, values, n * sizeof(sdi12_value_t));
    ctx->data_cache_count = n;
    ctx->data_available = true;

    /* Send service request for standard/verification measurements only */
    ctx->resp_len = 0;  /* text response — strlen is safe */
    if (ctx->state == SDI12_STATE_MEASURING) {
        /* Standard M/V — service request required */
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);

        if (ctx->cb.service_request) {
            ctx->cb.service_request(ctx->cb.user_data);
        } else {
            send_response(ctx);
        }
        ctx->state = SDI12_STATE_DATA_READY;
    } else if (ctx->state == SDI12_STATE_MEASURING_C) {
        /* Concurrent — NO service request per spec */
        ctx->state = SDI12_STATE_DATA_READY;
    }

    return SDI12_OK;
}

void sdi12_sensor_break(sdi12_sensor_ctx_t *ctx)
{
    if (!ctx) return;

    /* A concurrent measurement continues through breaks (§4.4.7) —
       the recorder wakes the sensor with a break before issuing D0. */
    if (ctx->state == SDI12_STATE_MEASURING_C) return;

    /* A break aborts a standard measurement (§4.4.5.1) */
    if (ctx->state == SDI12_STATE_MEASURING) {
        ctx->data_available = false;
        ctx->data_cache_count = 0;
    }

    ctx->state = SDI12_STATE_READY;
}

uint8_t sdi12_sensor_group_count(const sdi12_sensor_ctx_t *ctx, uint8_t group)
{
    if (!ctx) return 0;
    return count_group(ctx, group);
}
