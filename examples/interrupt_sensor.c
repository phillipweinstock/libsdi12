/**
 * @file interrupt_sensor.c
 * @brief Bare-metal interrupt-driven SDI-12 sensor (Cortex-M / generic).
 *
 * Demonstrates the recommended integration pattern for real products:
 *
 *   • UART RX IRQ → accumulates bytes, sets flag on '!'
 *   • Timer / pin-change IRQ → detects break signal (≥12 ms spacing)
 *   • Main loop → dispatches to sdi12_sensor_process() when ready
 *   • Async measurement → hardware finishes in background, calls
 *     sdi12_sensor_measurement_done() when complete
 *
 * This file is platform-agnostic pseudo-code. Replace the HW: stubs
 * stubs with your MCU's HAL (STM32 HAL, nRF SDK, ESP-IDF, etc.).
 *
 * Compile check:
 *   gcc -std=c11 -fsyntax-only -I.. interrupt_sensor.c
 */
#include "sdi12_easy.h"
#include <string.h>

/* ── Platform stubs (replace with your MCU HAL) ───────────────────── */

static inline void     hw_uart_write(const char *d, size_t n) { (void)d; (void)n; }
static inline void     hw_uart_flush(void)                    { }
static inline void     hw_gpio_set(int pin, int val)          { (void)pin; (void)val; }
static inline uint32_t hw_millis(void)                        { return 0; }
static inline void     hw_disable_irq(void)                   { }
static inline void     hw_enable_irq(void)                    { }
static inline float    hw_adc_read(int ch)                    { (void)ch; return 0.0f; }

#define DIR_PIN  2

/* ── Shared state (ISR ↔ main) ─────────────────────────────────────── */

#define CMD_BUF_SIZE 80

static volatile char     g_cmd_buf[CMD_BUF_SIZE];
static volatile uint8_t  g_cmd_len       = 0;
static volatile int      g_cmd_ready     = 0;   /* 1 = complete command */
static volatile int      g_break_flag    = 0;   /* 1 = break detected  */
static volatile uint32_t g_break_start   = 0;
static volatile int      g_meas_pending  = 0;
static volatile uint32_t g_meas_done_at  = 0;

/* ── Hardware callbacks for libsdi12 ───────────────────────────────── */

static void sdi_send(const char *data, size_t len, void *ud) {
    (void)ud;
    hw_gpio_set(DIR_PIN, 1);
    hw_uart_write(data, len);
    hw_uart_flush();
    hw_gpio_set(DIR_PIN, 0);
}

static void sdi_dir(sdi12_dir_t dir, void *ud) {
    (void)ud;
    hw_gpio_set(DIR_PIN, dir == SDI12_DIR_TX ? 1 : 0);
}

static sdi12_value_t sdi_read(uint8_t idx, void *ud) {
    (void)ud;
    sdi12_value_t v = {0.0f, 0};
    switch (idx) {
        case 0: v.value = hw_adc_read(0); v.decimals = 2; break;
        case 1: v.value = hw_adc_read(1); v.decimals = 1; break;
    }
    return v;
}

/* ── Sensor definition ─────────────────────────────────────────────── */

SDI12_SENSOR_DEFINE(sensor, '0',
    "MYCO    ", "IRQ-01", "100", "SN-12345     ",
    sdi_send, sdi_dir, sdi_read);

/* ══════════════════════════════════════════════════════════════════════
 *  INTERRUPT SERVICE ROUTINES
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * UART RX interrupt — called once per received byte.
 *
 * Accumulates characters into g_cmd_buf.  When '!' arrives the
 * command is complete and g_cmd_ready is set so the main loop can
 * process it outside ISR context.
 *
 * Hook this to your MCU's UART RX IRQ:
 *   STM32:  void USARTx_IRQHandler(void)   { ... }
 *   nRF52:  void UARTEx_IRQHandler(void)   { ... }
 *   ESP32:  uart_event_task or ISR hook
 */
void UART_RX_IRQHandler(char byte_received) {
    if (g_cmd_ready) return;            /* previous not yet consumed  */

    if (byte_received == '!' || byte_received == '?') {
        g_cmd_buf[g_cmd_len++] = byte_received;
        g_cmd_buf[g_cmd_len]   = '\0';
        g_cmd_ready = 1;
    } else if (g_cmd_len < CMD_BUF_SIZE - 2) {
        g_cmd_buf[g_cmd_len++] = byte_received;
    }
}

/**
 * Break detection — via pin-change or UART framing-error interrupt.
 *
 * Strategy A (pin-change):
 *   Monitor the SDI-12 data line.  On rising edge, record timestamp.
 *   On falling edge, if duration ≥ 12 ms → break detected.
 *
 * Strategy B (UART framing error):
 *   Many UARTs flag a framing error when the line is held at spacing
 *   for longer than a character frame.  Accumulate framing errors
 *   for ≥ 12 ms worth of bit times → break detected.
 *
 * This example uses Strategy A.
 */
void SDI12_LINE_CHANGE_IRQHandler(int line_high) {
    if (line_high) {
        /* Rising edge → spacing started (potential break) */
        g_break_start = hw_millis();
    } else {
        /* Falling edge → spacing ended, check duration */
        if (g_break_start > 0 && (hw_millis() - g_break_start) >= 12) {
            g_break_flag = 1;
        }
        g_break_start = 0;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  MAIN APPLICATION
 * ══════════════════════════════════════════════════════════════════════ */

void app_init(void) {
    /* HW: configure UART at 1200 baud, 7E1, enable RX interrupt       */
    /* HW: configure DIR_PIN as output, default LOW (RX)               */
    /* HW: configure break-detect pin as input with pin-change IRQ     */

    SDI12_SENSOR_SETUP(sensor);
    SDI12_SENSOR_ADD_PARAM(sensor, 0, "TA", "degC", 2);
    SDI12_SENSOR_ADD_PARAM(sensor, 0, "RH", "%RH",  1);
}

void app_main_loop(void) {
    /*
     * 1. Break signal — highest priority
     */
    if (g_break_flag) {
        hw_disable_irq();
        g_break_flag  = 0;
        g_cmd_len     = 0;
        g_cmd_ready   = 0;
        g_meas_pending = 0;
        hw_enable_irq();

        SDI12_SENSOR_BREAK(sensor);
    }

    /*
     * 2. Complete command — copy out of volatile buffer safely
     */
    if (g_cmd_ready) {
        char    local[CMD_BUF_SIZE];
        uint8_t len;

        hw_disable_irq();
        len = g_cmd_len;
        memcpy(local, (const char *)g_cmd_buf, len);
        local[len] = '\0';
        g_cmd_len   = 0;
        g_cmd_ready = 0;
        hw_enable_irq();

        SDI12_SENSOR_PROCESS(sensor, local, len);

        /* If M or C command, start async measurement hardware */
        if (len >= 2 && (local[1] == 'M' || local[1] == 'C')) {
            g_meas_pending = 1;
            g_meas_done_at = hw_millis() + 500;  /* e.g. 500 ms ADC */
            /* HW: start your ADC / I2C / SPI acquisition here */
        }
    }

    /*
     * 3. Async measurement complete → notify the library
     *    This triggers the service request (a\r\n) to the master.
     */
    if (g_meas_pending && hw_millis() >= g_meas_done_at) {
        g_meas_pending = 0;

        /* Read final values from hardware */
        sdi12_value_t vals[2];
        vals[0].value = hw_adc_read(0); vals[0].decimals = 2;
        vals[1].value = hw_adc_read(1); vals[1].decimals = 1;
        SDI12_SENSOR_MEAS_DONE(sensor, vals, 2);
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
