/**
 * @file sdi12_easy.h
 * @brief Beginner-friendly convenience macros for libsdi12.
 *
 * Wraps the full libsdi12 API into a handful of simple macros so you
 * can get an SDI-12 sensor or master running in under 20 lines of code.
 *
 * The core library is unchanged — these macros simply generate the
 * boilerplate (context structs, callbacks, identity) for you.
 *
 * ──────────────────────────────────────────────────────────────────────
 *  SENSOR QUICK START
 * ──────────────────────────────────────────────────────────────────────
 *
 *   // 1. Tell the library how to talk to your hardware:
 *   void my_send(const char *d, size_t n, void *u) { Serial1.write(d, n); }
 *   void my_dir(sdi12_dir_t dir, void *u)          { digitalWrite(2, dir); }
 *
 *   // 2. Tell the library how to read your sensors:
 *   sdi12_value_t my_read(uint8_t idx, void *u) {
 *       sdi12_value_t v = {0};
 *       if (idx == 0) { v.value = bme.readTemperature(); v.decimals = 2; }
 *       if (idx == 1) { v.value = bme.readHumidity();    v.decimals = 1; }
 *       return v;
 *   }
 *
 *   // 3. Define your sensor in one block:
 *   SDI12_SENSOR_DEFINE(my_sensor, '0',
 *       "MYVENDOR", "MDL001", "100", "SN001",
 *       my_send, my_dir, my_read);
 *
 *   // 4. Register what you measure:
 *   void setup() {
 *       SDI12_SENSOR_SETUP(my_sensor);
 *       SDI12_SENSOR_ADD_PARAM(my_sensor, 0, "TA", "degC", 2);
 *       SDI12_SENSOR_ADD_PARAM(my_sensor, 0, "RH", "%RH",  1);
 *   }
 *
 *   // 5. Feed commands from your UART:
 *   void on_command(const char *cmd, size_t len) {
 *       SDI12_SENSOR_PROCESS(my_sensor, cmd, len);
 *   }
 *
 * ──────────────────────────────────────────────────────────────────────
 *  MASTER QUICK START
 * ──────────────────────────────────────────────────────────────────────
 *
 *   void my_send(const char *d, size_t n, void *u)             { ... }
 *   size_t my_recv(char *b, size_t m, uint32_t t, void *u)     { ... }
 *   void my_dir(sdi12_dir_t d, void *u)                        { ... }
 *   void my_brk(void *u)                                       { ... }
 *   void my_dly(uint32_t ms, void *u)                          { ... }
 *
 *   SDI12_MASTER_DEFINE(recorder, my_send, my_recv, my_dir, my_brk, my_dly);
 *
 *   void setup() {
 *       SDI12_MASTER_SETUP(recorder);
 *   }
 *
 * ──────────────────────────────────────────────────────────────────────
 *  CRC HELPERS
 * ──────────────────────────────────────────────────────────────────────
 *
 *   char buf[64] = "0+1.23";
 *   SDI12_CRC_APPEND(buf);
 *   if (SDI12_CRC_VERIFY(buf)) { ... }
 *
 * @note Including this header automatically includes sdi12.h,
 *       sdi12_sensor.h, and sdi12_master.h.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SDI12_EASY_H
#define SDI12_EASY_H

#include "sdi12.h"
#include "sdi12_sensor.h"
#include "sdi12_master.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 *  SENSOR MACROS
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Declare a sensor with all required configuration.
 *
 * Creates a static sensor context and stores the callback + identity info.
 * Call SDI12_SENSOR_SETUP() once at startup to initialise it.
 *
 * @param name       A C identifier for this sensor (used as variable prefix).
 * @param addr       Initial SDI-12 address character ('0'–'9', 'A'–'Z', 'a'–'z').
 * @param vendor     Vendor name string (max 8 chars, space-padded by macro).
 * @param model      Model string (max 6 chars).
 * @param fw_ver     Firmware version string (max 3 chars).
 * @param serial     Serial number string (max 13 chars).
 * @param send_fn    void fn(const char *data, size_t len, void *user_data)
 * @param dir_fn     void fn(sdi12_dir_t dir, void *user_data)
 * @param read_fn    sdi12_value_t fn(uint8_t param_index, void *user_data)
 */
#define SDI12_SENSOR_DEFINE(name, addr, vendor, model, fw_ver, serial, \
                            send_fn, dir_fn, read_fn) \
    static sdi12_sensor_ctx_t name##_ctx; \
    static const char  name##_addr    = (addr); \
    static const char *name##_vendor  = (vendor); \
    static const char *name##_model   = (model); \
    static const char *name##_fwver   = (fw_ver); \
    static const char *name##_serial  = (serial); \
    static sdi12_send_response_fn  name##_send  = (send_fn); \
    static sdi12_set_direction_fn  name##_dir   = (dir_fn); \
    static sdi12_read_param_fn     name##_read  = (read_fn)

/**
 * @brief Initialise the sensor (call once in setup / main).
 *
 * Populates the identity struct, wires up callbacks, and calls
 * sdi12_sensor_init(). Stores the error in name##_err if you need it.
 */
#define SDI12_SENSOR_SETUP(name) \
    do { \
        sdi12_ident_t name##_id; \
        memset(&name##_id, ' ', sizeof(name##_id)); \
        name##_id.vendor[SDI12_ID_VENDOR_LEN] = '\0'; \
        name##_id.model[SDI12_ID_MODEL_LEN] = '\0'; \
        name##_id.firmware_version[SDI12_ID_FWVER_LEN] = '\0'; \
        name##_id.serial[0] = '\0'; \
        { size_t _n = strlen(name##_vendor); \
          if (_n > SDI12_ID_VENDOR_LEN) _n = SDI12_ID_VENDOR_LEN; \
          memcpy(name##_id.vendor, name##_vendor, _n); } \
        { size_t _n = strlen(name##_model); \
          if (_n > SDI12_ID_MODEL_LEN) _n = SDI12_ID_MODEL_LEN; \
          memcpy(name##_id.model, name##_model, _n); } \
        { size_t _n = strlen(name##_fwver); \
          if (_n > SDI12_ID_FWVER_LEN) _n = SDI12_ID_FWVER_LEN; \
          memcpy(name##_id.firmware_version, name##_fwver, _n); } \
        strncpy(name##_id.serial, name##_serial, SDI12_ID_SERIAL_MAXLEN); \
        name##_id.serial[SDI12_ID_SERIAL_MAXLEN] = '\0'; \
        \
        sdi12_sensor_callbacks_t name##_cb; \
        memset(&name##_cb, 0, sizeof(name##_cb)); \
        name##_cb.send_response = name##_send; \
        name##_cb.set_direction = name##_dir; \
        name##_cb.read_param    = name##_read; \
        \
        sdi12_sensor_init(&name##_ctx, name##_addr, &name##_id, &name##_cb); \
    } while (0)

/**
 * @brief Initialise the sensor with optional address persistence callbacks.
 *
 * Same as SDI12_SENSOR_SETUP but also wires save/load address callbacks
 * so the sensor remembers its address after power cycles.
 *
 * @param save_fn  void fn(char address, void *user_data) — write to EEPROM/flash
 * @param load_fn  char fn(void *user_data) — read from EEPROM/flash
 */
#define SDI12_SENSOR_SETUP_WITH_STORAGE(name, save_fn, load_fn) \
    do { \
        sdi12_ident_t name##_id; \
        memset(&name##_id, ' ', sizeof(name##_id)); \
        name##_id.vendor[SDI12_ID_VENDOR_LEN] = '\0'; \
        name##_id.model[SDI12_ID_MODEL_LEN] = '\0'; \
        name##_id.firmware_version[SDI12_ID_FWVER_LEN] = '\0'; \
        name##_id.serial[0] = '\0'; \
        { size_t _n = strlen(name##_vendor); \
          if (_n > SDI12_ID_VENDOR_LEN) _n = SDI12_ID_VENDOR_LEN; \
          memcpy(name##_id.vendor, name##_vendor, _n); } \
        { size_t _n = strlen(name##_model); \
          if (_n > SDI12_ID_MODEL_LEN) _n = SDI12_ID_MODEL_LEN; \
          memcpy(name##_id.model, name##_model, _n); } \
        { size_t _n = strlen(name##_fwver); \
          if (_n > SDI12_ID_FWVER_LEN) _n = SDI12_ID_FWVER_LEN; \
          memcpy(name##_id.firmware_version, name##_fwver, _n); } \
        strncpy(name##_id.serial, name##_serial, SDI12_ID_SERIAL_MAXLEN); \
        name##_id.serial[SDI12_ID_SERIAL_MAXLEN] = '\0'; \
        \
        sdi12_sensor_callbacks_t name##_cb; \
        memset(&name##_cb, 0, sizeof(name##_cb)); \
        name##_cb.send_response = name##_send; \
        name##_cb.set_direction = name##_dir; \
        name##_cb.read_param    = name##_read; \
        name##_cb.save_address  = (save_fn); \
        name##_cb.load_address  = (load_fn); \
        \
        sdi12_sensor_init(&name##_ctx, name##_addr, &name##_id, &name##_cb); \
    } while (0)

/**
 * @brief Register a measurement parameter.
 *
 * @param name      Sensor name (from SDI12_SENSOR_DEFINE).
 * @param group     Measurement group (0 = aM!/aC!, 1–9 = aM1!/aC1!, …).
 * @param shef      SHEF code string, e.g. "TA".
 * @param units     Units string, e.g. "degC".
 * @param decimals  Decimal places (0–7).
 */
#define SDI12_SENSOR_ADD_PARAM(name, group, shef, units, decimals) \
    sdi12_sensor_register_param(&name##_ctx, (group), (shef), (units), (decimals))

/**
 * @brief Register an extended command handler.
 *
 * @param name    Sensor name.
 * @param prefix  Command prefix after 'X' (e.g. "RST" for aXRST!).
 * @param handler sdi12_err_t fn(const char *xcmd, char *resp, size_t len, void *ud)
 */
#define SDI12_SENSOR_ADD_XCMD(name, prefix, handler) \
    sdi12_sensor_register_xcmd(&name##_ctx, (prefix), (handler))

/**
 * @brief Process a received SDI-12 command.
 *
 * Call this from your UART RX handler when a complete command arrives.
 */
#define SDI12_SENSOR_PROCESS(name, cmd, len) \
    sdi12_sensor_process(&name##_ctx, (cmd), (len))

/**
 * @brief Notify that an async measurement is complete.
 */
#define SDI12_SENSOR_MEAS_DONE(name, values, count) \
    sdi12_sensor_measurement_done(&name##_ctx, (values), (count))

/**
 * @brief Notify that a break signal was detected.
 */
#define SDI12_SENSOR_BREAK(name) \
    sdi12_sensor_break(&name##_ctx)

/**
 * @brief Get the current sensor address.
 */
#define SDI12_SENSOR_ADDRESS(name) \
    sdi12_sensor_get_address(&name##_ctx)

/**
 * @brief Get a pointer to the raw sensor context (for advanced use).
 */
#define SDI12_SENSOR_CTX(name) (&name##_ctx)

/* ═══════════════════════════════════════════════════════════════════════════
 *  MASTER MACROS
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Declare a master (data recorder) with all required callbacks.
 *
 * @param name      A C identifier for this master.
 * @param send_fn   void fn(const char *data, size_t len, void *ud)
 * @param recv_fn   size_t fn(char *buf, size_t max, uint32_t timeout_ms, void *ud)
 * @param dir_fn    void fn(sdi12_dir_t dir, void *ud)
 * @param break_fn  void fn(void *ud)
 * @param delay_fn  void fn(uint32_t ms, void *ud)
 */
#define SDI12_MASTER_DEFINE(name, send_fn, recv_fn, dir_fn, break_fn, delay_fn) \
    static sdi12_master_ctx_t name##_ctx; \
    static sdi12_master_send_fn       name##_send  = (send_fn); \
    static sdi12_master_recv_fn       name##_recv  = (recv_fn); \
    static sdi12_master_set_dir_fn    name##_dir   = (dir_fn); \
    static sdi12_master_send_break_fn name##_brk   = (break_fn); \
    static sdi12_master_delay_fn      name##_dly   = (delay_fn)

/**
 * @brief Initialise the master (call once in setup / main).
 */
#define SDI12_MASTER_SETUP(name) \
    do { \
        sdi12_master_callbacks_t name##_cb; \
        memset(&name##_cb, 0, sizeof(name##_cb)); \
        name##_cb.send          = name##_send; \
        name##_cb.recv          = name##_recv; \
        name##_cb.set_direction = name##_dir; \
        name##_cb.send_break    = name##_brk; \
        name##_cb.delay         = name##_dly; \
        sdi12_master_init(&name##_ctx, &name##_cb); \
    } while (0)

/** @brief Send a break signal to wake the bus. */
#define SDI12_MASTER_BREAK(name) \
    sdi12_master_send_break(&name##_ctx)

/** @brief Check if a sensor is present at an address. Returns bool via `present`. */
#define SDI12_MASTER_PING(name, addr, present) \
    sdi12_master_acknowledge(&name##_ctx, (addr), (present))

/** @brief Identify a sensor. Fills an sdi12_ident_t struct. */
#define SDI12_MASTER_IDENTIFY(name, addr, ident_ptr) \
    sdi12_master_identify(&name##_ctx, (addr), (ident_ptr))

/** @brief Start a standard measurement (aM!). */
#define SDI12_MASTER_MEASURE(name, addr, resp_ptr) \
    sdi12_master_start_measurement(&name##_ctx, (addr), \
        SDI12_MEAS_STANDARD, 0, false, (resp_ptr))

/** @brief Start a measurement with CRC (aMC!). */
#define SDI12_MASTER_MEASURE_CRC(name, addr, resp_ptr) \
    sdi12_master_start_measurement(&name##_ctx, (addr), \
        SDI12_MEAS_STANDARD, 0, true, (resp_ptr))

/** @brief Wait for a service request after M command. */
#define SDI12_MASTER_WAIT(name, addr, timeout_ms) \
    sdi12_master_wait_service_request(&name##_ctx, (addr), (timeout_ms))

/** @brief Get data page (aD0!–aD9!). */
#define SDI12_MASTER_GET_DATA(name, addr, page, crc, resp_ptr) \
    sdi12_master_get_data(&name##_ctx, (addr), (page), (crc), (resp_ptr))

/** @brief Read continuous measurement (aR0!). */
#define SDI12_MASTER_CONTINUOUS(name, addr, index, resp_ptr) \
    sdi12_master_continuous(&name##_ctx, (addr), (index), false, (resp_ptr))

/** @brief Change a sensor's address (aAb!). */
#define SDI12_MASTER_CHANGE_ADDR(name, old_addr, new_addr) \
    sdi12_master_change_address(&name##_ctx, (old_addr), (new_addr))

/** @brief Get a pointer to the raw master context (for advanced use). */
#define SDI12_MASTER_CTX(name) (&name##_ctx)

/* ═══════════════════════════════════════════════════════════════════════════
 *  CRC HELPERS
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Append CRC + CRLF to a char buffer. Returns sdi12_err_t. */
#define SDI12_CRC_APPEND(buf) \
    sdi12_crc_append((buf), sizeof(buf))

/** @brief Verify CRC on a received string. Returns bool. */
#define SDI12_CRC_VERIFY(buf) \
    sdi12_crc_verify((buf), strlen(buf))

#ifdef __cplusplus
}
#endif

#endif /* SDI12_EASY_H */
