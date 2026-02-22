/**
 * @file example_master.c
 * @brief Example: Implement an SDI-12 data recorder (master) with libsdi12.
 *
 * This example shows how to scan the bus, identify sensors, take
 * measurements, and read data — the full data recorder workflow.
 *
 * Platform stubs are marked with TODO — replace with your hardware code.
 *
 * Compile check (no linker — just syntax):
 *   gcc -std=c11 -fsyntax-only -I.. example_master.c
 */
#include "sdi12.h"
#include "sdi12_master.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  Platform stubs — replace these with your real hardware functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/* TODO: Transmit bytes at 1200 baud, 7E1, inverted logic */
static void platform_send(const char *data, size_t len, void *ud)
{
    (void)ud; (void)data; (void)len;
    /* uart_write(data, len); uart_flush(); */
}

/* TODO: Receive bytes with timeout. Return number of bytes read. */
static size_t platform_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud)
{
    (void)ud; (void)buf; (void)max; (void)timeout_ms;
    /* return uart_read_until_crlf(buf, max, timeout_ms); */
    return 0;
}

/* TODO: Switch between TX and RX mode */
static void platform_set_dir(sdi12_dir_t dir, void *ud)
{
    (void)ud; (void)dir;
    /* gpio_write(DIR_PIN, dir == SDI12_DIR_TX ? 1 : 0); */
}

/* TODO: Hold the bus in spacing state for ≥12ms */
static void platform_break(void *ud)
{
    (void)ud;
    /* gpio_hold_high(SDI12_PIN, 12); */
}

/* TODO: Blocking delay */
static void platform_delay(uint32_t ms, void *ud)
{
    (void)ud; (void)ms;
    /* delay_ms(ms); */
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Data Recorder Application
 * ═══════════════════════════════════════════════════════════════════════════ */

static sdi12_master_ctx_t master;

void master_setup(void)
{
    sdi12_master_callbacks_t cb = {
        .send          = platform_send,
        .recv          = platform_recv,
        .set_direction = platform_set_dir,
        .send_break    = platform_break,
        .delay         = platform_delay,
        .user_data     = NULL,
    };

    sdi12_master_init(&master, &cb);
}

/**
 * Scan the bus for all sensors and print their addresses.
 */
void master_scan_bus(void)
{
    printf("Scanning SDI-12 bus...\n");
    sdi12_master_send_break(&master);

    const char addrs[] = "0123456789"
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz";

    for (size_t i = 0; i < sizeof(addrs) - 1; i++) {
        bool present = false;
        sdi12_master_acknowledge(&master, addrs[i], &present);
        if (present) {
            printf("  Found sensor at address '%c'\n", addrs[i]);
        }
    }
}

/**
 * Read identification from a sensor and print it.
 */
void master_identify_sensor(char addr)
{
    sdi12_ident_t ident;
    sdi12_err_t err = sdi12_master_identify(&master, addr, &ident);

    if (err == SDI12_OK) {
        printf("Sensor '%c':\n", addr);
        printf("  Vendor:   %.8s\n", ident.vendor);
        printf("  Model:    %.6s\n", ident.model);
        printf("  Firmware: %.3s\n", ident.firmware_version);
        printf("  Serial:   %s\n",  ident.serial);
    } else {
        printf("Failed to identify sensor '%c' (error %d)\n", addr, err);
    }
}

/**
 * Take a standard measurement (aM! → wait → aD0!).
 */
void master_measure(char addr)
{
    /* Wake the bus */
    sdi12_master_send_break(&master);

    /* Start measurement */
    sdi12_meas_response_t mresp;
    sdi12_err_t err = sdi12_master_start_measurement(
        &master, addr, SDI12_MEAS_STANDARD, 0, false, &mresp);

    if (err != SDI12_OK) {
        printf("Measurement command failed (error %d)\n", err);
        return;
    }

    printf("Sensor '%c': wait %ds for %d values\n",
           mresp.address, mresp.wait_seconds, mresp.value_count);

    /* Wait for the sensor to complete if needed */
    if (mresp.wait_seconds > 0) {
        err = sdi12_master_wait_service_request(
            &master, addr, (uint32_t)mresp.wait_seconds * 1000 + 1000);
        if (err != SDI12_OK) {
            printf("Service request timeout\n");
            return;
        }
    }

    /* Retrieve data (may need multiple pages for >9 values) */
    uint8_t total = 0;
    for (uint8_t page = 0; total < mresp.value_count && page < 10; page++) {
        sdi12_data_response_t dresp;
        err = sdi12_master_get_data(&master, addr, page, false, &dresp);
        if (err != SDI12_OK) break;

        for (uint8_t i = 0; i < dresp.value_count; i++) {
            printf("  Value[%d]: %.*f\n",
                   total + i,
                   dresp.values[i].decimals,
                   (double)dresp.values[i].value);
        }
        total += dresp.value_count;
    }
}

/**
 * Take a measurement with CRC verification (aMC! → aD0! with CRC check).
 */
void master_measure_with_crc(char addr)
{
    sdi12_master_send_break(&master);

    sdi12_meas_response_t mresp;
    sdi12_err_t err = sdi12_master_start_measurement(
        &master, addr, SDI12_MEAS_STANDARD, 0, true, &mresp);  /* crc=true */

    if (err != SDI12_OK) return;

    if (mresp.wait_seconds > 0) {
        sdi12_master_wait_service_request(
            &master, addr, (uint32_t)mresp.wait_seconds * 1000 + 1000);
    }

    sdi12_data_response_t dresp;
    err = sdi12_master_get_data(&master, addr, 0, true, &dresp);  /* crc=true */

    if (err == SDI12_ERR_CRC_MISMATCH) {
        printf("CRC verification failed! Data may be corrupt.\n");
        return;
    }

    printf("CRC-verified data from '%c':\n", addr);
    for (uint8_t i = 0; i < dresp.value_count; i++) {
        printf("  [%d] = %.*f\n", i,
               dresp.values[i].decimals,
               (double)dresp.values[i].value);
    }
}

/**
 * Read continuous measurement (aR0! — immediate response, no wait).
 */
void master_continuous(char addr)
{
    sdi12_data_response_t dresp;
    sdi12_err_t err = sdi12_master_continuous(&master, addr, 0, false, &dresp);

    if (err == SDI12_OK) {
        printf("Continuous data from '%c': %d values\n",
               addr, dresp.value_count);
        for (uint8_t i = 0; i < dresp.value_count; i++) {
            printf("  [%d] = %.2f\n", i, (double)dresp.values[i].value);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Pure Parsing (no hardware needed)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Demonstrate the pure parsing API — works without any callbacks.
 * Useful for processing stored/logged SDI-12 responses.
 */
void parse_stored_responses(void)
{
    printf("\n--- Parsing stored responses (no hardware) ---\n");

    /* Parse a measurement response: "00053" */
    sdi12_meas_response_t mresp;
    sdi12_master_parse_meas_response("00053", 5, SDI12_MEAS_STANDARD, &mresp);
    printf("M response: addr='%c', wait=%ds, count=%d\n",
           mresp.address, mresp.wait_seconds, mresp.value_count);

    /* Parse data values: "+22.50+55.3+101.3" */
    sdi12_value_t vals[10];
    uint8_t count = 0;
    const char *data = "+22.50+55.3+101.3";
    sdi12_master_parse_data_values(data, strlen(data), vals, 10, &count, false);

    printf("Parsed %d values:\n", count);
    for (uint8_t i = 0; i < count; i++) {
        printf("  [%d] = %.*f (%d decimals)\n", i,
               vals[i].decimals, (double)vals[i].value, vals[i].decimals);
    }

    /* Parse with CRC verification */
    char crc_data[64] = "+1.23-4.56";
    sdi12_crc_append(crc_data, sizeof(crc_data));  /* append CRC + CRLF */

    /* Strip the CRLF for parsing (master normally does this) */
    size_t len = strlen(crc_data);
    if (len >= 2) crc_data[len - 2] = '\0';

    count = 0;
    sdi12_err_t err = sdi12_master_parse_data_values(
        crc_data, strlen(crc_data), vals, 10, &count, true);
    printf("CRC-verified parse: %s, %d values\n",
           err == SDI12_OK ? "OK" : "FAILED", count);
}
