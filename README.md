# libsdi12

**The most complete, portable SDI-12 library available.**

A pure C implementation of the **SDI-12 v1.4** protocol covering **every
command in the specification** — both **sensor (slave)** and **master (data
recorder)** roles — with zero external dependencies.

- ✅ **Full v1.4 spec coverage** — every command type, including high-volume,
  concurrent, continuous, verification, metadata, extended, and CRC variants
- ✅ **Dual-role** — sensor *and* master in one library
- ✅ **Beginner-friendly** — `sdi12_easy.h` convenience macros: sensor in 4
  lines, master in 3
- ✅ **Pure C11, no `malloc`** — all state lives in user-allocated context
  structs; UART, GPIO, and timing are abstracted behind callbacks
- ✅ **192 tests** — unit + metamorphic/property-based, all runnable on
  desktop with no hardware and no external test framework
- ✅ **Compiles anywhere** — `gcc`, `clang`, `armcc`, `arm-none-eabi-gcc`,
  MSVC, PlatformIO, Arduino, CMake, or a bare Makefile
- ✅ **C++ compatible** — all headers wrapped in `extern "C"`

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
| `aDB0!`–`aDB999!` | Binary data packets (§5.2) | ✅ | ✅ |
| `aV!` | Verification | ✅ | ✅ |
| `aAb!` | Change address | ✅ | ✅ |
| `aX…!` | Extended commands | ✅ | ✅ |
| `aHA!` | High-volume ASCII (pages always CRC-protected) | ✅ | ✅ |
| `aHB!` | High-volume binary | ✅¹ | ✅ |
| `aIM!` `aIC!` `aIM_nnn!` … | Metadata / parameter identification | ✅ | ✅ |
| CRC-16-IBM | Compute, append, verify | ✅ | ✅ |
| Break signal | Detect / send | ✅ | ✅ |
| Service request (`a\r\n`) | Async measurement complete | ✅ | ✅ |

¹ Binary payload encoding is manufacturer-defined, so the sensor side
delegates it to a `format_binary_page` callback; framing and CRC are handled
by the library.

### What the library does NOT do

The protocol engine is deliberately transport- and policy-free. Building a
fully §7-conformant data recorder additionally requires, **in your code**:

- **Retries (§7.2)** — the spec requires recorders to retry after 16.67–87 ms
  and to repeat the break+command sequence; `sdi12_master_transact()` sends
  once. The strict parser returns `SDI12_ERR_PARSE_FAILED`/`SDI12_ERR_TIMEOUT`
  so your retry loop has the triggers it needs.
- **Break scheduling (§7.1)** — send a break before addressing a new sensor
  and after 87 ms of marking; the library never sends breaks on its own.
- **Multi-line STX/ETX framing (§4.4.13.1)** — `sdi12_master_extended_multiline()`
  concatenates lines by timing gap and passes any STX/ETX bytes through raw.

**RAM budget note**: `sdi12_master_get_hv_binary_data()` uses a
`SDI12_BIN_MAX_PAYLOAD + 2` (default 1002-byte) stack buffer, and the context
structs are ~1.2 KB. On AVR-class targets (2 KB SRAM) avoid the high-volume
binary API or rebuild with `-DSDI12_BIN_MAX_PAYLOAD=<smaller>` /
`-DSDI12_MAX_RESPONSE_LEN=82`.

### Comparison With Other Libraries

| Feature | libsdi12 | Arduino-SDI-12 | Others |
|---|:---:|:---:|:---:|
| Full v1.4 command set | ✅ | Partial | Partial |
| Sensor (slave) role | ✅ | ❌ | Rare |
| Master (recorder) role | ✅ | ✅ | ✅ |
| CRC-16 (MC/CC/RC) | ✅ | ❌ | Rare |
| High-volume (HA/HB, aDBn!) | ✅ | ❌ | ❌ |
| Metadata (IM/IC) | ✅ | ❌ | ❌ |
| Platform independent | ✅ | Arduino | Varies |
| No `malloc` | ✅ | ❌ | Varies |
| Test suite | 192 tests | ❌ | Minimal |

---

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
├── CMakeLists.txt       # CMake build support
├── examples/            # Arduino sketches + plain-C examples
│   ├── EasySensor/ EasyMaster/          # ★ Easy-macro Arduino sketches
│   ├── BareSensor/ BareMaster/          # Raw-API Arduino sketches
│   ├── InterruptSensor/ InterruptMaster/  # ISR-driven Arduino sketches
│   ├── easy_sensor.c / easy_master.c    # ★ Minimal plain-C examples
│   ├── example_sensor.c / example_master.c  # Full raw-API walkthroughs
│   ├── interrupt_sensor.c / interrupt_master.c  # Bare-metal (Cortex-M)
│   └── example_crc.c                    # Standalone CRC demo
├── test/                # Self-contained test suite (see TESTING.md)
├── TESTING.md           # Test documentation & architecture
└── README.md
```

## Quick Start

### PlatformIO

Drop the `libsdi12/` folder into your project's `lib/` directory. PlatformIO
auto-discovers it via `library.json`. Then include:

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

Don't want to deal with structs, callback tables, and init boilerplate?
Include `sdi12_easy.h` and get going in **4 lines**:

### Easy Sensor (complete example)

```c
#include "sdi12_easy.h"

/* Write your 2 hardware functions once.
 * my_send owns TX/RX switching: TX, write, flush, back to RX. */
void my_send(const char *d, size_t n, void *u) {
    gpio_set(DIR, TX); uart_write(d, n); uart_flush(); gpio_set(DIR, RX);
}
sdi12_value_t my_read(uint8_t i, void *u) {
    sdi12_value_t v = {0};
    if (i == 0) { v.value = read_temp(); v.decimals = 2; }
    return v;
}

/* 1. Define */
SDI12_SENSOR_DEFINE(my_sensor, '0', "MYCO    ", "TEMP  ", "100", "SN001   ",
                    my_send, my_read);

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
> **Raw API**: [`example_sensor.c`](examples/example_sensor.c),
> [`example_master.c`](examples/example_master.c) |
> [`BareSensor`](examples/BareSensor/BareSensor.ino),
> [`BareMaster`](examples/BareMaster/BareMaster.ino) (Arduino)
>
> **Interrupt-driven**: [`InterruptSensor`](examples/InterruptSensor/InterruptSensor.ino),
> [`InterruptMaster`](examples/InterruptMaster/InterruptMaster.ino) (Arduino) |
> [`interrupt_sensor.c`](examples/interrupt_sensor.c),
> [`interrupt_master.c`](examples/interrupt_master.c) (bare-metal)

---

## Sensor (Slave) API

Implement an SDI-12 sensor that responds to commands from a data recorder.

### 1. Define Callbacks

```c
#include <sdi12.h>
#include <sdi12_sensor.h>

/* Required: send response bytes on the SDI-12 bus.
 * Owns TX/RX switching: switch to TX, write, flush, switch back to RX. */
void my_send(const char *data, size_t len, void *user_data) {
    uart_set_direction(TX);
    uart_write(data, len);
    uart_flush();
    uart_set_direction(RX);
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

/* When your async measurement hardware finishes (for M/C commands),
 * hand the values to the library — it sends the service request for
 * M/V and makes the data available for D commands: */
sdi12_value_t vals[] = { {23.10f, 2}, {55.4f, 1} };
sdi12_sensor_measurement_done(&ctx, vals, 2);

/* On break signal detection: */
sdi12_sensor_break(&ctx);
```

### Optional Callbacks

| Callback | Purpose |
|---|---|
| `save_address` / `load_address` | Persist address to flash/EEPROM across power cycles |
| `start_measurement` | Begin an async measurement; return `ttt` seconds (NULL = synchronous) |
| `service_request` | Custom service-request transmit (NULL = uses `send_response`) |
| `meas_duration` | Expected `ttt` for `aIM!`-family metadata (NULL = reports 000; async sensors should provide it) |
| `format_binary_page` | Manufacturer-defined binary encoding for `aHB!` / `aDBn!` pages |

### Extended Commands

Registered separately with `sdi12_sensor_register_xcmd()`:

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

/* Start a CRC-protected measurement on sensor '0' */
sdi12_meas_response_t mresp;
sdi12_master_start_measurement(&ctx, '0', SDI12_MEAS_STANDARD, 0, true, &mresp);

/* Wait for service request if needed */
if (mresp.wait_seconds > 0) {
    sdi12_master_wait_service_request(&ctx, '0', mresp.wait_seconds * 1000);
}

/* Retrieve data — CRC is verified before parsing */
sdi12_data_response_t dresp;
sdi12_err_t err = sdi12_master_get_data(&ctx, '0', 0, true, &dresp);
if (err == SDI12_ERR_CRC_MISMATCH) {
    /* corrupt data on the bus — retry */
}

for (int i = 0; i < dresp.value_count; i++) {
    printf("Value %d: %.2f\n", i, dresp.values[i].value);
}
```

With `crc=true`, `sdi12_master_get_data()` and `sdi12_master_continuous()`
verify the CRC over the raw response and set `resp.crc_valid`; corrupt data
returns `SDI12_ERR_CRC_MISMATCH`. Responses whose address doesn't match the
queried sensor are rejected with `SDI12_ERR_INVALID_ADDRESS`.

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

## Configuration

The response buffers inside the context structs default to 82 bytes — enough
for every standard ASCII response. For larger `aDBn!` binary packets, enlarge
them at compile time:

```
-DSDI12_MAX_RESPONSE_LEN=1006    # fits the spec's 1000-byte payload cap
```

Payload per binary packet = `SDI12_MAX_RESPONSE_LEN − 6` (address + size +
type + CRC overhead). The default is fully spec-compliant — small packets
just mean the recorder issues more `aDBn!` commands until it receives an
empty packet (§5.2.2).

> **Note:** overriding this changes `sizeof(sdi12_sensor_ctx_t)` and
> `sizeof(sdi12_master_ctx_t)`. Compile the library and your application with
> the same value — don't override it when linking against a prebuilt shared
> library.

---

## Spec Conformance Notes

Two v1.4 behaviors that regularly surprise integrators — libsdi12 implements
both per the spec:

- **Breaks do not abort concurrent measurements** (§4.4.7). The recorder is
  *expected* to wake the sensor with a break before issuing `aD0!`. Breaks
  do abort standard `aM!` measurements (§4.4.5.1).
- **Any command addressed to the sensor aborts its concurrent measurement —
  including `aD0!`** (§4.4.7.1). That is the spec's abort mechanism. Don't
  poll a concurrent measurement with D commands; wait the full `ttt`.

---

## CRC-16-IBM

Full CRC implementation per SDI-12 v1.4 §4.4.12:

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

**Algorithm**: CRC-16-IBM, polynomial 0xA001 (reflected), initial value
0x0000. Encoded as 3 printable ASCII characters (6 bits each, OR'd with 0x40).

---

## Error Handling

All API functions return `sdi12_err_t`:

| Code | Meaning |
|---|---|
| `SDI12_OK` | Success |
| `SDI12_ERR_INVALID_ADDRESS` | Address not in `[0-9A-Za-z]`, or response from wrong sensor |
| `SDI12_ERR_INVALID_COMMAND` | Malformed or unrecognised command |
| `SDI12_ERR_BUFFER_OVERFLOW` | Response exceeds buffer capacity |
| `SDI12_ERR_NOT_ADDRESSED` | Command addressed to a different sensor |
| `SDI12_ERR_PARAM_LIMIT` | Parameter / extended-command table full |
| `SDI12_ERR_CALLBACK_MISSING` | Required callback not provided |
| `SDI12_ERR_TIMEOUT` | No response within timeout period |
| `SDI12_ERR_CRC_MISMATCH` | CRC verification failed |
| `SDI12_ERR_PARSE_FAILED` | Response could not be parsed |

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
| Max response time | 15 ms from the command's last stop bit (includes the 8.33 ms marking) |

Conforms to **SDI-12 v1.4** (February 20, 2023).

---

## Testing

**192 tests** run on desktop with no hardware and no external framework —
the suite ships its own single-header framework (`sdi12_test.h`):

```bash
cd test
make            # or: make CC=clang
./test_sdi12    # 192 Tests 0 Failures
```

| Suite | Tests | What It Covers |
|---|---:|---|
| CRC-16 | 17 | Encode, decode, append, verify, roundtrip, spec worked examples |
| Address | 7 | Valid/invalid ranges, boundary chars, total count |
| Sensor | 76 | All command types, state machine, D-page pagination (incl. exact 75-char boundary), value formatting, strict grammar, abort semantics |
| Master | 62 | Response parsing, CRC verification, address checks, scripted binary transactions, locale independence |
| Metamorphic | 19 | Property-based: mutation detection, determinism, bijection |

See [TESTING.md](TESTING.md) for architecture, CMake/PlatformIO runners, and
how to add tests.

---

## Why This Library Exists

In May 2023, as a university student trying to implement an SDI-12 sensor, I
emailed the SDI-12 Support Group asking if an open-source reference
implementation existed. I never got a reply.

SDI-12 has been an open standard since 1988. In that time dozens of
companies — Campbell Scientific, Meter Group, In-Situ, Xylem/YSI, Hach,
Stevens Water, and others — have built profitable product lines on it. Not
one released a complete, reusable, open-source implementation of the
protocol they all depend on.

So every embedded engineer who needs to talk to an SDI-12 sensor or build a
datalogger has had to reverse-engineer the spec, scrape snippets from
forums, or buy a proprietary SDK — for a 1200-baud serial protocol from 1988.

This library is what should have existed decades ago: a complete, portable,
tested, MIT-licensed SDI-12 implementation — sensor and master — in pure C
with zero dependencies.

If your organisation profits from SDI-12, consider contributing back — a PR,
sponsorship, or simply sharing this library with your users. An open
protocol ***deserves*** open source.

— *Phillip Weinstock, 2026*

---

## Commercial Support & Services

Building an SDI-12 product and need help? Professional services for teams
and companies using libsdi12:

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
> Commercial support is available for teams that want expert guidance,
> faster integration, or custom development.

---

## License

MIT — see [LICENSE](LICENSE) for the full text.

## Author

Phillip Weinstock — © 2026 All Rights Reserved.
