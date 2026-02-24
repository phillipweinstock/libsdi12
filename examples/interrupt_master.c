/**
 * @file interrupt_master.c
 * @brief Bare-metal interrupt-driven SDI-12 master (Cortex-M / generic).
 *
 * Demonstrates a non-blocking data recorder pattern:
 *
 *   • UART RX IRQ  → fills a ring buffer with incoming bytes
 *   • recv callback → drains the ring buffer (timeout-aware)
 *   • State machine → cycles through break → measure → wait → read
 *
 * Replace the hw_* stubs with your MCU's HAL.
 *
 * Compile check:
 *   gcc -std=c11 -fsyntax-only -I.. interrupt_master.c
 */
#include "sdi12_easy.h"
#include <string.h>

/* ── Platform stubs (replace with your MCU HAL) ───────────────────── */

static inline void     hw_uart_write(const char *d, size_t n) { (void)d; (void)n; }
static inline void     hw_uart_flush(void)                    { }
static inline void     hw_gpio_set(int pin, int val)          { (void)pin; (void)val; }
static inline uint32_t hw_millis(void)                        { return 0; }
static inline void     hw_delay_ms(uint32_t ms)               { (void)ms; }
static inline void     hw_disable_irq(void)                   { }
static inline void     hw_enable_irq(void)                    { }
static inline void     hw_send_break(void)                    { }

#define DIR_PIN  2

/* ── Ring buffer (ISR → recv callback) ─────────────────────────────── */

#define RX_BUF_SIZE 128

static volatile char     rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static uint16_t rx_available(void) {
    uint16_t h = rx_head, t = rx_tail;
    return (h >= t) ? (h - t) : (RX_BUF_SIZE - t + h);
}

static char rx_pop(void) {
    char c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

/* ══════════════════════════════════════════════════════════════════════
 *  UART RX INTERRUPT
 *
 *  Hook this to your MCU's UART RX IRQ:
 *    STM32:  void USARTx_IRQHandler(void)
 *    nRF52:  void UARTEx_IRQHandler(void)
 *    ESP32:  uart_event_task
 * ══════════════════════════════════════════════════════════════════════ */

void UART_RX_IRQHandler(char byte_received) {
    uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
    if (next != rx_tail) {
        rx_buf[rx_head] = byte_received;
        rx_head = next;
    }
}

/* ── Hardware callbacks for libsdi12 ───────────────────────────────── */

static void sdi_send(const char *data, size_t len, void *ud) {
    (void)ud;
    hw_gpio_set(DIR_PIN, 1);
    hw_uart_write(data, len);
    hw_uart_flush();
    hw_gpio_set(DIR_PIN, 0);
}

/*
 * Receive callback: drain bytes from the ISR ring buffer until
 * a newline arrives or the timeout expires.  This is called by the
 * library from main-loop context (never from an ISR).
 */
static size_t sdi_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud) {
    (void)ud;
    uint32_t start = hw_millis();
    size_t pos = 0;

    while ((hw_millis() - start) < timeout_ms && pos < max) {
        if (rx_available() > 0) {
            char c = rx_pop();
            buf[pos++] = c;
            if (c == '\n') break;
        }
    }
    return pos;
}

static void sdi_dir(sdi12_dir_t dir, void *ud) {
    (void)ud;
    hw_gpio_set(DIR_PIN, dir == SDI12_DIR_TX ? 1 : 0);
}

static void sdi_break(void *ud) {
    (void)ud;
    hw_send_break();
}

static void sdi_delay(uint32_t ms, void *ud) {
    (void)ud;
    hw_delay_ms(ms);
}

/* ── Master definition ─────────────────────────────────────────────── */

SDI12_MASTER_DEFINE(rec, sdi_send, sdi_recv, sdi_dir, sdi_break, sdi_delay);

/* ── State machine ─────────────────────────────────────────────────── */

typedef enum {
    ST_IDLE,
    ST_BREAK_SENT,
    ST_MEASURING,
    ST_WAITING,
    ST_READING,
} master_state_t;

static master_state_t        state      = ST_IDLE;
static uint32_t              next_scan  = 0;
static uint32_t              wait_until = 0;
static sdi12_meas_response_t mresp;

#define SCAN_INTERVAL_MS  10000
#define SENSOR_ADDR       '0'

/* ── Application ───────────────────────────────────────────────────── */

void app_init(void) {
    /* HW: configure UART at 1200 baud, 7E1, enable RX interrupt */
    /* HW: configure DIR_PIN as output, default LOW (RX)         */

    SDI12_MASTER_SETUP(rec);
    next_scan = hw_millis();
}

void app_main_loop(void) {
    switch (state) {

    case ST_IDLE:
        if (hw_millis() >= next_scan) {
            SDI12_MASTER_BREAK(rec);
            state = ST_BREAK_SENT;
        }
        break;

    case ST_BREAK_SENT: {
        /* Ping the sensor */
        bool present = false;
        SDI12_MASTER_PING(rec, SENSOR_ADDR, &present);
        if (!present) {
            next_scan = hw_millis() + SCAN_INTERVAL_MS;
            state = ST_IDLE;
            break;
        }

        /* Start measurement */
        SDI12_MASTER_MEASURE(rec, SENSOR_ADDR, &mresp);

        if (mresp.wait_seconds > 0) {
            wait_until = hw_millis()
                       + (uint32_t)mresp.wait_seconds * 1000
                       + 500;
            state = ST_WAITING;
        } else {
            state = ST_READING;
        }
        break;
    }

    case ST_WAITING:
        /* Poll for service request or timeout */
        if (rx_available() >= 3 || hw_millis() >= wait_until) {
            SDI12_MASTER_WAIT(rec, SENSOR_ADDR, 1000);
            state = ST_READING;
        }
        break;

    case ST_READING: {
        sdi12_data_response_t dresp;
        SDI12_MASTER_GET_DATA(rec, SENSOR_ADDR, 0, false, &dresp);

        /* Process dresp.values[0..dresp.value_count-1] here */
        (void)dresp;

        next_scan = hw_millis() + SCAN_INTERVAL_MS;
        state = ST_IDLE;
        break;
    }

    default:
        state = ST_IDLE;
        break;
    }
}

/*
 * Typical main() for a bare-metal firmware:
 *
 *   int main(void) {
 *       hw_system_init();
 *       app_init();
 *       for (;;) app_main_loop();
 *   }
 */
