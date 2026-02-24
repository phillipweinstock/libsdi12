/**
 * @file sdi12_master.h
 * @brief SDI-12 Master (Data Recorder) API.
 *
 * Provides functions for a data recorder to communicate with SDI-12
 * sensors on the bus. All bus I/O goes through user-provided callbacks.
 *
 * Features:
 *   - Send break signal
 *   - Query / change sensor addresses
 *   - Request identification
 *   - Start measurements (M, MC, C, CC, V, R/RC)
 *   - Parse measurement responses (atttn / atttnn / atttnnn)
 *   - Parse data responses (aD0–aD9) with value extraction
 *   - CRC verification on C-variant responses
 *   - Transparent command passthrough for extended commands (X)
 *
 * Usage Pattern:
 *   1. sdi12_master_init()
 *   2. sdi12_master_send_break()       — wake bus
 *   3. sdi12_master_request_*()        — build command string
 *   4. User sends command + waits for response via platform I/O
 *   5. sdi12_master_parse_*()          — decode sensor response
 */
#ifndef SDI12_MASTER_H
#define SDI12_MASTER_H

#include "sdi12.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ────────────────────────────────────────────────────────────────────────── */
/*  Callbacks                                                                */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Send raw bytes on the SDI-12 bus.
 * Implementation must transmit exactly `len` bytes at 1200 baud 7E1.
 */
typedef void (*sdi12_master_send_fn)(const char *data, size_t len, void *user_data);

/**
 * Receive bytes from the SDI-12 bus.
 * Implementation should block/poll for up to `timeout_ms` milliseconds.
 * Returns number of bytes read into `buf`, 0 on timeout.
 */
typedef size_t (*sdi12_master_recv_fn)(char *buf, size_t buflen,
                                        uint32_t timeout_ms, void *user_data);

/**
 * Set the bus direction.
 * Implementation must switch between transmit and receive mode.
 */
typedef void (*sdi12_master_set_dir_fn)(sdi12_dir_t dir, void *user_data);

/**
 * Generate a break signal on the bus.
 * Implementation must hold the line marking (spacing) for ≥ 12ms.
 */
typedef void (*sdi12_master_send_break_fn)(void *user_data);

/**
 * Delay for the specified number of milliseconds.
 * Used for inter-command timing and post-break marking.
 */
typedef void (*sdi12_master_delay_fn)(uint32_t ms, void *user_data);

/** Master callback collection. */
typedef struct {
    sdi12_master_send_fn        send;
    sdi12_master_recv_fn        recv;
    sdi12_master_set_dir_fn     set_direction;
    sdi12_master_send_break_fn  send_break;
    sdi12_master_delay_fn       delay;
    void                       *user_data;
} sdi12_master_callbacks_t;

/* ────────────────────────────────────────────────────────────────────────── */
/*  Master Context                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

typedef struct {
    sdi12_master_callbacks_t cb;
    char                     cmd_buf[SDI12_CMD_MAX_CHARS + 4];  /**< Outgoing command buffer */
    char                     resp_buf[SDI12_RESP_MAX_CHARS + 4]; /**< Incoming response buffer */
    size_t                   resp_len;                          /**< Bytes in response buffer */
} sdi12_master_ctx_t;

/* ────────────────────────────────────────────────────────────────────────── */
/*  Initialization                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Initialize the master context.
 *
 * @param ctx       Master context structure.
 * @param callbacks Platform I/O callbacks (all required).
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_init(sdi12_master_ctx_t *ctx,
                               const sdi12_master_callbacks_t *callbacks);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Bus Operations                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Send a break signal to wake all sensors on the bus.
 * Holds the line for ≥ 12ms break + 8.33ms marking.
 */
sdi12_err_t sdi12_master_send_break(sdi12_master_ctx_t *ctx);

/**
 * Send a raw command string and receive the response.
 *
 * @param ctx         Master context.
 * @param cmd         Command to send (e.g., "0M!").
 * @param timeout_ms  Maximum time to wait for response.
 * @return SDI12_OK if response received, SDI12_ERR_TIMEOUT if not.
 */
sdi12_err_t sdi12_master_transact(sdi12_master_ctx_t *ctx,
                                   const char *cmd,
                                   uint32_t timeout_ms);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Address Commands                                                         */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Query the address of the sensor (only works with one sensor on bus).
 * Sends "?!" and parses the response.
 *
 * @param ctx   Master context.
 * @param addr  [out] Sensor address.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_query_address(sdi12_master_ctx_t *ctx, char *addr);

/**
 * Acknowledge / test if a sensor is present at the given address.
 * Sends "a!" and checks for response.
 *
 * @param ctx     Master context.
 * @param addr    Sensor address to test.
 * @param present [out] true if sensor responded.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_acknowledge(sdi12_master_ctx_t *ctx,
                                      char addr, bool *present);

/**
 * Change a sensor's address.
 * Sends "aAb!" command.
 *
 * @param ctx       Master context.
 * @param old_addr  Current address.
 * @param new_addr  Desired new address.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_change_address(sdi12_master_ctx_t *ctx,
                                         char old_addr, char new_addr);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Identification                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Request sensor identification.
 * Sends "aI!" and parses the SDI-12 identification string.
 *
 * @param ctx   Master context.
 * @param addr  Sensor address.
 * @param ident [out] Parsed identification structure.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_identify(sdi12_master_ctx_t *ctx,
                                   char addr, sdi12_ident_t *ident);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Measurement Commands                                                     */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Start a measurement on the sensor.
 * Sends aM!, aMC!, aM1!–aM9!, aMC1!–aMC9!, aC!, aCC!, etc.
 *
 * @param ctx     Master context.
 * @param addr    Sensor address.
 * @param type    Measurement type (M, C, V).
 * @param group   Group 0–9.
 * @param crc     Request CRC on data responses.
 * @param resp    [out] Parsed measurement response (ttt, count).
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_start_measurement(sdi12_master_ctx_t *ctx,
                                            char addr,
                                            sdi12_meas_type_t type,
                                            uint8_t group,
                                            bool crc,
                                            sdi12_meas_response_t *resp);

/**
 * Wait for a service request from the sensor.
 * Listens for "a\r\n" from the bus.
 *
 * @param ctx        Master context.
 * @param addr       Expected sensor address.
 * @param timeout_ms Maximum time to wait.
 * @return SDI12_OK if service request received, SDI12_ERR_TIMEOUT otherwise.
 */
sdi12_err_t sdi12_master_wait_service_request(sdi12_master_ctx_t *ctx,
                                               char addr,
                                               uint32_t timeout_ms);

/**
 * Request data from a sensor (D0–D9).
 * Sends "aD0!" through "aD9!" and parses the response into values.
 *
 * @param ctx       Master context.
 * @param addr      Sensor address.
 * @param page      Data page 0–9.
 * @param crc       Whether CRC was requested (verifies CRC if true).
 * @param resp      [out] Parsed data response with values.
 * @return SDI12_OK on success, SDI12_ERR_CRC on CRC failure.
 */
sdi12_err_t sdi12_master_get_data(sdi12_master_ctx_t *ctx,
                                   char addr, uint8_t page, bool crc,
                                   sdi12_data_response_t *resp);

/**
 * Start a continuous measurement (R0–R9, RC0–RC9).
 * Sends "aR0!" and parses the immediate data response.
 *
 * @param ctx   Master context.
 * @param addr  Sensor address.
 * @param index Continuous measurement index (0–9).
 * @param crc   Request CRC variant.
 * @param resp  [out] Parsed data response.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_continuous(sdi12_master_ctx_t *ctx,
                                     char addr, uint8_t index, bool crc,
                                     sdi12_data_response_t *resp);

/**
 * Request verification measurement.
 * Sends "aV!" and parses response like M command.
 *
 * @param ctx   Master context.
 * @param addr  Sensor address.
 * @param resp  [out] Parsed measurement response.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_verify(sdi12_master_ctx_t *ctx,
                                 char addr, sdi12_meas_response_t *resp);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Identify Measurement Metadata                                            */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Identify measurement capability.
 * Sends aIM!, aIMn!, aIMC!, aIMCn!, aIC!, aICn!, aICC!, aICCn!,
 *       aIV!, aIHA!, aIHB!, aIR0!–aIR9!.
 *
 * @param ctx       Master context.
 * @param addr      Sensor address.
 * @param cmd_body  Command body after 'aI' (e.g. "M", "M1", "MC", "C",
 *                  "V", "HA", "HB", "R0"–"R9").
 * @param type      Expected response format:
 *                  - SDI12_MEAS_STANDARD for M/V (atttn, n=1 digit)
 *                  - SDI12_MEAS_CONCURRENT for C/R (atttnn, n=2 digits)
 *                  - SDI12_MEAS_HIGHVOL_ASCII or _BINARY for H (atttnnn)
 * @param resp      [out] Parsed measurement response (ttt, count).
 * @return SDI12_OK on success, SDI12_ERR_TIMEOUT if not supported.
 */
sdi12_err_t sdi12_master_identify_measurement(sdi12_master_ctx_t *ctx,
                                               char addr,
                                               const char *cmd_body,
                                               sdi12_meas_type_t type,
                                               sdi12_meas_response_t *resp);

/**
 * Query per-parameter metadata.
 * Sends aIM_nnn!, aIMC_nnn!, aIC_nnn!, etc.
 * Parses "a,SHEF,units;\r\n" response.
 *
 * @param ctx       Master context.
 * @param addr      Sensor address.
 * @param cmd_body  Command body between 'aI' and '_' (e.g. "M", "MC", "C").
 * @param param_num 1-based parameter number.
 * @param resp      [out] Parsed parameter metadata.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_identify_param(sdi12_master_ctx_t *ctx,
                                         char addr,
                                         const char *cmd_body,
                                         uint16_t param_num,
                                         sdi12_param_meta_response_t *resp);

/* ────────────────────────────────────────────────────────────────────────── */
/*  High-Volume Data Retrieval                                               */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Request high-volume data page (D0–D999).
 * Sends "aD0!" through "aD999!" and returns the raw response.
 *
 * For ASCII responses: caller should use sdi12_master_parse_data_values().
 * For binary responses: caller should use sdi12_master_parse_binary_page().
 *
 * @param ctx       Master context.
 * @param addr      Sensor address.
 * @param page      Data page 0–999.
 * @param raw_buf   [out] Raw response (after address).
 * @param raw_len   [in] Buffer capacity / [out] response length.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_get_hv_data(sdi12_master_ctx_t *ctx,
                                      char addr, uint16_t page,
                                      char *raw_buf, size_t *raw_len);

/**
 * Decode the size in bytes of a single binary value for a given type.
 *
 * @param type  Binary data type.
 * @return Size in bytes (1–8), or 0 for invalid.
 */
size_t sdi12_bintype_size(sdi12_bintype_t type);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Extended / Transparent Commands                                          */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Send a raw extended command (aX...!).
 * The user is responsible for parsing the response.
 *
 * @param ctx        Master context.
 * @param addr       Sensor address.
 * @param xcmd       Extended command body (after 'X', before '!').
 * @param resp_buf   [out] Buffer for raw response.
 * @param resp_len   Size of resp_buf / [out] bytes received.
 * @param timeout_ms Response timeout.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_extended(sdi12_master_ctx_t *ctx,
                                   char addr,
                                   const char *xcmd,
                                   char *resp_buf, size_t *resp_len,
                                   uint32_t timeout_ms);

/**
 * Send an extended command and collect a multi-line response.
 * Keeps receiving additional lines as long as data arrives within
 * SDI12_MULTILINE_GAP_MS (150 ms) of the previous line.
 *
 * @param ctx          Master context.
 * @param addr         Sensor address.
 * @param xcmd         Extended command body (after 'X', before '!').
 * @param resp_buf     [out] Buffer for concatenated multi-line response.
 * @param resp_bufsize Size of resp_buf.
 * @param resp_len     [out] Total bytes received.
 * @param line_count   [out] Number of lines collected (NULL to ignore).
 * @param timeout_ms   Response timeout for the *first* line.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_extended_multiline(sdi12_master_ctx_t *ctx,
                                             char addr,
                                             const char *xcmd,
                                             char *resp_buf, size_t resp_bufsize,
                                             size_t *resp_len,
                                             uint8_t *line_count,
                                             uint32_t timeout_ms);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Response Parsing Utilities                                               */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * Parse a measurement response string ("atttn" / "atttnn" / "atttnnn").
 *
 * @param resp_str  Raw response string.
 * @param len       Length of the string.
 * @param type      Expected measurement type (determines n/nn/nnn format).
 * @param resp      [out] Parsed response.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_master_parse_meas_response(const char *resp_str, size_t len,
                                              sdi12_meas_type_t type,
                                              sdi12_meas_response_t *resp);

/**
 * Parse a data response string into individual numeric values.
 *
 * Values are extracted as sign-prefixed decimal numbers (+/-nn.nnn).
 *
 * @param resp_str  Raw data response (after address char).
 * @param len       Length of the data string.
 * @param values    [out] Array of parsed values.
 * @param max_values Size of values array.
 * @param count     [out] Number of values parsed.
 * @param verify_crc If true, verify and strip CRC-16 before parsing.
 * @return SDI12_OK on success, SDI12_ERR_CRC if CRC fails.
 */
sdi12_err_t sdi12_master_parse_data_values(const char *resp_str, size_t len,
                                            sdi12_value_t *values,
                                            uint8_t max_values,
                                            uint8_t *count,
                                            bool verify_crc);

#ifdef __cplusplus
}
#endif

#endif /* SDI12_MASTER_H */
