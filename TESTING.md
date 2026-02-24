# Testing libsdi12

libsdi12 ships with **98 tests** across 5 categories, all runnable on desktop
without any hardware, SDI-12 bus, or external test framework.

---

## Quick Start

### Any C Compiler (recommended)

```bash
cd test
make                   # builds and runs with gcc
make CC=clang          # use clang instead
make CC=x86_64-w64-mingw32-gcc   # cross-compile on Linux for Windows
```

Output:

```
  PASS: test_crc16_empty
  PASS: test_crc16_single_char
  ...
  PASS: test_meta_parse_meas_address_passthrough

-----------------------
98 Tests 0 Failures 0 Ignored
OK
```

### PlatformIO

If your project already uses PlatformIO, add a native test environment:

```ini
[env:native]
platform = native
build_flags = -std=c11
test_filter = test_libsdi12*
```

Then run:

```bash
pio test -e native
```

### CMake

```bash
mkdir build && cd build
cmake .. -DSDI12_BUILD_TESTS=ON
make && ctest --output-on-failure
```

---

## Test Architecture

### Self-Contained Framework

Tests use `sdi12_test.h` — a single-header, zero-dependency test framework
included in the `test/` directory. It provides a Unity-compatible API:

| Macro | Description |
|---|---|
| `UNITY_BEGIN()` | Reset counters |
| `UNITY_END()` | Print summary, return failure count |
| `RUN_TEST(fn)` | Run test with setUp/tearDown, catch failures |
| `TEST_ASSERT_TRUE(x)` | Assert boolean true |
| `TEST_ASSERT_EQUAL(e, a)` | Assert integer equality |
| `TEST_ASSERT_EQUAL_STRING(e, a)` | Assert string equality |
| `TEST_ASSERT_FLOAT_WITHIN(d, e, a)` | Assert float within delta |
| `TEST_ASSERT_EQUAL_HEX16(e, a)` | Assert 16-bit hex equality |
| `TEST_ASSERT_EQUAL_CHAR(e, a)` | Assert char equality |
| `TEST_ASSERT_NOT_NULL(p)` | Assert non-null pointer |
| `TEST_ASSERT_*_MESSAGE(…, msg)` | All of the above with custom message |

Failure isolation uses `setjmp`/`longjmp` — a failing assertion aborts only
that test, not the entire suite.

### Mock Infrastructure

Sensor tests use mock callbacks defined in `test_sensor.c`:

| Mock | Purpose |
|---|---|
| `mock_send_response` | Captures the response string into `mock_response[]` |
| `mock_set_direction` | Records direction changes |
| `mock_read_param` | Returns fixed values (42.0, 25.50, 101.3, 65.00, -10.5) |
| `mock_save_address` | Records persisted address |
| `mock_load_address` | Returns saved address |
| `reset_mocks()` | Clear all mock state between tests |
| `create_test_ctx(addr)` | Create a fully configured sensor context |

These are non-static, so `test_metamorphic.c` reuses them via `extern` declarations.

---

## Test Categories

### 1. CRC-16 Tests — `test_crc.c` (15 tests)

Tests the CRC-16-IBM implementation used for MC/CC/RC command variants.

| Test | What It Verifies |
|---|---|
| `test_crc16_empty` | CRC of empty string is 0x0000 |
| `test_crc16_single_char` | Single char produces non-zero CRC |
| `test_crc16_known_vector` | Deterministic output for known input |
| `test_crc16_different_data_differs` | Different inputs → different CRCs |
| `test_crc_encode_ascii_zero` | 0x0000 encodes to "@@@" |
| `test_crc_encode_ascii_all_ones` | 0xFFFF encodes correctly |
| `test_crc_encode_ascii_printable_range` | All outputs in 0x40–0x7F |
| `test_crc_append_basic` | Appends CRC + CRLF to data |
| `test_crc_append_with_existing_crlf` | Replaces existing CRLF correctly |
| `test_crc_append_buffer_overflow` | Returns error on tiny buffer |
| `test_crc_verify_valid` | Verifies a correctly CRC'd string |
| `test_crc_verify_corrupt_data` | Detects corrupted data byte |
| `test_crc_verify_corrupt_crc` | Detects corrupted CRC byte |
| `test_crc_verify_too_short` | Rejects strings too short for CRC |
| `test_crc_roundtrip_various` | Append + verify roundtrip on 4 strings |

### 2. Address Validation Tests — `test_address.c` (7 tests)

Tests the `sdi12_valid_address()` function.

| Test | What It Verifies |
|---|---|
| `test_valid_digits` | '0'–'9' are valid |
| `test_valid_uppercase` | 'A'–'Z' are valid |
| `test_valid_lowercase` | 'a'–'z' are valid |
| `test_invalid_special_chars` | Punctuation, space, symbols are invalid |
| `test_invalid_control_chars` | 0x00–0x1F are invalid |
| `test_invalid_boundaries` | Chars adjacent to valid ranges are invalid |
| `test_total_valid_count` | Exactly 62 valid addresses in ASCII range |

### 3. Sensor (Slave) Tests — `test_sensor.c` (36 tests)

Tests the complete sensor command parser and state machine.

| Group | Tests | Commands Covered |
|---|---|---|
| Initialization | 5 | `sdi12_sensor_init()` validation |
| Acknowledge | 3 | `a!`, `?!`, wrong address |
| Identification | 1 | `aI!` |
| Standard measurement | 3 | `aM!`, `aMC!`, `aM5!` |
| Concurrent measurement | 2 | `aC!`, `aCC!` |
| Send data | 3 | `aD0!` after M, MC, no-data |
| Continuous | 3 | `aR0!`, `aRC0!`, `aR9!` |
| Change address | 2 | `aA5!`, `aA!!` (invalid) |
| High-volume | 1 | `aH!` stub |
| Break handling | 2 | Abort measurement, NULL safety |
| Extended commands | 2 | `aXTEST!`, unregistered `aXFOO!` |
| Metadata | 4 | `aIM!`, `aIC!`, `aIM_001!`, `aIM_002!` |
| Parameter registration | 2 | Max params, group counts |
| Async measurement | 2 | Service request, concurrent (no SR) |
| Negative values | 1 | `-10.5` in data response |

### 4. Master (Data Recorder) Tests — `test_master.c` (21 tests)

Tests the pure parsing functions (no I/O required).

| Group | Tests | What It Parses |
|---|---|---|
| Measurement response | 10 | `atttn` (M), `atttnn` (C), `atttnnn` (H), edge cases |
| Data values | 11 | `+/-nn.nnn` extraction, CRC strip, capacity, NULL safety |

### 5. Metamorphic / Property-Based Tests — `test_metamorphic.c` (19 tests)

Tests *relations between outputs* rather than specific expected values.
These catch bugs that point-test oracles miss.

| Property | What It Proves |
|---|---|
| **CRC: Mutation detection** | Flipping any byte changes the CRC |
| **CRC: Roundtrip idempotency** | append → verify is always true (8 inputs) |
| **CRC: Non-idempotent append** | Double-append ≠ single-append |
| **CRC: Encoding bijection** | Different CRCs → different ASCII (no collisions) |
| **Address: Idempotent** | Checking validity twice gives same result |
| **Address: Partition complete** | 62 valid + 66 invalid = 128 total |
| **Sensor: Address reversible** | Change A→B→A restores original |
| **Sensor: Universal silence** | Wrong address → no response (all 61 others) |
| **Sensor: Deterministic M+D** | Same params → same response twice |
| **Sensor: Break from any state** | Always returns to READY |
| **Sensor: CRC adds 3 chars** | MC response is exactly 3 chars longer than M |
| **Sensor: HA vs M format** | HA response is 2 chars longer (nnn vs n) |
| **Sensor: HB with callback** | Binary callback is invoked |
| **Sensor: HB without callback** | Falls back to ASCII format |
| **Master: Sign-flip negation** | parse("+X") = −parse("−X") |
| **Master: Concatenation additive** | parse(A+B) = parse(A) ∪ parse(B) |
| **Master: Deterministic** | Same input → same output N times |
| **Master: Decimal count** | Parsed decimals match input dot position |
| **Master: Address passthrough** | All 62 addresses pass through correctly |

---

## File Layout

```
test/
├── sdi12_test.h          # Standalone test framework (single header)
├── Makefile              # Build with any C compiler
├── test_main.c           # Test runner — setUp/tearDown + all RUN_TEST calls
├── test_crc.c            # CRC-16 tests
├── test_address.c        # Address validation tests
├── test_sensor.c         # Sensor tests + mock infrastructure
├── test_master.c         # Master parser tests
└── test_metamorphic.c    # Property-based tests
```

---

## Adding a New Test

### 1. Write the test function

Add your test to the appropriate file (or create a new `.c` file):

```c
/* In test_sensor.c or a new file */
void test_sensor_my_new_feature(void)
{
    reset_mocks();
    sdi12_sensor_ctx_t ctx = create_test_ctx('0');

    sdi12_sensor_process(&ctx, "0M!", 3);
    TEST_ASSERT_EQUAL(SDI12_OK, err);
    TEST_ASSERT_EQUAL_STRING("00005\r\n", mock_response);
}
```

### 2. Declare and register in test_main.c

```c
/* Add extern declaration */
extern void test_sensor_my_new_feature(void);

/* Add to main() */
RUN_TEST(test_sensor_my_new_feature);
```

### 3. If using a new file, add to Makefile

```makefile
TEST_SRCS = test_main.c test_crc.c ... test_my_new.c
```

### 4. Build and run

```bash
make test
```

---

## Design Principles

1. **No hardware** — all tests run on desktop via mock callbacks
2. **No external dependencies** — `sdi12_test.h` replaces Unity/Google Test
3. **Deterministic** — no random seeds, no timing, no threads
4. **Fast** — full suite runs in <1 second
5. **Portable** — tested with gcc, works with clang and MSVC
6. **Isolated** — each test gets fresh mock state via `reset_mocks()`
7. **Complete** — every SDI-12 v1.4 command type has at least one test
