/**
 * BareMaster.ino — SDI-12 data recorder using the raw API (no macros).
 *
 * Demonstrates every master API function with explicit struct setup:
 *   • Manual sdi12_master_ctx_t / sdi12_master_callbacks_t initialization
 *   • sdi12_master_init(), sdi12_master_send_break()
 *   • sdi12_master_acknowledge(), sdi12_master_identify()
 *   • sdi12_master_start_measurement(), sdi12_master_wait_service_request()
 *   • sdi12_master_get_data(), sdi12_master_continuous()
 *   • sdi12_master_change_address()
 *   • CRC-verified measurements
 *
 * Compare with EasyMaster.ino which uses the convenience macros from
 * sdi12_easy.h to achieve the same result with less boilerplate.
 *
 * Wiring (Arduino Due / Mega / ESP32):
 *   - SDI-12 data line → Serial1 TX/RX (via level shifter)
 *   - Direction pin     → digital pin 2
 *
 * Open Serial Monitor at 115200 to see results.
 */
#include <sdi12.h>
#include <sdi12_master.h>
#include <string.h>

/* ── Configuration ─────────────────────────────────────────────────── */

#define SDI12_UART      Serial1
#define DIR_PIN         2
#define SENSOR_ADDR     '0'
#define SCAN_INTERVAL   15000   /* ms between measurement cycles */

/* ── Forward declarations ──────────────────────────────────────────── */

static void   cb_send(const char *data, size_t len, void *ud);
static size_t cb_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud);
static void   cb_set_dir(sdi12_dir_t dir, void *ud);
static void   cb_send_break(void *ud);
static void   cb_delay(uint32_t ms, void *ud);

/* ── Global master context ─────────────────────────────────────────── */

static sdi12_master_ctx_t master;

/* ══════════════════════════════════════════════════════════════════════
 *  CALLBACKS — every bus interaction goes through these
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Transmit raw bytes on the SDI-12 bus.
 */
static void cb_send(const char *data, size_t len, void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, HIGH);        /* TX mode */
    SDI12_UART.write(data, len);
    SDI12_UART.flush();
    digitalWrite(DIR_PIN, LOW);         /* back to RX */
}

/**
 * Receive bytes from the bus, blocking up to timeout_ms.
 * Returns when a newline arrives or the timeout expires.
 */
static size_t cb_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud) {
    (void)ud;
    unsigned long start = millis();
    size_t pos = 0;

    while ((millis() - start) < timeout_ms && pos < max) {
        if (SDI12_UART.available()) {
            char c = SDI12_UART.read();
            buf[pos++] = c;
            if (c == '\n') break;       /* response complete */
        }
    }
    return pos;
}

/**
 * Switch the RS-485 / level-shifter direction.
 */
static void cb_set_dir(sdi12_dir_t dir, void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, dir == SDI12_DIR_TX ? HIGH : LOW);
}

/**
 * Generate a break signal: ≥12 ms spacing + ≥8.33 ms marking.
 */
static void cb_send_break(void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, HIGH);
    SDI12_UART.end();
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);              /* spacing */
    delay(15);
    digitalWrite(1, LOW);               /* marking */
    delay(9);
    SDI12_UART.begin(1200, SERIAL_7E1);
    digitalWrite(DIR_PIN, LOW);
}

/**
 * Blocking delay (used for inter-command timing).
 */
static void cb_delay(uint32_t ms, void *ud) {
    (void)ud;
    delay(ms);
}

/* ══════════════════════════════════════════════════════════════════════
 *  SETUP — explicit callback table, no SDI12_MASTER_DEFINE macros
 * ══════════════════════════════════════════════════════════════════════ */

void setup() {
    Serial.begin(115200);
    SDI12_UART.begin(1200, SERIAL_7E1);

    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(DIR_PIN, LOW);

    /* ── Build the callback table ──────────────────────────────────── */
    sdi12_master_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.send          = cb_send;
    cb.recv          = cb_recv;
    cb.set_direction = cb_set_dir;
    cb.send_break    = cb_send_break;
    cb.delay         = cb_delay;
    cb.user_data     = NULL;

    /* ── Initialize the master context ─────────────────────────────── */
    sdi12_err_t err = sdi12_master_init(&master, &cb);
    if (err != SDI12_OK) {
        Serial.println("[Bare] sdi12_master_init FAILED");
        while (1) ;
    }

    Serial.println("[Bare] SDI-12 master ready (raw API)");
    Serial.print("[Bare] Scanning address '");
    Serial.print((char)SENSOR_ADDR);
    Serial.print("' every ");
    Serial.print(SCAN_INTERVAL / 1000);
    Serial.println("s");
}

/* ══════════════════════════════════════════════════════════════════════
 *  LOOP — periodic measurement cycle using raw API calls
 * ══════════════════════════════════════════════════════════════════════ */

static unsigned long next_scan = 0;

void loop() {
    if (millis() < next_scan) return;
    next_scan = millis() + SCAN_INTERVAL;

    Serial.println("\n────── New measurement cycle ──────");

    /* ── 1. Wake the bus ──────────────────────────────────────────── */
    sdi12_master_send_break(&master);

    /* ── 2. Check if sensor is present ────────────────────────────── */
    bool present = false;
    sdi12_master_acknowledge(&master, SENSOR_ADDR, &present);
    if (!present) {
        Serial.println("[Bare] No sensor responded");
        return;
    }
    Serial.println("[Bare] Sensor acknowledged");

    /* ── 3. Identify the sensor ───────────────────────────────────── */
    sdi12_ident_t ident;
    sdi12_err_t err = sdi12_master_identify(&master, SENSOR_ADDR, &ident);
    if (err == SDI12_OK) {
        Serial.print("  Vendor:   "); Serial.write(ident.vendor, 8); Serial.println();
        Serial.print("  Model:    "); Serial.write(ident.model, 6);  Serial.println();
        Serial.print("  Firmware: "); Serial.write(ident.firmware_version, 3); Serial.println();
        Serial.print("  Serial:   "); Serial.println(ident.serial);
    }

    /* ── 4. Start measurement (aM!) ───────────────────────────────── */
    sdi12_meas_response_t mresp;
    err = sdi12_master_start_measurement(
        &master, SENSOR_ADDR,
        SDI12_MEAS_STANDARD,            /* type = standard M  */
        0,                               /* group = 0          */
        false,                           /* crc = false        */
        &mresp);

    if (err != SDI12_OK) {
        Serial.print("[Bare] Measurement failed, error ");
        Serial.println(err);
        return;
    }

    Serial.print("[Bare] Wait ");
    Serial.print(mresp.wait_seconds);
    Serial.print("s for ");
    Serial.print(mresp.value_count);
    Serial.println(" values");

    /* ── 5. Wait for service request if needed ────────────────────── */
    if (mresp.wait_seconds > 0) {
        uint32_t timeout = (uint32_t)mresp.wait_seconds * 1000 + 1000;
        err = sdi12_master_wait_service_request(&master, SENSOR_ADDR, timeout);
        if (err != SDI12_OK) {
            Serial.println("[Bare] Service request timeout");
            return;
        }
    }

    /* ── 6. Read data pages (aD0!, aD1!, …) ───────────────────────── */
    uint8_t total = 0;
    for (uint8_t page = 0; total < mresp.value_count && page < 10; page++) {
        sdi12_data_response_t dresp;
        err = sdi12_master_get_data(&master, SENSOR_ADDR, page, false, &dresp);
        if (err != SDI12_OK) break;

        for (uint8_t i = 0; i < dresp.value_count; i++) {
            Serial.print("  Value[");
            Serial.print(total + i);
            Serial.print("] = ");
            Serial.println(dresp.values[i].value, dresp.values[i].decimals);
        }
        total += dresp.value_count;
    }

    /* ── 7. Continuous measurement demo (aR0!) ────────────────────── */
    Serial.println("\n  --- Continuous (aR0!) ---");
    sdi12_data_response_t cresp;
    err = sdi12_master_continuous(&master, SENSOR_ADDR, 0, false, &cresp);
    if (err == SDI12_OK) {
        for (uint8_t i = 0; i < cresp.value_count; i++) {
            Serial.print("  R0[");
            Serial.print(i);
            Serial.print("] = ");
            Serial.println(cresp.values[i].value, cresp.values[i].decimals);
        }
    }

    /* ── 8. CRC-verified measurement (aMC! → aD0! with CRC) ──────── */
    Serial.println("\n  --- CRC-verified (aMC!) ---");
    err = sdi12_master_start_measurement(
        &master, SENSOR_ADDR,
        SDI12_MEAS_STANDARD, 0,
        true,                            /* crc = true */
        &mresp);

    if (err == SDI12_OK) {
        if (mresp.wait_seconds > 0) {
            sdi12_master_wait_service_request(
                &master, SENSOR_ADDR,
                (uint32_t)mresp.wait_seconds * 1000 + 1000);
        }

        sdi12_data_response_t dresp;
        err = sdi12_master_get_data(&master, SENSOR_ADDR, 0, true, &dresp);

        if (err == SDI12_ERR_CRC_MISMATCH) {
            Serial.println("  CRC FAILED — data may be corrupt!");
        } else if (err == SDI12_OK) {
            Serial.print("  CRC OK, ");
            Serial.print(dresp.value_count);
            Serial.println(" values verified");
        }
    }
}

/*
 * API functions demonstrated:
 *
 *   sdi12_master_init()                 — explicit callback wiring
 *   sdi12_master_send_break()           — wake bus
 *   sdi12_master_acknowledge()          — ping sensor
 *   sdi12_master_identify()             — read aI! info
 *   sdi12_master_start_measurement()    — aM! / aMC! with group & CRC
 *   sdi12_master_wait_service_request() — listen for a\r\n
 *   sdi12_master_get_data()             — aD0!-aD9! with optional CRC
 *   sdi12_master_continuous()           — aR0! immediate data
 */
