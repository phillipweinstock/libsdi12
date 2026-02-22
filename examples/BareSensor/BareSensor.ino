/**
 * BareSensor.ino — SDI-12 sensor using the raw API (no sdi12_easy.h macros).
 *
 * This sketch demonstrates the full, explicit API surface:
 *   • Manual sdi12_sensor_ctx_t / sdi12_ident_t / sdi12_sensor_callbacks_t
 *   • sdi12_sensor_init(), sdi12_sensor_register_param()
 *   • sdi12_sensor_process(), sdi12_sensor_break()
 *   • sdi12_sensor_measurement_done()  (async measurement completion)
 *   • sdi12_sensor_register_xcmd()     (extended command handler)
 *
 * Compare with EasySensor.ino which uses the convenience macros from
 * sdi12_easy.h to achieve the same result with less boilerplate.
 *
 * Wiring (Arduino Due / Mega / ESP32):
 *   - SDI-12 data line → Serial1 TX/RX (via level shifter)
 *   - Direction pin     → digital pin 2
 */
#include <sdi12.h>
#include <sdi12_sensor.h>
#include <string.h>
#include <stdio.h>

/* ── Configuration ─────────────────────────────────────────────────── */

#define SDI12_UART      Serial1
#define DIR_PIN         2
#define SENSOR_ADDR     '0'

/* ── Forward declarations ──────────────────────────────────────────── */

static void     cb_send_response(const char *data, size_t len, void *ud);
static void     cb_set_direction(sdi12_dir_t dir, void *ud);
static sdi12_value_t cb_read_param(uint8_t idx, void *ud);
static void     cb_save_address(char addr, void *ud);
static char     cb_load_address(void *ud);
static sdi12_err_t xcmd_info(const char *xcmd, char *resp,
                              size_t resp_size, void *ud);

/* ── Global sensor context ─────────────────────────────────────────── */

static sdi12_sensor_ctx_t sensor;

/* ══════════════════════════════════════════════════════════════════════
 *  CALLBACKS — every hardware interaction goes through these
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Transmit a fully formatted response (includes CR/LF) on the bus.
 */
static void cb_send_response(const char *data, size_t len, void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, HIGH);        /* TX mode */
    SDI12_UART.write(data, len);
    SDI12_UART.flush();                 /* wait for all bytes */
    digitalWrite(DIR_PIN, LOW);         /* back to RX */
}

/**
 * Switch the RS-485 / level-shifter direction.
 */
static void cb_set_direction(sdi12_dir_t dir, void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, dir == SDI12_DIR_TX ? HIGH : LOW);
}

/**
 * Return the current value for measurement parameter `idx`.
 * Called by the library during M, C, and R command processing.
 */
static sdi12_value_t cb_read_param(uint8_t idx, void *ud) {
    (void)ud;
    sdi12_value_t v = {0.0f, 0};

    switch (idx) {
        case 0:                         /* Temperature from A0 */
            v.value    = analogRead(A0) * 0.1f;
            v.decimals = 2;
            break;
        case 1:                         /* Humidity from A1 */
            v.value    = analogRead(A1) * 0.05f;
            v.decimals = 1;
            break;
        case 2:                         /* Battery voltage from A2 */
            v.value    = analogRead(A2) * 3.3f / 4095.0f;
            v.decimals = 3;
            break;
    }
    return v;
}

/**
 * Persist the address when changed via aAb! command.
 * Replace with EEPROM / flash writes for your platform.
 */
static char saved_addr = SENSOR_ADDR;

static void cb_save_address(char addr, void *ud) {
    (void)ud;
    saved_addr = addr;
    Serial.print("[Bare] Address changed to '");
    Serial.print(addr);
    Serial.println("'");
}

/**
 * Load the address from non-volatile storage on startup.
 */
static char cb_load_address(void *ud) {
    (void)ud;
    return saved_addr;
}

/* ── Extended command: XINFO! ──────────────────────────────────────── */

/**
 * Handle "aXINFO!" — returns a custom status string.
 */
static sdi12_err_t xcmd_info(const char *xcmd, char *resp,
                              size_t resp_size, void *ud) {
    (void)xcmd; (void)ud;
    size_t pos = strlen(resp);          /* address already at resp[0] */
    snprintf(resp + pos, resp_size - pos,
             "BARE_SENSOR,uptime=%lu", millis() / 1000);
    return SDI12_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 *  SETUP — explicit struct initialization, no macros
 * ══════════════════════════════════════════════════════════════════════ */

void setup() {
    Serial.begin(115200);
    SDI12_UART.begin(1200, SERIAL_7E1);

    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(DIR_PIN, LOW);         /* start in RX mode */

    /* ── Build the identification structure ─────────────────────────── */
    sdi12_ident_t ident;
    memset(&ident, 0, sizeof(ident));
    memcpy(ident.vendor,           "MYCO    ", SDI12_ID_VENDOR_LEN);
    memcpy(ident.model,            "BARE01",   SDI12_ID_MODEL_LEN);
    memcpy(ident.firmware_version, "100",      SDI12_ID_FWVER_LEN);
    strncpy(ident.serial, "SN-99001", sizeof(ident.serial) - 1);

    /* ── Fill in the callback table ────────────────────────────────── */
    sdi12_sensor_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.send_response = cb_send_response;
    cb.set_direction = cb_set_direction;
    cb.read_param    = cb_read_param;
    cb.save_address  = cb_save_address;     /* optional: persist address */
    cb.load_address  = cb_load_address;     /* optional: restore on boot */

    /* ── Initialize the sensor context ─────────────────────────────── */
    sdi12_err_t err = sdi12_sensor_init(&sensor, SENSOR_ADDR, &ident, &cb);
    if (err != SDI12_OK) {
        Serial.println("[Bare] sdi12_sensor_init FAILED");
        while (1) ;                         /* halt on fatal error */
    }

    /* ── Register measurement parameters ───────────────────────────── */
    /* Group 0: responds to aM!, aMC!, aC!, aCC!, aR0! */
    sdi12_sensor_register_param(&sensor, 0, "TA", "degC", 2);
    sdi12_sensor_register_param(&sensor, 0, "RH", "%RH",  1);
    sdi12_sensor_register_param(&sensor, 0, "VB", "V",    3);

    /* ── Register an extended command ──────────────────────────────── */
    sdi12_sensor_register_xcmd(&sensor, "INFO", xcmd_info);

    Serial.println("[Bare] SDI-12 sensor ready (raw API)");
}

/* ══════════════════════════════════════════════════════════════════════
 *  LOOP — poll UART and feed commands to the library
 * ══════════════════════════════════════════════════════════════════════ */

static char cmd_buf[80];
static uint8_t cmd_len = 0;

void loop() {
    while (SDI12_UART.available()) {
        char c = SDI12_UART.read();

        if (c == '!' || c == '?') {
            cmd_buf[cmd_len++] = c;
            cmd_buf[cmd_len]   = '\0';

            Serial.print("[Bare] RX: ");
            Serial.println(cmd_buf);

            /* Feed the complete command to the library */
            sdi12_sensor_process(&sensor, cmd_buf, cmd_len);
            cmd_len = 0;
        } else if (cmd_len < sizeof(cmd_buf) - 2) {
            cmd_buf[cmd_len++] = c;
        }
    }
}

/*
 * Example commands this sketch handles:
 *
 *   "0!"       → Acknowledge:   "0\r\n"
 *   "?!"       → Query address: "0\r\n"
 *   "0I!"      → Identify:      "014MYCO    BARE01100SN-99001\r\n"
 *   "0M!"      → Measure:       "00003\r\n"  (3 values, 0s wait)
 *   "0D0!"     → Send data:     "0+22.50+55.3+0.812\r\n"
 *   "0MC!"     → Measure+CRC:   "00003\r\n"  (D0 includes CRC)
 *   "0R0!"     → Continuous:    "0+22.50+55.3+0.812\r\n"
 *   "0A5!"     → Change addr:   "5\r\n"
 *   "0XINFO!"  → Extended:      "0BARE_SENSOR,uptime=42\r\n"
 */
