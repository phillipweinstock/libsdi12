/**
 * @file example_sensor.c
 * @brief Example: Implement an SDI-12 sensor with libsdi12.
 *
 * This example shows how to build a weather station sensor that responds
 * to SDI-12 commands. It reads temperature and humidity, supports CRC,
 * address changes, and extended commands.
 *
 * Platform stubs are marked with TODO — replace with your hardware code.
 *
 * Compile check (no linker — just syntax):
 *   gcc -std=c11 -fsyntax-only -I.. example_sensor.c
 */
#include "sdi12.h"
#include "sdi12_sensor.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  Platform stubs — replace these with your real hardware functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/* TODO: Replace with your UART transmit function */
static void uart_write(const char *data, size_t len)      { (void)data; (void)len; }
static void uart_flush(void)                               { }

/* TODO: Replace with your GPIO function */
static void gpio_set_pin(int pin, int level)               { (void)pin; (void)level; }

/* TODO: Replace with your sensor read functions */
static float read_temperature(void)                        { return 22.5f; }
static float read_humidity(void)                           { return 55.3f; }
static float read_pressure(void)                           { return 101.3f; }

/* TODO: Replace with your EEPROM/flash functions */
static char  eeprom_read_address(void)                     { return '0'; }
static void  eeprom_write_address(char addr)               { (void)addr; }

#define DIR_PIN   7
#define DIR_HIGH  1
#define DIR_LOW   0

/* ═══════════════════════════════════════════════════════════════════════════
 *  SDI-12 Callbacks
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Send a response on the SDI-12 bus.
 * The library provides a fully formatted string including CR/LF.
 */
static void cb_send_response(const char *data, size_t len, void *user_data)
{
    (void)user_data;
    gpio_set_pin(DIR_PIN, DIR_HIGH);    /* Switch to TX */
    uart_write(data, len);
    uart_flush();
    gpio_set_pin(DIR_PIN, DIR_LOW);     /* Switch back to RX */
}

/**
 * Control the bus direction pin.
 */
static void cb_set_direction(sdi12_dir_t dir, void *user_data)
{
    (void)user_data;
    gpio_set_pin(DIR_PIN, dir == SDI12_DIR_TX ? DIR_HIGH : DIR_LOW);
}

/**
 * Read a measurement parameter by index.
 * Return the current value with the desired decimal precision.
 */
static sdi12_value_t cb_read_param(uint8_t param_index, void *user_data)
{
    (void)user_data;
    sdi12_value_t val = {0.0f, 0};

    switch (param_index) {
    case 0: val.value = read_temperature(); val.decimals = 2; break;
    case 1: val.value = read_humidity();    val.decimals = 1; break;
    case 2: val.value = read_pressure();    val.decimals = 1; break;
    default: break;
    }
    return val;
}

/**
 * Persist the address when changed via aAb! command.
 */
static void cb_save_address(char address, void *user_data)
{
    (void)user_data;
    eeprom_write_address(address);
}

/**
 * Load the address from non-volatile storage on startup.
 */
static char cb_load_address(void *user_data)
{
    (void)user_data;
    return eeprom_read_address();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Extended Command: Factory Reset
 * ═══════════════════════════════════════════════════════════════════════════ */

static sdi12_err_t xcmd_reset(const char *xcmd, char *resp,
                               size_t resp_size, void *user_data)
{
    (void)xcmd;
    (void)user_data;
    size_t pos = strlen(resp);  /* address already at resp[0] */
    snprintf(resp + pos, resp_size - pos, "RESET_OK");
    /* TODO: schedule a system reset after response is sent */
    return SDI12_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Main
 * ═══════════════════════════════════════════════════════════════════════════ */

static sdi12_sensor_ctx_t sensor;

void sensor_setup(void)
{
    /* ── Identity ──────────────────────────────────────────────────────── */
    sdi12_ident_t ident;
    memset(&ident, 0, sizeof(ident));
    memcpy(ident.vendor, "WEATHER ", SDI12_ID_VENDOR_LEN);  /* 8 chars */
    memcpy(ident.model,  "WX3000", SDI12_ID_MODEL_LEN);     /* 6 chars */
    memcpy(ident.firmware_version, "110", SDI12_ID_FWVER_LEN); /* 3 chars */
    strncpy(ident.serial, "SN-00042", sizeof(ident.serial) - 1);

    /* ── Callbacks ─────────────────────────────────────────────────────── */
    sdi12_sensor_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.send_response = cb_send_response;
    cb.set_direction = cb_set_direction;
    cb.read_param    = cb_read_param;
    cb.save_address  = cb_save_address;  /* optional: persist address */
    cb.load_address  = cb_load_address;  /* optional: restore on boot */

    /* ── Initialize ────────────────────────────────────────────────────── */
    sdi12_err_t err = sdi12_sensor_init(&sensor, '0', &ident, &cb);
    if (err != SDI12_OK) {
        /* Handle error — invalid address, missing callbacks, etc. */
        return;
    }

    /* ── Register measurement parameters ───────────────────────────────── */
    /* Group 0: main measurements (aM!, aMC!, aC!, aCC!, aR0!) */
    sdi12_sensor_register_param(&sensor, 0, "TA", "degC", 2);  /* Temperature */
    sdi12_sensor_register_param(&sensor, 0, "RH", "%RH",  1);  /* Humidity    */
    sdi12_sensor_register_param(&sensor, 0, "PA", "kPa",  1);  /* Pressure    */

    /* ── Register extended commands ────────────────────────────────────── */
    sdi12_sensor_register_xcmd(&sensor, "RST", xcmd_reset);
    /* Sensor now responds to "0XRST!" with "0RESET_OK\r\n" */
}

/**
 * Call this from your UART RX interrupt / main loop when a complete
 * SDI-12 command has been received (terminated by '!').
 */
void sensor_on_command(const char *cmd, size_t len)
{
    sdi12_sensor_process(&sensor, cmd, len);
}

/**
 * Call this when you detect a break signal (≥12ms spacing on the bus).
 */
void sensor_on_break(void)
{
    sdi12_sensor_break(&sensor);
}

/*
 * Example commands this sensor handles:
 *
 *   "0!"      → Acknowledge:  "0\r\n"
 *   "?!"      → Query address: "0\r\n"
 *   "0I!"     → Identify:     "014WEATHER WX3000110SN-00042\r\n"
 *   "0M!"     → Measure:      "00003\r\n" (3 values, 0s wait)
 *   "0D0!"    → Send data:    "0+22.50+55.3+101.3\r\n"
 *   "0MC!"    → Measure+CRC:  "00003\r\n" (data D0 will include CRC)
 *   "0R0!"    → Continuous:   "0+22.50+55.3+101.3\r\n"
 *   "0A5!"    → Change addr:  "5\r\n" (now responds to '5')
 *   "0XRST!"  → Extended:     "0RESET_OK\r\n"
 */
