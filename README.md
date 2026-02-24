# libsdi12

**The most complete, portable SDI-12 library available.**

A pure C implementation of the **SDI-12 v1.4** protocol covering **every
command in the specification** — both **sensor (slave)** and **master (data
recorder)** roles — with zero external dependencies.

No other open-source SDI-12 library offers this combination:

- ✅ **Full v1.4 spec coverage** — every command type, including high-volume,
  concurrent, continuous, verification, metadata, extended, and CRC variants
- ✅ **Dual-role** — sensor *and* master in one library
- ✅ **Beginner-friendly** — `sdi12_easy.h` convenience macros: sensor in 4
  lines, master in 3 — great for hobbyists and Arduino users
- ✅ **Pure C11** — no Arduino, no HAL, no OS, no `malloc`
- ✅ **98 tests** — unit + metamorphic/property-based, all platform-agnostic
- ✅ **Registry-ready** — works out of the box with PlatformIO Library Manager
  and Arduino Library Manager
- ✅ **Zero dependencies** — compiles anywhere: `gcc`, `clang`, `armcc`,
  `arm-none-eabi-gcc`, MSVC, PlatformIO, CMake, or bare Makefile

> Most SDI-12 libraries only implement the master side, cover a handful of
> commands, and are tightly coupled to Arduino or a specific HAL. **libsdi12**
> is designed from the ground up as a portable, spec-complete protocol engine
> with hardware abstracted behind callbacks.

---

## Command Coverage

| Command | Description | Sensor | Master |
|---|---|:---:|:---:|
| `a!` / `?!` | Acknowledge / query address | ✅ | ✅ |
| `aI!` | Identification | ✅ | ✅ |
| `aM!` `aM1!`–`aM9!` | Standard measurement | ✅ | ✅ |
| `aMC!` `aMC1!`–`aMC9!` | Standard measurement + CRC | ✅ | ✅ |
| `aC!` `aC1!`–`aC9!` | Concurrent measurement | ✅ | ✅ |
| `aCC!` `aCC1!`–`aCC9!` | Concurrent measurement + CRC | ✅ | ✅ |
| `aR0!`–`aR9!` | Continuous measurement | ✅ | ✅ |
| `aRC0!`–`aRC9!` | Continuous measurement + CRC | ✅ | ✅ |
| `aD0!`–`aD9!` | Send data | ✅ | ✅ |
| `aV!` | Verification | ✅ | ✅ |
| `aAb!` | Change address | ✅ | ✅ |
| `aX…!` | Extended commands | ✅ | ✅ |
| `aHA!` | High-volume ASCII | ✅ | ✅ |
| `aHB!` | High-volume binary | Callback | ✅ |
| `aIM!` `aIC!` `aIM_nnn!` | Metadata / param identification | ✅ | — |
| CRC-16-IBM | Compute, append, verify | ✅ | ✅ |
| Break signal | Detect / send | ✅ | ✅ |
| Service request (`a\r\n`) | Async measurement complete | ✅ | ✅ |

### Comparison With Other Libraries

| Feature | libsdi12 | Arduino-SDI-12 | Others |
|---|:---:|:---:|:---:|
| Full v1.4 command set | ✅ | Partial | Partial |
| Sensor (slave) role | ✅ | ❌ | Rare |
| Master (recorder) role | ✅ | ✅ | ✅ |
| CRC-16 (MC/CC/RC) | ✅ | ❌ | Rare |
| High-volume (HA/HB) | ✅ | ❌ | ❌ |
| Metadata (IM/IC) | ✅ | ❌ | ❌ |
| Platform independent | ✅ | Arduino | Varies |
| No `malloc` | ✅ | ❌ | Varies |
| Test suite | 98 tests | ❌ | Minimal |

---

## Zero Dependencies

- **No `malloc`** — all state lives in user-allocated context structs.
- **No hardware headers** — UART, GPIO, and timing abstracted via callbacks.
- **C11** — compiles with `gcc`, `clang`, `armcc`, `arm-none-eabi-gcc`, MSVC.
- **C++ compatible** — all headers wrapped in `extern "C"`.
- **Self-contained tests** — includes its own single-header test framework;
  no Unity, no Google Test, no framework install needed.

## File Structure

```
libsdi12/
├── libsdi12.h           # Convenience header — includes everything
├── sdi12.h              # Common types, constants, enums, CRC API
├── sdi12_easy.h         # ★ Beginner-friendly convenience macros
├── sdi12_crc.c          # CRC-16-IBM implementation
├── sdi12_sensor.h       # Sensor (slave) API declarations
├── sdi12_sensor.c       # Sensor command parser & state machine
├── sdi12_master.h       # Master (data recorder) API declarations
├── sdi12_master.c       # Master command builder & response parser
├── library.json         # PlatformIO library manifest
├── library.properties   # Arduino Library Manager manifest
├── LICENSE              # MIT license
├── CMakeLists.txt       # CMake build support
├── examples/
│   ├── EasySensor/EasySensor.ino  # ★ Arduino sensor sketch (easy macros)
│   ├── EasyMaster/EasyMaster.ino  # ★ Arduino master sketch (easy macros)
│   ├── BareSensor/BareSensor.ino  # ★ Arduino sensor sketch (raw API)
│   ├── BareMaster/BareMaster.ino  # ★ Arduino master sketch (raw API)
│   ├── InterruptSensor/InterruptSensor.ino  # ★ ISR-driven Arduino sensor
│   ├── InterruptMaster/InterruptMaster.ino  # ★ ISR-driven Arduino master
│   ├── interrupt_sensor.c   # ★ Bare-metal ISR sensor (Cortex-M)
│   ├── interrupt_master.c   # ★ Bare-metal ISR master (Cortex-M)
│   ├── easy_sensor.c    # ★ Minimal sensor (plain C, easy macros)
│   ├── easy_master.c    # ★ Minimal master (plain C, easy macros)
│   ├── example_sensor.c # Full-featured sensor walkthrough (raw API)
│   ├── example_master.c # Full-featured master walkthrough (raw API)
│   └── example_crc.c    # Standalone CRC demo (compiles & runs)
├── test/
│   ├── sdi12_test.h     # Standalone single-header test framework
│   ├── Makefile         # Build tests with any C compiler
│   ├── test_main.c      # Test runner (98 tests)
│   ├── test_crc.c       # CRC-16 tests (15)
│   ├── test_address.c   # Address validation tests (7)
│   ├── test_sensor.c    # Sensor state machine tests (36)
│   ├── test_master.c    # Master parser tests (21)
│   └── test_metamorphic.c  # Property-based tests (19)
├── TESTING.md           # Test documentation & architecture
└── README.md
```

## Quick Start

### PlatformIO

Drop the `libsdi12/` folder into your project's `lib/` directory. PlatformIO
will auto-discover it via `library.json`. Then include:

```c
#include <sdi12.h>
#include <sdi12_sensor.h>  /* or sdi12_master.h */
```

### CMake

```cmake
add_subdirectory(libsdi12)
target_link_libraries(your_target PRIVATE sdi12)
```

### Manual

Add all `.c` and `.h` files to your build system. Requires C11 (`-std=c11`).

---

## ★ Easy API — For Beginners & Hobbyists

Don't want to deal with structs, callbacks tables, and init boilerplate?
Include `sdi12_easy.h` and get going in **4 lines**:

### Easy Sensor (complete example)

```c
#include "sdi12_easy.h"

/* Write your 3 hardware functions once */
void my_send(const char *d, size_t n, void *u) { uart_write(d, n); }
void my_dir(sdi12_dir_t dir, void *u)          { gpio_set(DIR, dir); }
sdi12_value_t my_read(uint8_t i, void *u) {
    sdi12_value_t v = {0};
    if (i == 0) { v.value = read_temp(); v.decimals = 2; }
    return v;
}

/* 1. Define */
SDI12_SENSOR_DEFINE(my_sensor, '0', "MYCO    ", "TEMP  ", "100", "SN001   ",
                    my_send, my_dir, my_read);

void setup(void) {
    SDI12_SENSOR_SETUP(my_sensor);                  /* 2. Init  */
    SDI12_SENSOR_ADD_PARAM(my_sensor, 0, "TA", "C", 2);  /* 3. Add a param */
}

void on_command(const char *cmd, size_t len) {
    SDI12_SENSOR_PROCESS(my_sensor, cmd, len);      /* 4. Done! */
}
```

### Easy Master (complete example)

```c
#include "sdi12_easy.h"

SDI12_MASTER_DEFINE(rec, my_send, my_recv, my_dir, my_break, my_delay);

void setup(void) { SDI12_MASTER_SETUP(rec); }

void read_sensor(char addr) {
    SDI12_MASTER_BREAK(rec);

    sdi12_meas_response_t m;
    SDI12_MASTER_MEASURE(rec, addr, &m);            /* Start measurement */

    if (m.wait_seconds > 0)
        SDI12_MASTER_WAIT(rec, addr, m.wait_seconds * 1000);

    sdi12_data_response_t d;
    SDI12_MASTER_GET_DATA(rec, addr, 0, false, &d); /* Read results */

    for (int i = 0; i < d.value_count; i++)
        printf("%.2f\n", d.values[i].value);
}
```

> **Easy macros**: [`examples/easy_sensor.c`](examples/easy_sensor.c),
> [`examples/easy_master.c`](examples/easy_master.c) |
> [`EasySensor`](examples/EasySensor/EasySensor.ino),
> [`EasyMaster`](examples/EasyMaster/EasyMaster.ino) (Arduino)
>
> **Raw API (bare headers)**: [`example_sensor.c`](examples/example_sensor.c),
> [`example_master.c`](examples/example_master.c) |
> [`BareSensor`](examples/BareSensor/BareSensor.ino),
> [`BareMaster`](examples/BareMaster/BareMaster.ino) (Arduino)
>
> **Interrupt-driven**: [`InterruptSensor`](examples/InterruptSensor/InterruptSensor.ino),
> [`InterruptMaster`](examples/InterruptMaster/InterruptMaster.ino) (Arduino) |
> [`interrupt_sensor.c`](examples/interrupt_sensor.c),
> [`interrupt_master.c`](examples/interrupt_master.c) (bare-metal)
>
> **Advanced API**: See the full Sensor and Master API sections below for
> complete control (EEPROM persistence, extended commands, binary high-volume,
> metadata, etc.)

---

## Sensor (Slave) API

Implement an SDI-12 sensor that responds to commands from a data recorder.

### 1. Define Callbacks

```c
#include <sdi12.h>
#include <sdi12_sensor.h>

/* Required: send response bytes on the SDI-12 bus */
void my_send(const char *data, size_t len, void *user_data) {
    uart_set_direction(TX);
    uart_write(data, len);
    uart_flush();
    uart_set_direction(RX);
}

/* Required: set bus direction */
void my_dir(sdi12_dir_t dir, void *user_data) {
    gpio_write(DIR_PIN, dir == SDI12_DIR_TX ? HIGH : LOW);
}

/* Required: read a measurement parameter by index */
sdi12_value_t my_read_param(uint8_t param_index, void *user_data) {
    sdi12_value_t val = {0};
    switch (param_index) {
        case 0: val.value = read_temperature(); val.decimals = 2; break;
        case 1: val.value = read_humidity();    val.decimals = 1; break;
    }
    return val;
}
```

### 2. Initialize

```c
sdi12_sensor_ctx_t ctx;
sdi12_ident_t ident = {0};
memcpy(ident.vendor, "MYVENDOR", 8);
memcpy(ident.model, "MDL001", 6);
memcpy(ident.firmware_version, "100", 3);

sdi12_sensor_callbacks_t cb = {0};
cb.send_response = my_send;
cb.set_direction = my_dir;
cb.read_param    = my_read_param;

sdi12_sensor_init(&ctx, '0', &ident, &cb);

/* Register measurement parameters in group 0 */
sdi12_sensor_register_param(&ctx, 0, "TA", "C",   2);  /* Temperature */
sdi12_sensor_register_param(&ctx, 0, "RH", "%RH", 1);  /* Humidity   */
```

### 3. Process Commands

```c
/* In your main loop, when a complete SDI-12 command arrives: */
sdi12_sensor_process(&ctx, buffer, length);

/* After measurement hardware finishes (for M/C commands): */
sdi12_sensor_measurement_done(&ctx);

/* On break signal detection: */
sdi12_sensor_break(&ctx);
```

### Optional Callbacks

| Callback | Purpose |
|---|---|
| `save_address` | Persist address to flash/EEPROM on `aAb!` change |
| `load_address` | Restore address on init (overrides default) |
| `xcmd_handler` | Handle extended commands (`aX...!`) |
| `format_binary_page` | Custom binary encoding for `aHB!` data pages |

### Extended Commands

```c
sdi12_err_t my_reset(const char *xcmd, char *resp, size_t len, void *ud) {
    system_reset();
    return SDI12_OK;
}

sdi12_sensor_register_xcmd(&ctx, "RST", my_reset);
/* Responds to "0XRST!" */
```

---

## Master (Data Recorder) API

Communicate with SDI-12 sensors on the bus.

### 1. Define Callbacks

```c
#include <sdi12.h>
#include <sdi12_master.h>

void my_send(const char *data, size_t len, void *ud) { uart_tx(data, len); }
size_t my_recv(char *buf, size_t max, uint32_t timeout_ms, void *ud) {
    return uart_rx(buf, max, timeout_ms);
}
void my_dir(sdi12_dir_t dir, void *ud) { gpio_set(DIR_PIN, dir); }
void my_break(void *ud) { uart_send_break(12); }
void my_delay(uint32_t ms, void *ud) { delay_ms(ms); }
```

### 2. Initialize

```c
sdi12_master_ctx_t ctx;
sdi12_master_callbacks_t cb = {
    .send       = my_send,
    .recv       = my_recv,
    .set_direction = my_dir,
    .send_break = my_break,
    .delay      = my_delay,
};
sdi12_master_init(&ctx, &cb);
```

### 3. Take Measurements

```c
/* Wake the bus */
sdi12_master_send_break(&ctx);

/* Start measurement on sensor '0' */
sdi12_meas_response_t mresp;
sdi12_master_start_measurement(&ctx, '0', SDI12_MEAS_STANDARD, 0, false, &mresp);

/* Wait for service request if needed */
if (mresp.wait_seconds > 0) {
    sdi12_master_wait_service_request(&ctx, '0', mresp.wait_seconds * 1000);
}

/* Retrieve data */
sdi12_data_response_t dresp;
sdi12_master_get_data(&ctx, '0', 0, false, &dresp);

for (int i = 0; i < dresp.value_count; i++) {
    printf("Value %d: %.2f\n", i, dresp.values[i].value);
}
```

### Pure Parsing (No I/O)

These functions work without callbacks — useful for parsing stored responses:

```c
/* Parse "00053" → address='0', wait=5s, count=3 */
sdi12_meas_response_t resp;
sdi12_master_parse_meas_response("00053", 5, SDI12_MEAS_STANDARD, &resp);

/* Parse "+1.23-4.56+7.89" → 3 values */
sdi12_value_t vals[10];
uint8_t count;
sdi12_master_parse_data_values("+1.23-4.56+7.89", 15, vals, 10, &count, false);
```

---

## CRC-16-IBM

The library includes a full CRC implementation per the SDI-12 v1.4 specification:

```c
#include <sdi12.h>

/* Compute CRC over raw bytes */
uint16_t crc = sdi12_crc16("0+1.23+4.56", 11);

/* Encode to 3 ASCII characters */
char encoded[4];
sdi12_crc_encode_ascii(crc, encoded);

/* Append CRC before \r\n in a response buffer */
char buf[64] = "0+1.23+4.56\r\n";
sdi12_crc_append(buf, sizeof(buf));

/* Verify a received CRC-bearing response */
bool ok = sdi12_crc_verify("0+1.23+4.56XYZ\r\n", 17);
```

**Algorithm**: CRC-16-IBM, polynomial 0xA001 (reflected), initial value 0x0000.
Each 16-bit CRC is encoded as 3 printable ASCII characters (6 bits each, OR'd
with 0x40).

---

## Error Handling

All API functions return `sdi12_err_t`:

| Code | Meaning |
|---|---|
| `SDI12_OK` | Success |
| `SDI12_ERR_INVALID_ADDRESS` | Address not in `[0-9A-Za-z]` |
| `SDI12_ERR_INVALID_COMMAND` | Malformed or unrecognised command |
| `SDI12_ERR_BUFFER_OVERFLOW` | Response exceeds buffer capacity |
| `SDI12_ERR_NOT_ADDRESSED` | Command addressed to a different sensor |
| `SDI12_ERR_TIMEOUT` | No response within timeout period |
| `SDI12_ERR_CRC` | CRC verification failed |

---

## SDI-12 Protocol Reference

| Parameter | Value |
|---|---|
| Baud rate | 1200 |
| Data format | 7 data bits, even parity, 1 stop bit (7E1) |
| Logic | Inverted (marking = low, spacing = high) |
| Valid addresses | `0`–`9`, `A`–`Z`, `a`–`z` (62 total) |
| Break signal | ≥ 12 ms spacing |
| Marking after break | ≥ 8.33 ms |
| Max response time | 15 ms (after marking) |

Conforms to **SDI-12 v1.4** (February 20, 2023).

---

## Testing

98 unit tests run on desktop without any hardware or external dependencies.

### Standalone (any C compiler)

```bash
cd test
make            # or: make CC=clang
./test_sdi12    # 98 Tests 0 Failures
```

The test suite uses a **self-contained single-header test framework**
(`sdi12_test.h`) — no Unity, no Google Test, no package manager. Just a C
compiler and `make`.

### PlatformIO

```bash
pio test -e native    # if using PlatformIO with Unity
```

### CMake

```bash
mkdir build && cd build
cmake .. -DSDI12_BUILD_TESTS=ON
make && ctest
```

### Test Categories

| Suite | Tests | What It Covers |
|---|---:|---|
| CRC-16 | 15 | Encode, decode, append, verify, roundtrip, edge cases |
| Address | 7 | Valid/invalid ranges, boundary chars, total count |
| Sensor | 36 | All command types, state machine, callbacks, metadata |
| Master | 21 | Measurement parsing, data extraction, CRC strip |
| Metamorphic | 19 | Property-based: mutation detection, determinism, bijection, sign-flip, partition completeness |
| **Total** | **98** | |

---

## Commercial Support & Services

Building an SDI-12 product and need help? I offer
professional services for teams and companies using libsdi12:

| Service | Description |
|---|---|
| **Integration support** | Get libsdi12 running on your MCU/RTOS with hands-on help |
| **Custom sensor firmware** | Turnkey SDI-12 sensor firmware for your hardware |
| **Protocol consulting** | SDI-12 v1.4 compliance review, bus debugging, timing analysis |
| **Driver development** | UART/GPIO HAL drivers for your specific platform |
| **Extended features** | Custom command handlers, binary high-volume encoding, multi-drop networks |
| **Training** | Workshops on SDI-12 protocol internals and embedded best practices |

📧 **Contact**: [phillipweinstock@gmail.com](mailto:phillipweinstock@gmail.com)

> The library itself is and will always be **free and open source** (MIT).
> Commercial support is available for teams that want expert guidance, faster
> integration, or custom development.

---

## License

MIT — see [LICENSE](LICENSE) for the full text.

## Author

Phillip Weinstock — © 2026 All Rights Reserved.
