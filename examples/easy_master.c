/**
 * @file easy_master.c
 * @brief Minimal SDI-12 data recorder using sdi12_easy.h macros.
 *
 * Shows the simplest way to scan, measure, and read SDI-12 sensors.
 *
 * Compile check:
 *   gcc -std=c11 -fsyntax-only -I.. easy_master.c
 */
#include "sdi12_easy.h"
#include <stdio.h>

/* ── Step 1: Write your hardware functions ─────────────────────────────── */

void my_send(const char *data, size_t len, void *ud) {
    (void)ud; (void)data; (void)len;
    /* TODO: uart_write(data, len); uart_flush(); */
}

size_t my_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud) {
    (void)ud; (void)buf; (void)max; (void)timeout_ms;
    /* TODO: return uart_read_until_crlf(buf, max, timeout_ms); */
    return 0;
}

void my_dir(sdi12_dir_t dir, void *ud) {
    (void)ud; (void)dir;
    /* TODO: gpio_write(DIR_PIN, dir == SDI12_DIR_TX); */
}

void my_break(void *ud) {
    (void)ud;
    /* TODO: hold SDI-12 line high for ≥12ms */
}

void my_delay(uint32_t ms, void *ud) {
    (void)ud; (void)ms;
    /* TODO: delay_ms(ms); */
}

/* ── Step 2: Define your master ────────────────────────────────────────── */

SDI12_MASTER_DEFINE(recorder, my_send, my_recv, my_dir, my_break, my_delay);

/* ── Step 3: Use it ────────────────────────────────────────────────────── */

void setup(void) {
    SDI12_MASTER_SETUP(recorder);
}

void read_sensor(char addr) {
    /* Wake the bus */
    SDI12_MASTER_BREAK(recorder);

    /* Check if sensor is present */
    bool present = false;
    SDI12_MASTER_PING(recorder, addr, &present);
    if (!present) {
        printf("No sensor at '%c'\n", addr);
        return;
    }

    /* Identify it */
    sdi12_ident_t id;
    SDI12_MASTER_IDENTIFY(recorder, addr, &id);
    printf("Sensor: %.8s %.6s\n", id.vendor, id.model);

    /* Take a measurement */
    sdi12_meas_response_t mresp;
    SDI12_MASTER_MEASURE(recorder, addr, &mresp);
    printf("Wait %ds for %d values\n", mresp.wait_seconds, mresp.value_count);

    /* Wait if needed */
    if (mresp.wait_seconds > 0) {
        SDI12_MASTER_WAIT(recorder, addr,
                          (uint32_t)mresp.wait_seconds * 1000 + 1000);
    }

    /* Read data */
    sdi12_data_response_t dresp;
    SDI12_MASTER_GET_DATA(recorder, addr, 0, false, &dresp);

    for (uint8_t i = 0; i < dresp.value_count; i++) {
        printf("  Value[%d] = %.*f\n", i,
               dresp.values[i].decimals,
               (double)dresp.values[i].value);
    }
}

/*
 * That's it! To read sensor at address '0':
 *   setup();
 *   read_sensor('0');
 */
