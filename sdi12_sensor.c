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

/** Format a single value with mandatory sign prefix per SDI-12 spec. */
static int format_value(char *buf, size_t buflen, sdi12_value_t val)
{
    char sign = val.value >= 0.0f ? '+' : '-';
    float absval = val.value >= 0.0f ? val.value : -val.value;

    if (val.decimals == 0) {
        return snprintf(buf, buflen, "%c%lu", sign, (unsigned long)absval);
    } else {
        return snprintf(buf, buflen, "%c%.*f", sign, val.decimals, (double)absval);
    }
}

/**
 * Format values into D-response pages.
 *
 * Populates resp_buf with the response for the requested page.
 * Returns SDI12_OK if values were written, SDI12_ERR_NO_DATA if page empty.
 */
static sdi12_err_t format_data_page(sdi12_sensor_ctx_t *ctx,
                                     uint8_t page,
                                     uint16_t max_value_chars)
{
    char *buf = ctx->resp_buf;
    size_t buflen = sizeof(ctx->resp_buf);

    buf[0] = ctx->address;
    size_t pos = 1;

    /* Walk through cached values, skipping those on earlier pages */
    uint8_t current_page = 0;
    uint8_t i = 0;
    bool any_on_page = false;

    while (i < ctx->data_cache_count) {
        char vbuf[SDI12_VALUE_MAX_CHARS + 1];
        int vlen = format_value(vbuf, sizeof(vbuf), ctx->data_cache[i]);

        if (vlen <= 0) {
            i++;
            continue;
        }

        /* Would this value fit on the current page? */
        size_t page_used = pos - 1; /* subtract address char */
        if (current_page > 0) {
            /* Reset for new page tracking — pos is already at address */
            /* This is tracked implicitly */
        }

        if (page_used + (size_t)vlen > max_value_chars && page_used > 0) {
            /* Start a new page */
            current_page++;
            pos = 1; /* reset (address already placed) */
            page_used = 0;
        }

        if (current_page == page) {
            /* This value belongs to the requested page */
            if (pos + (size_t)vlen >= buflen - 6) { /* room for CRC + CRLF + null */
                break;
            }
            memcpy(buf + pos, vbuf, (size_t)vlen);
            pos += (size_t)vlen;
            any_on_page = true;
        } else if (current_page > page) {
            break;
        }

        i++;
    }

    if (!any_on_page && page > 0) {
        /* Empty page — just respond with address */
        pos = 1;
    }

    buf[pos] = '\0';

    /* Append CRC if it was requested */
    if (ctx->crc_requested) {
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

    /* If sensor has no data for this group, respond with zero */
    if (n == 0) {
        if (type == SDI12_MEAS_STANDARD || type == SDI12_MEAS_VERIFICATION) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c0000\r\n", ctx->address);
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
    uint8_t data_type = (uint8_t)tmpbuf[1];
    uint16_t payload_size = (uint16_t)(cb_bytes - 1);

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
static sdi12_err_t handle_send_data(sdi12_sensor_ctx_t *ctx, uint8_t page)
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

    /* High-volume binary: delegate to user callback if available */
    if (ctx->pending_meas_type == SDI12_MEAS_HIGHVOL_BINARY &&
        ctx->cb.format_binary_page != NULL) {
        ctx->resp_buf[0] = ctx->address;
        size_t payload = ctx->cb.format_binary_page(
            page, ctx->data_cache, ctx->data_cache_count,
            ctx->resp_buf, sizeof(ctx->resp_buf),
            ctx->cb.user_data);
        size_t pos = 1 + payload;  /* address + binary payload */

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

    format_data_page(ctx, page, max_chars);
    send_response(ctx);

    /* After the last page is read, data could be retained until next M/C/V */
    return SDI12_OK;
}

/** Handle aR0!–aR9!, aRC0!–aRC9! — Continuous measurement. */
static sdi12_err_t handle_continuous(sdi12_sensor_ctx_t *ctx,
                                      uint8_t index, bool with_crc)
{
    ctx->crc_requested = with_crc;
    ctx->pending_meas_type = SDI12_MEAS_CONTINUOUS;

    /* For continuous, we read the specific parameter group and respond immediately */
    /* R0 = all group 0 params, R1 = group 1 params, etc. */
    uint8_t n = count_group(ctx, index);

    if (n == 0) {
        /* Sensor doesn't support this continuous measurement */
        if (with_crc) {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c", ctx->address);
            sdi12_crc_append(ctx->resp_buf, sizeof(ctx->resp_buf));
        } else {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", ctx->address);
        }
        send_response(ctx);
        return SDI12_OK;
    }

    /* Read all params in this group synchronously */
    read_group_sync(ctx, index);

    /* Format as a single data response (like D0) */
    format_data_page(ctx, 0, SDI12_C_VALUES_MAX_CHARS);
    send_response(ctx);

    return SDI12_OK;
}

/** Handle aAb! — Change address. */
static sdi12_err_t handle_change_address(sdi12_sensor_ctx_t *ctx, char new_addr)
{
    if (!sdi12_valid_address(new_addr)) {
        return SDI12_ERR_INVALID_ADDRESS;
    }

    ctx->address = new_addr;

    if (ctx->cb.save_address) {
        ctx->cb.save_address(new_addr, ctx->cb.user_data);
    }

    snprintf(ctx->resp_buf, sizeof(ctx->resp_buf), "%c\r\n", new_addr);
    send_response(ctx);
    return SDI12_OK;
}

/** Handle aH! — High-volume stub for non-supporting sensors. */
static sdi12_err_t handle_highvol_stub(sdi12_sensor_ctx_t *ctx)
{
    snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
             "%c000000\r\n", ctx->address);
    send_response(ctx);
    return SDI12_OK;
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
        /* Parse parameter number after '_' */
        int param_num = 0;
        const char *p = underscore + 1;
        while (*p >= '0' && *p <= '9') {
            param_num = param_num * 10 + (*p - '0');
            p++;
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
            if (cmd[3] >= '0' && cmd[3] <= '9') {
                group = (uint8_t)(cmd[3] - '0');
            }
        }

        /* Find the param_num-th parameter in this group (1-based) */
        uint8_t indices[SDI12_MAX_PARAMS];
        uint8_t n = collect_group_indices(ctx, group, indices, SDI12_MAX_PARAMS);

        if (param_num >= 1 && param_num <= n) {
            uint8_t idx = indices[param_num - 1];
            bool crc = (memchr(cmd + 2, 'C', (size_t)(underscore - cmd - 2)) != NULL);
            ctx->crc_requested = crc;

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
            bool crc = (memchr(cmd + 2, 'C', (size_t)(underscore - cmd - 2)) != NULL);
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

    /* Non-parameter metadata — return measurement capability summary */
    uint8_t group = 0;

    switch (subcmd) {
    case 'M': {
        /* aIM!, aIM1!–aIM9!, aIMC!, aIMC1!–aIMC9! */
        bool crc = (len > 3 && cmd[3] == 'C');
        size_t digit_pos = crc ? 4 : 3;
        if (digit_pos < len && cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9') {
            group = (uint8_t)(cmd[digit_pos] - '0');
        }
        uint8_t n = count_group(ctx, group);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c000%u\r\n", ctx->address, n > 9 ? 9 : n);
    } break;

    case 'C': {
        /* aIC!, aIC1!–aIC9!, aICC!, aICC1!–aICC9! */
        bool crc = (len > 3 && cmd[3] == 'C');
        size_t digit_pos = crc ? 4 : 3;
        if (digit_pos < len && cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9') {
            group = (uint8_t)(cmd[digit_pos] - '0');
        }
        uint8_t n = count_group(ctx, group);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c000%02u\r\n", ctx->address, n > 99 ? 99 : n);
    } break;

    case 'V': {
        uint8_t n = count_group(ctx, 0);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c000%u\r\n", ctx->address, n > 9 ? 9 : n);
    } break;

    case 'H': {
        /* aIHA!, aIHB! */
        if (len > 3 && (cmd[3] == 'A' || cmd[3] == 'B')) {
            uint8_t n = count_group(ctx, 0);
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c000%03u\r\n", ctx->address, (unsigned)n);
        } else {
            snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                     "%c000000\r\n", ctx->address);
        }
    } break;

    case 'R': {
        /* aIR0!–aIR9! — describe continuous measurement capability */
        if (len > 3 && cmd[3] >= '0' && cmd[3] <= '9') {
            group = (uint8_t)(cmd[3] - '0');
        }
        uint8_t n = count_group(ctx, group);
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c000%02u\r\n", ctx->address, n > 99 ? 99 : n);
    } break;

    default:
        snprintf(ctx->resp_buf, sizeof(ctx->resp_buf),
                 "%c0000\r\n", ctx->address);
        break;
    }

    send_response(ctx);
    return SDI12_OK;
}

/** Handle aX...! — Extended commands. */
static sdi12_err_t handle_extended(sdi12_sensor_ctx_t *ctx,
                                    const char *cmd, size_t len)
{
    /* cmd[0]=address, cmd[1]='X', cmd[2..len-1] = extended payload */
    const char *xcmd_str = cmd + 2;
    size_t xcmd_len = len - 2;

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
    if (!callbacks->send_response || !callbacks->set_direction || !callbacks->read_param) {
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

    /* If we receive a valid command addressed to us while in concurrent
       measurement state, abort the measurement per spec §4.4.7 */
    if (is_addressed && ctx->state == SDI12_STATE_MEASURING_C) {
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
        /* aM!, aMC!, aM1!–aM9!, aMC1!–aMC9! */
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        uint8_t group = 0;
        size_t digit_pos = crc ? 3 : 2;

        if (digit_pos < cmdlen && cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9') {
            group = (uint8_t)(cmd[digit_pos] - '0');
        }

        /* Invalidate any prior concurrent measurement data */
        return handle_measurement(ctx, group, crc, SDI12_MEAS_STANDARD);
    }

    case 'C': {
        /* aC!, aCC!, aC1!–aC9!, aCC1!–aCC9! */
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        uint8_t group = 0;
        size_t digit_pos = crc ? 3 : 2;

        if (digit_pos < cmdlen && cmd[digit_pos] >= '1' && cmd[digit_pos] <= '9') {
            group = (uint8_t)(cmd[digit_pos] - '0');
        }

        return handle_measurement(ctx, group, crc, SDI12_MEAS_CONCURRENT);
    }

    case 'D': {
        /* aD0!–aD9!, aDB0!–aDB999! */
        if (cmdlen >= 3) {
            /* Check for aDBn! (binary data packet per §5.2) */
            if (cmd[2] == 'B') {
                uint16_t page = 0;
                for (size_t i = 3; i < cmdlen; i++) {
                    if (cmd[i] >= '0' && cmd[i] <= '9')
                        page = page * 10 + (uint16_t)(cmd[i] - '0');
                    else
                        break;
                }
                return handle_send_binary_data(ctx, page);
            }
            /* aDn! — standard ASCII data */
            uint16_t page = 0;
            for (size_t i = 2; i < cmdlen; i++) {
                if (cmd[i] >= '0' && cmd[i] <= '9') {
                    page = page * 10 + (uint16_t)(cmd[i] - '0');
                } else {
                    break;
                }
            }
            return handle_send_data(ctx, (uint8_t)(page > 255 ? 255 : page));
        }
        return SDI12_ERR_INVALID_COMMAND;
    }

    case 'R': {
        /* aR0!–aR9!, aRC0!–aRC9! */
        bool crc = (cmdlen > 2 && cmd[2] == 'C');
        size_t digit_pos = crc ? 3 : 2;
        uint8_t index = 0;

        if (digit_pos < cmdlen && cmd[digit_pos] >= '0' && cmd[digit_pos] <= '9') {
            index = (uint8_t)(cmd[digit_pos] - '0');
        }

        return handle_continuous(ctx, index, crc);
    }

    case 'V': {
        /* aV! — Verification. Same flow as M but uses group 0. */
        return handle_measurement(ctx, 0, false, SDI12_MEAS_VERIFICATION);
    }

    case 'A': {
        /* aAb! — Change address */
        if (cmdlen >= 3 && isprint((unsigned char)cmd[2])) {
            return handle_change_address(ctx, cmd[2]);
        }
        return SDI12_ERR_INVALID_ADDRESS;
    }

    case 'H': {
        /* aH!, aHA!, aHAC!, aHB!, aHBC! */
        if (cmdlen == 2) {
            return handle_highvol_stub(ctx);
        }
        if (cmdlen >= 3) {
            if (cmd[2] == 'A') {
                bool crc = (cmdlen > 3 && cmd[3] == 'C');
                return handle_measurement(ctx, 0, crc, SDI12_MEAS_HIGHVOL_ASCII);
            } else if (cmd[2] == 'B') {
                bool crc = (cmdlen > 3 && cmd[3] == 'C');
                return handle_measurement(ctx, 0, crc, SDI12_MEAS_HIGHVOL_BINARY);
            }
        }
        return handle_highvol_stub(ctx);
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
    if (!ctx) return SDI12_ERR_INVALID_COMMAND;

    /* Store the values in the cache */
    uint8_t n = count;
    if (n > SDI12_MAX_PARAMS) n = SDI12_MAX_PARAMS;
    memcpy(ctx->data_cache, values, n * sizeof(sdi12_value_t));
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

    /* Abort any pending measurement */
    if (ctx->state == SDI12_STATE_MEASURING ||
        ctx->state == SDI12_STATE_MEASURING_C) {
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
