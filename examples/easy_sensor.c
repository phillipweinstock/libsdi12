/**
 * @file easy_sensor.c
 * @brief Minimal SDI-12 sensor using sdi12_easy.h macros.
 *
 * This shows the simplest possible way to make an SDI-12 sensor.
 * Compare this to examples/example_sensor.c to see how much
 * boilerplate the macros eliminate.
 *
 * Compile check:
 *   gcc -std=c11 -fsyntax-only -I.. easy_sensor.c
 */
#include "sdi12_easy.h"

/* ── Step 1: Write your hardware functions ─────────────────────────────── */

void my_send(const char *data, size_t len, void *ud) {
    (void)ud;
    /* TODO: transmit data on your SDI-12 UART */
    (void)data; (void)len;
}

void my_dir(sdi12_dir_t dir, void *ud) {
    (void)ud;
    /* TODO: set your direction pin HIGH for TX, LOW for RX */
    (void)dir;
}

sdi12_value_t my_read(uint8_t idx, void *ud) {
    (void)ud;
    sdi12_value_t v = {0.0f, 0};
    switch (idx) {
    case 0: v.value = 22.5f;  v.decimals = 2; break;  /* Temperature */
    case 1: v.value = 65.0f;  v.decimals = 1; break;  /* Humidity    */
    case 2: v.value = 101.3f; v.decimals = 1; break;  /* Pressure    */
    }
    return v;
}

/* ── Step 2: Define your sensor ────────────────────────────────────────── */

SDI12_SENSOR_DEFINE(weather, '0',
    "WEATHER",   /* Vendor  (max 8 chars)  */
    "WX3000",    /* Model   (max 6 chars)  */
    "110",       /* Version (max 3 chars)  */
    "SN-00042",  /* Serial  (max 13 chars) */
    my_send, my_dir, my_read);

/* ── Step 3: Initialise and register params ────────────────────────────── */

void setup(void) {
    SDI12_SENSOR_SETUP(weather);

    SDI12_SENSOR_ADD_PARAM(weather, 0, "TA", "degC", 2);  /* Temperature */
    SDI12_SENSOR_ADD_PARAM(weather, 0, "RH", "%RH",  1);  /* Humidity    */
    SDI12_SENSOR_ADD_PARAM(weather, 0, "PA", "kPa",  1);  /* Pressure    */
}

/* ── Step 4: Feed commands from your UART ──────────────────────────────── */

void on_sdi12_command(const char *cmd, size_t len) {
    SDI12_SENSOR_PROCESS(weather, cmd, len);
}

void on_sdi12_break(void) {
    SDI12_SENSOR_BREAK(weather);
}

/*
 * That's it! Your sensor now responds to:
 *   "0!"     → "0\r\n"                       (acknowledge)
 *   "0I!"    → "014WEATHER WX3000110SN-00042\r\n"  (identify)
 *   "0M!"    → "00003\r\n"                   (measure: 3 values)
 *   "0D0!"   → "0+22.50+65.0+101.3\r\n"     (send data)
 *   "0MC!"   → same but data includes CRC
 *   "0R0!"   → immediate continuous reading
 *   "0A5!"   → change address to '5'
 */
