/**
 * InterruptMaster.ino — Interrupt-driven SDI-12 data recorder.
 *
 * Demonstrates a master that:
 *   • Uses UART RX interrupt to accumulate sensor responses
 *   • Detects service requests (a\r\n) asynchronously
 *   • Runs a non-blocking measurement state machine in loop()
 *
 * Wiring (Arduino Due / Mega / ESP32):
 *   - SDI-12 data line → Serial1 TX/RX (via level shifter)
 *   - Direction pin     → digital pin 2
 *
 * Open Serial Monitor at 115200 to see results.
 * Scans address '0' every 10 seconds.
 */
#include <libsdi12.h>

/* ── Configuration ─────────────────────────────────────────────────── */

#define SDI12_UART      Serial1
#define DIR_PIN         2
#define RX_BUF_SIZE     128
#define SCAN_INTERVAL   10000   /* ms between measurements */
#define SENSOR_ADDR     '0'

/* ── RX ring buffer (filled by ISR, drained by recv callback) ──────── */

static volatile char     rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static uint16_t rx_available(void) {
    uint16_t h = rx_head, t = rx_tail;
    return (h >= t) ? (h - t) : (RX_BUF_SIZE - t + h);
}

static char rx_read_byte(void) {
    char c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

/* Called between loop() iterations when Serial1 data arrives */
void serialEvent1() {
    while (SDI12_UART.available()) {
        char c = SDI12_UART.read();
        uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
        if (next != rx_tail) {          /* drop byte if full */
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

/* ── Hardware callbacks for libsdi12 ───────────────────────────────── */

void sdi_send(const char *data, size_t len, void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, HIGH);
    SDI12_UART.write(data, len);
    SDI12_UART.flush();
    digitalWrite(DIR_PIN, LOW);
}

/*
 * Non-blocking receive: drains bytes from the ISR ring buffer
 * until \n is seen or timeout expires.
 */
size_t sdi_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud) {
    (void)ud;
    unsigned long start = millis();
    size_t pos = 0;

    while ((millis() - start) < timeout_ms && pos < max) {
        if (rx_available() > 0) {
            char c = rx_read_byte();
            buf[pos++] = c;
            if (c == '\n') break;       /* complete response */
        }
    }
    return pos;
}

void sdi_dir(sdi12_dir_t dir, void *ud) {
    (void)ud;
    digitalWrite(DIR_PIN, dir == SDI12_DIR_TX ? HIGH : LOW);
}

void sdi_break(void *ud) {
    (void)ud;
    /* Hold line at spacing (HIGH) for ≥12 ms, then marking for ≥8.33 ms */
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

void sdi_delay(uint32_t ms, void *ud) {
    (void)ud;
    delay(ms);
}

/* ── Master definition ─────────────────────────────────────────────── */

SDI12_MASTER_DEFINE(rec, sdi_send, sdi_recv, sdi_dir, sdi_break, sdi_delay);

/* ── Measurement state machine ─────────────────────────────────────── */

enum MasterState {
    STATE_IDLE,
    STATE_BREAK_SENT,
    STATE_MEASURING,
    STATE_WAITING,
    STATE_READING,
};

static enum MasterState  state        = STATE_IDLE;
static unsigned long     next_scan    = 0;
static unsigned long     wait_until   = 0;
static sdi12_meas_response_t mresp;

/* ── Setup ─────────────────────────────────────────────────────────── */

void setup() {
    Serial.begin(115200);
    SDI12_UART.begin(1200, SERIAL_7E1);

    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(DIR_PIN, LOW);

    SDI12_MASTER_SETUP(rec);
    Serial.println("[IRQ Master] Ready — scanning '0' every 10 s");
    next_scan = millis();
}

/* ── Main loop — non-blocking state machine ────────────────────────── */

void loop() {
    switch (state) {

    case STATE_IDLE:
        if (millis() >= next_scan) {
            Serial.println("\n--- New measurement cycle ---");
            SDI12_MASTER_BREAK(rec);
            state = STATE_BREAK_SENT;
        }
        break;

    case STATE_BREAK_SENT: {
        bool present = false;
        SDI12_MASTER_PING(rec, SENSOR_ADDR, &present);
        if (!present) {
            Serial.println("[IRQ Master] No sensor at '0'");
            next_scan = millis() + SCAN_INTERVAL;
            state = STATE_IDLE;
            break;
        }

        /* Identify */
        sdi12_ident_t id;
        SDI12_MASTER_IDENTIFY(rec, SENSOR_ADDR, &id);
        Serial.print("Sensor: ");
        Serial.write(id.vendor, 8);
        Serial.print(" ");
        Serial.write(id.model, 6);
        Serial.println();

        /* Start measurement */
        SDI12_MASTER_MEASURE(rec, SENSOR_ADDR, &mresp);
        Serial.print("Wait ");
        Serial.print(mresp.wait_seconds);
        Serial.print("s for ");
        Serial.print(mresp.value_count);
        Serial.println(" values");

        if (mresp.wait_seconds > 0) {
            wait_until = millis() + (uint32_t)mresp.wait_seconds * 1000 + 500;
            state = STATE_WAITING;
        } else {
            state = STATE_READING;
        }
        break;
    }

    case STATE_WAITING:
        /*
         * Wait for service request (sensor sends "a\r\n" when done).
         * The RX ISR buffers incoming bytes; sdi_recv will pick them up
         * when the library calls wait_service_request internally.
         *
         * We also have a hard timeout as a fallback.
         */
        if (rx_available() >= 3 || millis() >= wait_until) {
            /* Let the library consume the service request if present */
            SDI12_MASTER_WAIT(rec, SENSOR_ADDR, 1000);
            state = STATE_READING;
        }
        break;

    case STATE_READING: {
        sdi12_data_response_t dresp;
        SDI12_MASTER_GET_DATA(rec, SENSOR_ADDR, 0, false, &dresp);

        Serial.println("Results:");
        for (uint8_t i = 0; i < dresp.value_count; i++) {
            Serial.print("  [");
            Serial.print(i);
            Serial.print("] = ");
            Serial.println(dresp.values[i].value, dresp.values[i].decimals);
        }

        next_scan = millis() + SCAN_INTERVAL;
        state = STATE_IDLE;
        break;
    }

    default:
        state = STATE_IDLE;
        break;
    }
}
