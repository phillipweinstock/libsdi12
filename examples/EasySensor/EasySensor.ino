/**
 * EasySensor.ino — Minimal SDI-12 sensor using libsdi12 easy macros.
 *
 * Wiring (example for Arduino Due / Mega / ESP32):
 *   - SDI-12 data line → Serial1 TX/RX (via level shifter)
 *   - Direction pin     → digital pin 2
 *
 * This sketch creates a weather sensor at address '0' that reports
 * three parameters: temperature, humidity, and pressure.
 */
#include <libsdi12.h>

/* ── Hardware functions ────────────────────────────────────────────── */

void my_send(const char *data, size_t len, void *ud) {
    (void)ud;
    digitalWrite(2, HIGH);          /* TX mode */
    Serial1.write(data, len);
    Serial1.flush();
    digitalWrite(2, LOW);           /* back to RX */
}

void my_dir(sdi12_dir_t dir, void *ud) {
    (void)ud;
    digitalWrite(2, dir == SDI12_DIR_TX ? HIGH : LOW);
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

/* ── Define & set up ───────────────────────────────────────────────── */

SDI12_SENSOR_DEFINE(weather, '0',
    "WEATHER ", "WX3000", "110", "SN-00042     ",
    my_send, my_dir, my_read);

void setup() {
    Serial.begin(115200);
    Serial1.begin(1200, SERIAL_7E1);  /* SDI-12: 1200 baud, 7E1 */
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);

    SDI12_SENSOR_SETUP(weather);
    SDI12_SENSOR_ADD_PARAM(weather, 0, "TA", "degC", 2);
    SDI12_SENSOR_ADD_PARAM(weather, 0, "RH", "%RH",  1);
    SDI12_SENSOR_ADD_PARAM(weather, 0, "PA", "kPa",  1);

    Serial.println("SDI-12 sensor ready at address '0'");
}

void loop() {
    if (Serial1.available()) {
        char buf[80];
        size_t len = Serial1.readBytesUntil('!', buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '!';
            buf[len + 1] = '\0';
            len++;
            SDI12_SENSOR_PROCESS(weather, buf, len);
        }
    }
}
