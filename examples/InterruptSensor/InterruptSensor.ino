/**
 * InterruptSensor.ino — Interrupt-driven SDI-12 sensor using libsdi12.
 *
 * Unlike the polling examples, this sketch uses:
 *   • UART RX interrupt  → accumulate command bytes into a buffer
 *   • Pin-change interrupt → detect SDI-12 break signal (≥12 ms spacing)
 *   • Timer comparison    → flag when measurement hardware is "done"
 *
 * Wiring (Arduino Due / Mega / ESP32):
 *   - SDI-12 data line → Serial1 TX/RX (via RS-232/SDI-12 level shifter)
 *   - Direction pin     → digital pin 2
 *   - Break detect pin  → digital pin 3 (wire to SDI-12 data line, or
 *                          use the same pin with a pin-change interrupt)
 *
 * How SDI-12 framing works:
 *   1. Master sends a BREAK (line held HIGH ≥12 ms, inverted logic)
 *   2. Master sends a MARKING (line LOW ≥8.33 ms)
 *   3. Master sends the command at 1200 baud 7E1
 *   4. Sensor must respond within 15 ms of the command's stop bit
 */
#include <libsdi12.h>

/* ── Configuration ─────────────────────────────────────────────────── */

#define SDI12_UART      Serial1
#define DIR_PIN         2
#define BREAK_PIN       3       /* SDI-12 data line for break detect  */
#define CMD_BUF_SIZE    80

/* ── Shared state (ISR ↔ main loop) ────────────────────────────────── */

static volatile char     cmd_buf[CMD_BUF_SIZE];
static volatile uint8_t  cmd_len       = 0;
static volatile bool     cmd_ready     = false;   /* complete command received */
static volatile bool     break_flag    = false;    /* break signal detected     */
static volatile uint32_t break_start   = 0;        /* millis() when line went HIGH */
static volatile bool     meas_pending  = false;     /* measurement in progress   */
static volatile uint32_t meas_done_at  = 0;         /* millis() when meas finishes */

/* ── Hardware callbacks for libsdi12 ───────────────────────────────── */

void sdi_send(const char *data, size_t len, void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, HIGH);        /* TX mode */
    delay(SDI12_MARKING_MS);            /* §7.0 marking before reply */
    SDI12_UART.write(data, len);
    SDI12_UART.flush();                 /* wait for last byte out */
    digitalWrite(DIR_PIN, LOW);         /* back to RX */
}

sdi12_value_t sdi_read(uint8_t idx, void *ud) {
    (void)ud;
    sdi12_value_t v = {0.0f, 0};
    switch (idx) {
        case 0: v.value = analogRead(A0) * 0.1f; v.decimals = 2; break;
        case 1: v.value = analogRead(A1) * 0.05f; v.decimals = 1; break;
    }
    return v;
}

uint16_t sdi_start(uint8_t group, sdi12_meas_type_t type, void *ud) {
    (void)group; (void)type; (void)ud;
    meas_pending = true;
    meas_done_at = millis() + 500;      /* simulated 500 ms acquisition */
    return 1;                           /* ttt = 1 s */
}

/* ── Sensor definition ─────────────────────────────────────────────── */

SDI12_SENSOR_DEFINE(sensor, '0',
    "MYCO    ", "IRQ-01", "100", "SN-12345     ",
    sdi_send, sdi_read);

/* ── UART RX interrupt handler ─────────────────────────────────────── */
/*
 * On platforms without a direct UART ISR hook (Arduino), we use
 * serialEvent1() which is called between loop() iterations when
 * data is available.  For true ISR-level handling on bare metal,
 * see examples/interrupt_sensor.c.
 */
void serialEvent1() {
    while (SDI12_UART.available()) {
        char c = SDI12_UART.read();

        if (cmd_ready) continue;        /* previous command not yet consumed */

        if (c == '!') {
            /* End of SDI-12 command */
            cmd_buf[cmd_len++] = c;
            cmd_buf[cmd_len]   = '\0';
            cmd_ready = true;
        } else if (cmd_len < CMD_BUF_SIZE - 2) {
            cmd_buf[cmd_len++] = c;
        }
    }
}

/* ── Break detection via pin-change interrupt ──────────────────────── */
/*
 * SDI-12 uses inverted logic:
 *   spacing (idle / break) = HIGH
 *   marking                = LOW
 *
 * A break is spacing held for ≥12 ms.  We timestamp the rising edge
 * and check duration on the falling edge.
 */
void breakISR() {
    if (digitalRead(BREAK_PIN) == HIGH) {
        /* Rising edge → line went to spacing (potential break start) */
        break_start = millis();
    } else {
        /* Falling edge → spacing ended, check duration */
        if (break_start > 0 && (millis() - break_start) >= 12) {
            break_flag = true;
        }
        break_start = 0;
    }
}

/* ── Setup ─────────────────────────────────────────────────────────── */

void setup() {
    Serial.begin(115200);
    SDI12_UART.begin(1200, SERIAL_7E1);

    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(DIR_PIN, LOW);         /* start in RX mode */

    pinMode(BREAK_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(BREAK_PIN), breakISR, CHANGE);

    /* Initialise the sensor */
    SDI12_SENSOR_SETUP_ASYNC(sensor, sdi_start);
    SDI12_SENSOR_ADD_PARAM(sensor, 0, "TA", "degC", 2);  /* Analog A0 */
    SDI12_SENSOR_ADD_PARAM(sensor, 0, "RH", "%RH",  1);  /* Analog A1 */

    Serial.println("[IRQ] SDI-12 interrupt sensor ready at '0'");
}

/* ── Main loop — lightweight, interrupt-driven ─────────────────────── */

void loop() {
    /*
     * 1. Handle break signal (highest priority)
     *    Resets any in-progress measurement, clears command buffer.
     */
    if (break_flag) {
        break_flag = false;
        cmd_len    = 0;
        cmd_ready  = false;
        SDI12_SENSOR_BREAK(sensor);
        Serial.println("[IRQ] Break detected");
    }

    /*
     * 2. Handle complete command
     *    The ISR set cmd_ready=true when '!' or '?' arrived.
     */
    if (cmd_ready) {
        /* Copy volatile buffer to local (safe outside ISR) */
        char local[CMD_BUF_SIZE];
        uint8_t len;

        noInterrupts();
        len = cmd_len;
        memcpy(local, (const char *)cmd_buf, len);
        local[len] = '\0';
        cmd_len   = 0;
        cmd_ready = false;
        interrupts();

        Serial.print("[IRQ] Command: ");
        Serial.println(local);

        SDI12_SENSOR_PROCESS(sensor, local, len);  /* M/C/V/H → sdi_start */
    }

    /*
     * 3. Check if async measurement is complete
     *    Service request follows for M/V only; ignored if a break aborted it.
     */
    if (meas_pending && millis() >= meas_done_at) {
        meas_pending = false;

        /* Read final values from hardware */
        sdi12_value_t vals[2];
        vals[0].value = analogRead(A0) * 0.1f;  vals[0].decimals = 2;
        vals[1].value = analogRead(A1) * 0.05f; vals[1].decimals = 1;
        SDI12_SENSOR_MEAS_DONE(sensor, vals, 2);
        Serial.println("[IRQ] Measurement complete");
    }
}
