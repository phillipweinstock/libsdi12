/**
 * @file sdi12_sensor.h
 * @brief SDI-12 Sensor (Slave) API.
 *
 * Implements the sensor side of the SDI-12 protocol. All hardware I/O
 * is performed through user-supplied callbacks — the library never
 * directly accesses any hardware.
 *
 * Usage:
 *   1. Populate an sdi12_sensor_callbacks_t with your platform functions.
 *   2. Call sdi12_sensor_init() with identity info and callbacks.
 *   3. Register measurement parameters with sdi12_sensor_register_param().
 *   4. In your serial RX handler, pass received commands to sdi12_sensor_process().
 *   5. When an async measurement completes, call sdi12_sensor_measurement_done().
 *   6. On detecting a break signal, call sdi12_sensor_break().
 */
#ifndef SDI12_SENSOR_H
#define SDI12_SENSOR_H

#include "sdi12.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ────────────────────────────────────────────────────────────────────────── */
/*  Callback Types                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Callback to read a single measurement parameter.
 *
 * The library calls this when it needs a sensor value (e.g. during aM!, aR!).
 *
 * @param param_index  Index of the parameter (as registered).
 * @param user_data    User pointer passed during registration.
 * @return The measurement value.
 */
typedef sdi12_value_t (*sdi12_read_param_fn)(uint8_t param_index, void *user_data);

/**
 * @brief Callback to start an asynchronous measurement.
 *
 * Called for M/C/V/HA/HB commands when ttt > 0. The platform should
 * begin measuring and call sdi12_sensor_measurement_done() when complete.
 * If NULL, measurements are assumed synchronous (ttt = 0).
 *
 * @param group      Measurement group index (0 = aM!/aC!, 1–9 = aM1!–aM9!, etc.)
 * @param type       Measurement type (standard, concurrent, etc.)
 * @param user_data  User pointer from callbacks.
 * @return Estimated time in seconds (0–999). Library uses this for ttt field.
 */
typedef uint16_t (*sdi12_start_measurement_fn)(uint8_t group,
                                                sdi12_meas_type_t type,
                                                void *user_data);

/**
 * @brief Callback to send a response string on the SDI-12 bus.
 *
 * The library provides a fully formatted, CR/LF-terminated response.
 * The platform must switch to TX, send the bytes, then switch back to RX.
 *
 * @param data  Null-terminated response string (includes CR/LF).
 * @param len   Length of response (excluding null terminator).
 * @param user_data  User pointer from callbacks.
 */
typedef void (*sdi12_send_response_fn)(const char *data, size_t len, void *user_data);

/**
 * @brief Callback to control the bus direction pin.
 *
 * @param dir   SDI12_DIR_TX or SDI12_DIR_RX.
 * @param user_data  User pointer from callbacks.
 */
typedef void (*sdi12_set_direction_fn)(sdi12_dir_t dir, void *user_data);

/**
 * @brief Callback to persist the sensor address to non-volatile storage.
 *
 * @param address  New address character.
 * @param user_data  User pointer from callbacks.
 */
typedef void (*sdi12_save_address_fn)(char address, void *user_data);

/**
 * @brief Callback to load the sensor address from non-volatile storage.
 *
 * @param user_data  User pointer from callbacks.
 * @return The stored address, or '0' if none stored.
 */
typedef char (*sdi12_load_address_fn)(void *user_data);

/**
 * @brief Callback to send a service request (a<CR><LF>) on the bus.
 *
 * Called when a deferred measurement (ttt > 0) completes. The platform
 * must switch to TX, send the service request, then switch back to RX.
 * If NULL, the library will use send_response instead.
 *
 * @param user_data  User pointer from callbacks.
 */
typedef void (*sdi12_service_request_fn)(void *user_data);

/**
 * @brief Callback for device reset (aX! extended command).
 *
 * @param user_data  User pointer from callbacks.
 */
typedef void (*sdi12_reset_fn)(void *user_data);

/**
 * @brief Extended command handler callback.
 *
 * @param xcmd       The extended command string (everything between 'X' and '!').
 * @param response   Buffer to write the response into (pre-filled with address).
 * @param resp_size  Size of the response buffer.
 * @param user_data  User pointer from callbacks.
 * @return SDI12_OK if handled, SDI12_ERR_INVALID_COMMAND if not recognized.
 */
typedef sdi12_err_t (*sdi12_xcmd_handler_fn)(const char *xcmd,
                                              char *response, size_t resp_size,
                                              void *user_data);

/**
 * @brief Format a binary data page for high-volume binary (aHB!) responses.
 *
 * Called instead of the default ASCII formatter when the pending measurement
 * type is SDI12_MEAS_HIGHVOL_BINARY. The implementation writes raw bytes into
 * buf (manufacturer-defined encoding). The library prepends the address and
 * appends CRLF automatically.
 *
 * @param page       Requested page number (0–999).
 * @param values     Array of measured values from the data cache.
 * @param count      Number of values in the array.
 * @param buf        Output buffer (address already at buf[0]).
 * @param buflen     Total size of the output buffer.
 * @param user_data  User pointer from callbacks.
 * @return Number of payload bytes written starting at buf[1] (excluding address).
 *         Return 0 if the page is empty or past the last page.
 */
typedef size_t (*sdi12_format_binary_fn)(uint16_t page,
                                         const sdi12_value_t *values,
                                         uint8_t count,
                                         char *buf, size_t buflen,
                                         void *user_data);

/* ────────────────────────────────────────────────────────────────────────── */
/*  Callback Collection                                                      */
/* ────────────────────────────────────────────────────────────────────────── */

typedef struct {
    /* Required callbacks */
    sdi12_send_response_fn    send_response;   /**< Send formatted response. */
    sdi12_set_direction_fn    set_direction;    /**< Control TX/RX direction. */
    sdi12_read_param_fn       read_param;       /**< Read a sensor parameter. */

    /* Optional callbacks (NULL = feature disabled or default behavior) */
    sdi12_save_address_fn     save_address;     /**< Persist address (NULL = RAM only). */
    sdi12_load_address_fn     load_address;     /**< Load persisted address. */
    sdi12_start_measurement_fn start_measurement; /**< Async measurement (NULL = sync). */
    sdi12_service_request_fn  service_request;  /**< Send service request. */
    sdi12_reset_fn            on_reset;         /**< Device reset hook. */
    sdi12_format_binary_fn    format_binary_page; /**< Binary HV data (NULL = unsupported). */

    void *user_data; /**< Passed to all callbacks. */
} sdi12_sensor_callbacks_t;

/* ────────────────────────────────────────────────────────────────────────── */
/*  Parameter Registration                                                   */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Registered measurement parameter.
 */
typedef struct {
    sdi12_param_meta_t meta;    /**< SHEF code and units. */
    uint8_t group;              /**< Measurement group (0 = M/C, 1–9 = M1–M9/C1–C9). */
    uint8_t decimals;           /**< Default decimal places. */
    bool    active;             /**< Whether this slot is in use. */
} sdi12_param_reg_t;

/**
 * @brief Extended command registration.
 */
typedef struct {
    char                 prefix[16]; /**< Command prefix to match (after 'X'). */
    sdi12_xcmd_handler_fn handler;   /**< Handler callback. */
    bool                 active;
} sdi12_xcmd_reg_t;

/* ────────────────────────────────────────────────────────────────────────── */
/*  Sensor Context                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Complete sensor context — holds all state for one SDI-12 sensor.
 *
 * Allocate one of these (statically or dynamically) and pass to all
 * sdi12_sensor_*() functions. Do not access fields directly — use the API.
 */
typedef struct {
    /* Configuration */
    char               address;
    sdi12_ident_t      ident;
    sdi12_sensor_callbacks_t cb;

    /* Parameter table */
    sdi12_param_reg_t  params[SDI12_MAX_PARAMS];
    uint8_t            param_count;

    /* Extended command table */
    sdi12_xcmd_reg_t   xcmds[SDI12_MAX_XCMDS];
    uint8_t            xcmd_count;

    /* State machine */
    sdi12_state_t      state;
    sdi12_meas_type_t  pending_meas_type;
    uint8_t            pending_meas_group;
    bool               crc_requested;

    /* Measurement data cache */
    sdi12_value_t      data_cache[SDI12_MAX_PARAMS];
    uint8_t            data_cache_count;
    bool               data_available;

    /* Response buffer */
    char               resp_buf[SDI12_MAX_RESPONSE_LEN];
    size_t             resp_len;  /**< Actual response length (avoids strlen on binary). */
} sdi12_sensor_ctx_t;

/* ────────────────────────────────────────────────────────────────────────── */
/*  API Functions                                                            */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialize a sensor context.
 *
 * @param ctx       Pointer to sensor context (caller-allocated).
 * @param address   Initial SDI-12 address ('0'–'9', 'a'–'z', 'A'–'Z').
 * @param ident     Sensor identification info.
 * @param callbacks Callback function pointers.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_sensor_init(sdi12_sensor_ctx_t *ctx,
                               char address,
                               const sdi12_ident_t *ident,
                               const sdi12_sensor_callbacks_t *callbacks);

/**
 * @brief Register a measurement parameter.
 *
 * @param ctx      Sensor context.
 * @param group    Measurement group (0 = aM!/aC!, 1–9 = aM1!–aM9!/aC1!–aC9!).
 * @param shef     SHEF code string (e.g. "TA").
 * @param units    Units string (e.g. "C").
 * @param decimals Default decimal places for formatting.
 * @return SDI12_OK on success, SDI12_ERR_PARAM_LIMIT if table full.
 */
sdi12_err_t sdi12_sensor_register_param(sdi12_sensor_ctx_t *ctx,
                                         uint8_t group,
                                         const char *shef,
                                         const char *units,
                                         uint8_t decimals);

/**
 * @brief Register an extended command handler.
 *
 * @param ctx     Sensor context.
 * @param prefix  Command prefix to match (e.g. "RESET" for aXRESET!).
 * @param handler Callback invoked when this command is received.
 * @return SDI12_OK on success, SDI12_ERR_PARAM_LIMIT if table full.
 */
sdi12_err_t sdi12_sensor_register_xcmd(sdi12_sensor_ctx_t *ctx,
                                        const char *prefix,
                                        sdi12_xcmd_handler_fn handler);

/**
 * @brief Process a received SDI-12 command.
 *
 * Call this when a complete command (terminated by '!') has been received
 * from the bus. The library will parse it, generate the appropriate response,
 * and call the send_response callback.
 *
 * @param ctx    Sensor context.
 * @param cmd    Command buffer (null-terminated, '!' already stripped or present).
 * @param len    Length of command (excluding null terminator).
 * @return SDI12_OK if command was handled.
 */
sdi12_err_t sdi12_sensor_process(sdi12_sensor_ctx_t *ctx,
                                  const char *cmd, size_t len);

/**
 * @brief Notify the library that an asynchronous measurement is complete.
 *
 * Call this from your measurement completion callback/ISR after
 * start_measurement was invoked. The library will send a service request
 * (for M/V commands) and make data available for D commands.
 *
 * @param ctx     Sensor context.
 * @param values  Array of measurement values.
 * @param count   Number of values.
 * @return SDI12_OK on success.
 */
sdi12_err_t sdi12_sensor_measurement_done(sdi12_sensor_ctx_t *ctx,
                                           const sdi12_value_t *values,
                                           uint8_t count);

/**
 * @brief Notify the library that a break signal was detected.
 *
 * The platform must detect the break condition (≥12ms of spacing on the bus)
 * and call this function. The library will abort any pending measurement
 * and transition to the ready state.
 *
 * @param ctx  Sensor context.
 */
void sdi12_sensor_break(sdi12_sensor_ctx_t *ctx);

/**
 * @brief Get the current sensor address.
 *
 * @param ctx  Sensor context.
 * @return Current address character.
 */
static inline char sdi12_sensor_get_address(const sdi12_sensor_ctx_t *ctx) {
    return ctx->address;
}

/**
 * @brief Get the current sensor state.
 *
 * @param ctx  Sensor context.
 * @return Current state enum value.
 */
static inline sdi12_state_t sdi12_sensor_get_state(const sdi12_sensor_ctx_t *ctx) {
    return ctx->state;
}

/**
 * @brief Get the number of registered parameters in a measurement group.
 *
 * @param ctx   Sensor context.
 * @param group Measurement group index (0–9).
 * @return Number of parameters in that group.
 */
uint8_t sdi12_sensor_group_count(const sdi12_sensor_ctx_t *ctx, uint8_t group);

#ifdef __cplusplus
}
#endif

#endif /* SDI12_SENSOR_H */
