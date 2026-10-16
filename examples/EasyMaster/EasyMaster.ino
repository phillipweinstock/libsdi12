/**
 * EasyMaster.ino — Minimal SDI-12 data recorder using libsdi12 easy macros.
 *
 * Wiring (example for Arduino Due / Mega / ESP32):
 *   - SDI-12 data line → Serial1 TX/RX (via level shifter)
 *   - Direction pin     → digital pin 2
 *
 * This sketch scans address '0', identifies the sensor,
 * takes a measurement, and prints the values to the Serial Monitor.
 */
#include <libsdi12.h>

/* ── Hardware functions ────────────────────────────────────────────── */

void my_send(const char *data, size_t len, void *ud) {
    (void)ud;
    digitalWrite(2, HIGH);
    Serial1.write(data, len);
    Serial1.flush();
    digitalWrite(2, LOW);
}

#define RX_IDLE_MS 20   /* §7.0: ≤1.66 ms gap + 8.33 ms char, with slack */

size_t my_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud) {
    (void)ud;
    size_t pos = 0;
    unsigned long t = millis();
    uint32_t wait = timeout_ms;               /* first byte */
    while (pos < max && millis() - t < wait) {
        if (Serial1.available()) {
            buf[pos++] = Serial1.read();
            if (buf[pos - 1] == '\n') break;
            t = millis();
            wait = RX_IDLE_MS;
        }
    }
    return pos;
}

void my_dir(sdi12_dir_t dir, void *ud) {
    (void)ud;
    digitalWrite(2, dir == SDI12_DIR_TX ? HIGH : LOW);
}

void my_break(void *ud) {
    (void)ud;
    /* §7.0: spacing ≥ 12 ms; the library adds post-break marking */
    digitalWrite(2, HIGH);
    Serial1.end();
    pinMode(1, OUTPUT);       /* TX pin → manual control */
    digitalWrite(1, HIGH);    /* spacing = high for inverted logic */
    delay(15);
    digitalWrite(1, LOW);
    Serial1.begin(1200, SERIAL_7E1);
    digitalWrite(2, LOW);
}

void my_delay(uint32_t ms, void *ud) {
    (void)ud;
    delay(ms);
}

/* ── Define & set up ───────────────────────────────────────────────── */

SDI12_MASTER_DEFINE(recorder, my_send, my_recv, my_dir, my_break, my_delay);

void setup() {
    Serial.begin(115200);
    Serial1.begin(1200, SERIAL_7E1);
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);

    SDI12_MASTER_SETUP(recorder);
    Serial.println("SDI-12 master ready — reading sensor at '0'...");

    /* Wake the bus */
    SDI12_MASTER_BREAK(recorder);

    /* Check if sensor is present */
    bool present = false;
    SDI12_MASTER_PING(recorder, '0', &present);
    if (!present) {
        Serial.println("No sensor found at address '0'");
        return;
    }

    /* Identify */
    sdi12_ident_t id;
    SDI12_MASTER_IDENTIFY(recorder, '0', &id);
    Serial.print("Sensor: ");
    Serial.write(id.vendor, 8);
    Serial.print(" ");
    Serial.write(id.model, 6);
    Serial.println();

    /* Start measurement */
    sdi12_meas_response_t mresp;
    SDI12_MASTER_MEASURE(recorder, '0', &mresp);
    Serial.print("Waiting ");
    Serial.print(mresp.wait_seconds);
    Serial.print("s for ");
    Serial.print(mresp.value_count);
    Serial.println(" values...");

    if (mresp.wait_seconds > 0) {
        SDI12_MASTER_WAIT(recorder, '0',
                          (uint32_t)mresp.wait_seconds * 1000 + 1000);
    }

    /* Read data */
    sdi12_data_response_t dresp;
    SDI12_MASTER_GET_DATA(recorder, '0', 0, false, &dresp);

    for (uint8_t i = 0; i < dresp.value_count; i++) {
        Serial.print("  Value[");
        Serial.print(i);
        Serial.print("] = ");
        Serial.println(dresp.values[i].value, dresp.values[i].decimals);
    }

    Serial.println("Done!");
}

void loop() {
    /* Single-shot demo — nothing to do in loop */
}
