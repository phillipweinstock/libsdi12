# First-Byte Deadline, Timing Margins and Wake Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make libsdi12 recorders read compliant sensors on real UARTs, add per-bus timing margins (libsdi12 0.6.0), and give libsdi12-verifier a spec-judged wake/reply probe pinned to 0.6.0.

**Architecture:** libsdi12 gets a 24 ms first-byte constant, fixed example recv callbacks, and two `uint8_t` margins in existing struct padding with a validating setter. The verifier pins v0.6.0, stops doubling post-break marking, runs an auto-probe before the suite, and self-tests it through two new loopback faults.

**Tech Stack:** C11, in-repo test framework (`test/sdi12_test.h`, `make` in `test/`), CMake for the verifier, `--self-test` loopback HAL.

**Spec:** `docs/superpowers/specs/2026-10-17-timing-margins-design.md`

> **Amended after Part A review:** first-byte deadline 25 ms, multi-line 160 ms, reply-margin limit 62; Part B must also apply the ledger's Task 6 rulings (late retry just past 100 ms, any reply margin = FAIL, adapter-latency hint).

Repos: `L` = `/home/phillip/Documents/Projects/libsdi12`, `V` = `/home/phillip/Documents/Projects/libsdi12-verifier`.

## Global Constraints

- C11; `test/Makefile` flags `-std=c11 -Wall -Wextra -Wpedantic` must stay warning-free.
- Spec values (SDI-12 v1.4 §7.0–7.2): break ≥ 12 ms; post-break marking 8.33 ms (library uses 9); reply start bit ≤ 15 ms (+0.40); first byte observable ≤ 23.73 ms → `SDI12_FIRST_BYTE_TIMEOUT_MS 24`; > 87 ms marking needs a break; sensors may take 100 ms to wake.
- `SDI12_RESPONSE_TIMEOUT_MS` stays 15 and keeps all its non-first-byte uses.
- No new error codes. The setter returns `SDI12_ERR_INVALID_COMMAND` for NULL ctx or out-of-limit margins.
- Comments: one line, cite §, nothing superfluous. In every function you edit, cut multi-line explanatory blocks to the essential line.
- Commits: Conventional Commits, author Phillip Weinstock, **no Co-Authored-By or any AI trailer**. Tasks 1–2 (fixes) commit before Task 3 (margins).
- Never `git push` or push tags without asking Phillip first.
- Verifier tasks (5–8) start only after Phillip has committed the verifier's in-progress `response_too_early()` work and v0.6.0 is pushed.

## Review Focus

1. No sensor at the probed address: the probe must end in FAIL within a bounded time (< 30 s) and leave margins at (0, 0). Pinned by `fault_probe_absent` (Task 7).
2. Fault self-tests leaking faults or margins into later tests, making later results silently lenient. Pinned by `fault_probe_cleanup` (Task 7).
3. `uint8_t` arithmetic wrap in the setter (e.g. 255 + 9 computed in 8 bits passes the limit). Pinned by the (255, 255) case in `test_master_set_margin_limits` (Task 3).
4. Margins leaking into binary continuation reads or explicit `transact` timeouts. Pinned by `test_master_response_margin_extends_first_byte` (last-timeout assertion) and `test_master_margin_skips_explicit_transact` (Task 3).
5. Example recv given a reply with no LF (garbled line) must return after the idle timeout, not block. There's no Arduino toolchain here (`arduino-cli`/`pio` absent), so Task 2 checks it by review against the shared body below. The verifier's `LB_FAULT_PACED_UART` exercises the same contract on its own recv.

---

## Part A — libsdi12 (repo `L`)

### Task 1: First-byte deadline (§7.0)

**Files:**
- Modify: `sdi12.h` (timing constants, after `SDI12_RESPONSE_TIMEOUT_MS`)
- Modify: `sdi12_master.c:151,174,206,233,339,383,428,483,511,801,876`, plus the comment at `:199-202`
- Modify: `sdi12_master.h:48-66` (recv contract comment)
- Test: `test/test_master.c`, `test/test_main.c`

**Interfaces:**
- Produces: `#define SDI12_FIRST_BYTE_TIMEOUT_MS 24` in `sdi12.h`. Test mocks `m_first_timeout` (timeout of the first `recv` per scripted reply) and `m_last_delay` (last `delay` ms), reset in `make_scripted_master`, `set_reply` and `set_reply_bin`.

- [ ] **Step 1: Extend the scripted mocks and write the failing tests**

In `test/test_master.c`: add `static uint32_t m_first_timeout; static int m_recv_calls; static uint32_t m_last_delay;`. `m_recv` sets `m_first_timeout` when `m_recv_calls++ == 0`, and `m_dly` stores `m_last_delay = ms`. Reset `m_recv_calls = 0` in the three reset points. Add these tests and register them in `test/test_main.c` (extern + `RUN_TEST`, next to the other `test_master_*`):

```c
void test_master_first_byte_deadline_24ms(void)
{   /* acknowledge, reply "0\r\n" */
    TEST_ASSERT_EQUAL(24, SDI12_FIRST_BYTE_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(SDI12_FIRST_BYTE_TIMEOUT_MS, m_first_timeout);
}
void test_master_hv_binary_continuation_keeps_15ms(void)
{   /* hv binary roundtrip as in test_master_hv_binary_roundtrip_ok */
    TEST_ASSERT_EQUAL(SDI12_FIRST_BYTE_TIMEOUT_MS, m_first_timeout);
    TEST_ASSERT_EQUAL(SDI12_RESPONSE_TIMEOUT_MS, m_last_timeout);
}
void test_master_change_address_waits_1024ms(void)
{   /* change '0'→'1', reply "1\r\n" */
    TEST_ASSERT_EQUAL(SDI12_ADDRESS_CHANGE_DELAY_MS + SDI12_FIRST_BYTE_TIMEOUT_MS,
                      m_first_timeout);
}
void test_master_transact_explicit_timeout_exact(void)
{   /* sdi12_master_transact(&m, "0!", 200), reply "0\r\n" */
    TEST_ASSERT_EQUAL(200, m_first_timeout);
}
```

- [ ] **Step 2: Run, expect failure**

Run: `cd test && make && ./test_sdi12`
Expected: compile error, `SDI12_FIRST_BYTE_TIMEOUT_MS` undeclared.

- [ ] **Step 3: Implement**

- Add the constant with the comment `/** First-byte recv deadline: §7.0 15 ms + 0.40 tol + one 8.33 ms char. */`.
- Use it at the 11 first-byte sites listed above: 10 transactions, the change-address wait `SDI12_ADDRESS_CHANGE_DELAY_MS + ...`, and the HV header `recv_exact` at `:876`. `:884`, `:895` and `:902` keep `SDI12_RESPONSE_TIMEOUT_MS`.
- Cut the change-address comment to `/* §4.4.4: normal reply, then up to 1 s busy persisting the address */`.
- In `sdi12_master.h`, rewrite the recv contract's "The library passes the spec's 15 ms response window here…" so it names `SDI12_FIRST_BYTE_TIMEOUT_MS`, then tighten the rest of that block.

- [ ] **Step 4: Run, expect pass**

Run: `cd test && make && ./test_sdi12`
Expected: `200 Tests 0 Failures 0 Ignored`

- [ ] **Step 5: Commit**

```bash
git add sdi12.h sdi12_master.c sdi12_master.h test/test_master.c test/test_main.c
git commit -m "fix(master): wait 24 ms for a reply's first byte (§7.0)"
```

### Task 2: Example master recv callbacks follow the contract

**Files:**
- Modify: `examples/EasyMaster/EasyMaster.ino:23-35`, `examples/BareMaster/BareMaster.ino:64-77`, `examples/InterruptMaster/InterruptMaster.ino:69-82`, `examples/interrupt_master.c:83-96`
- Modify: `examples/easy_master.c:20-24` and `examples/example_master.c:30-35` (stub TODO text); `examples/example_master.c:44-49` (break)

**Interfaces:**
- Consumes: the recv contract in `sdi12_master.h` (Task 1).

- [ ] **Step 1: Rewrite the four recv bodies**

Use this shape, swapping each file's own clock and RX calls (`millis`/`hw_millis`, `Serial1`/`SDI12_UART`/`rx_available`+`rx_read_byte`/`rx_pop`). Unsigned subtraction keeps it safe across `millis()` wraparound:

```c
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
```

- [ ] **Step 2: Fix the stubs and the break example**

- Stub recv TODOs become one line: `/* TODO: wait ≤ timeout_ms for the first byte, then read to LF with a short idle timeout */`.
- In `example_master.c`, `gpio_hold_high(SDI12_PIN, 12)` becomes `SDI12_BREAK_MS`, and the break TODO becomes `/* TODO: set direction to TX first, then hold spacing ≥ SDI12_BREAK_MS */`.

- [ ] **Step 3: Verify**

Run: `cc -std=c11 -Wall -Wextra -fsyntax-only -I. examples/interrupt_master.c examples/easy_master.c examples/example_master.c`
Expected: no output.
For the `.ino` files: read each body against Step 1 (Review Focus 5). No Arduino toolchain is installed.

- [ ] **Step 4: Commit**

```bash
git add examples/
git commit -m "fix(examples): master recv waits for the first byte, then reads to LF"
```

### Task 3: Per-bus timing margins

**Files:**
- Modify: `sdi12_master.h` (macros, struct `:103-108`, setter prototype)
- Modify: `sdi12_master.c` (init `:104-116`, `send_break` `:122-132`, the 11 first-byte sites)
- Modify: `sdi12_easy.h` (after `SDI12_MASTER_BREAK`, `:301`)
- Modify: `debian/libsdi12-0.symbols`
- Test: `test/test_master.c`, `test/test_main.c`

**Interfaces:**
- Consumes: `SDI12_FIRST_BYTE_TIMEOUT_MS` and the test mocks (Task 1).
- Produces:
  - `#define SDI12_MASTER_MARKING_MARGIN_MS 0` and `#define SDI12_MASTER_RESPONSE_MARGIN_MS 0`, both `#ifndef`-guarded.
  - Fields `uint8_t marking_margin_ms; uint8_t response_margin_ms;` placed directly after `resp_buf`.
  - `sdi12_err_t sdi12_master_set_timing_margin(sdi12_master_ctx_t *ctx, uint8_t marking_ms, uint8_t response_ms);`
  - `SDI12_MASTER_MARGIN(name, mark, resp)`.

- [ ] **Step 1: Write the failing tests**

Add `#include <stddef.h>` and `#include "sdi12_easy.h"` to `test/test_master.c`. Add the tests below and register all 7:

```c
void test_master_margins_default_zero(void);              /* fresh ctx: both fields 0 */
void test_master_marking_margin_extends_break_marking(void);
    /* set(5,0)==SDI12_OK; send_break → m_last_delay == SDI12_MARKING_MS + 5 */
void test_master_response_margin_extends_first_byte(void);
    /* set(0,7): acknowledge → m_first_timeout == 31;
       change address → 1000 + 31;
       hv binary → m_first_timeout == 31 && m_last_timeout == SDI12_RESPONSE_TIMEOUT_MS */
void test_master_margin_skips_explicit_transact(void);
    /* set(0,7); transact(&m, "0!", 200) → m_first_timeout == 200 */
void test_master_set_margin_limits(void);
    /* set(78,63)==OK, fields 78/63;
       set(79,0), set(0,64), set(255,255) each == SDI12_ERR_INVALID_COMMAND
         and fields still 78/63;
       set(NULL,0,0) == SDI12_ERR_INVALID_COMMAND */
void test_master_margin_easy_macro(void);
    /* SDI12_MASTER_DEFINE(easy_rec, m_send, m_recv, m_dir, m_brk, m_dly);
       SDI12_MASTER_SETUP(easy_rec); SDI12_MASTER_MARGIN(easy_rec,3,4)==OK;
       fields 3/4 */
void test_master_ctx_layout_unchanged(void);
    /* mirror of the 0.5.4 layout:
       struct ctx_v054 { sdi12_master_callbacks_t cb;
         char cmd_buf[SDI12_CMD_MAX_CHARS + 4];
         char resp_buf[SDI12_RESP_MAX_CHARS + 4]; size_t resp_len; };
       sizeof and offsetof(resp_len) equal to sdi12_master_ctx_t's */
```

- [ ] **Step 2: Run, expect failure**

Run: `cd test && make && ./test_sdi12`
Expected: compile error, `sdi12_master_set_timing_margin` / `marking_margin_ms` undeclared.

- [ ] **Step 3: Implement**

- **Macros:** add them to `sdi12_master.h` with no comments beyond a one-line group label.
- **`sdi12_master_init`:** after the memset, load both fields from the macros.
- **Setter:** the limits are `SDI12_MARKING_MS + marking_ms ≤ SDI12_MARKING_TIMEOUT_MS` and `SDI12_FIRST_BYTE_TIMEOUT_MS + response_ms ≤ SDI12_MARKING_TIMEOUT_MS`. Compute in `uint32_t`, and on rejection leave both fields unchanged. Its doc comment states the 87 ms rule (§7.0) in one line, plus one line saying port overhead lands on top.
- **`send_break`:** the delay becomes `SDI12_MARKING_MS + ctx->marking_margin_ms`.
- **First-byte waits:** add `static uint32_t first_byte_ms(const sdi12_master_ctx_t *ctx)` (constant + response margin) and use it at the 11 Task 1 sites. Continuation reads stay on `SDI12_RESPONSE_TIMEOUT_MS`.
- **Easy macro:** `SDI12_MASTER_MARGIN` with a one-line `@brief`.
- **Symbols file:** add ` sdi12_master_set_timing_margin@Base 0.6.0` in alphabetical position (between `send_break` and `start_measurement`).

- [ ] **Step 4: Run, expect pass**

Run: `cd test && make && ./test_sdi12`
Expected: `207 Tests 0 Failures 0 Ignored`
Also run `cmake -B /tmp/claude-1000/-home-phillip-Documents-Projects-libsdi12/ea9b924c-893a-4b43-8f62-7af082bd01f4/scratchpad/build -S . && cmake --build <that dir>` and expect a clean build with the shared lib.

- [ ] **Step 5: Commit**

```bash
git add sdi12_master.h sdi12_master.c sdi12_easy.h debian/libsdi12-0.symbols test/test_master.c test/test_main.c
git commit -m "feat(master): per-bus timing margins for slow sensors"
```

### Task 4: Docs and release 0.6.0

**Files:**
- Modify: `README.md`, `TESTING.md`, `library.json`, `library.properties`, `CMakeLists.txt:3`, `sdi12.h:29-31`, `debian/changelog`

**Interfaces:**
- Consumes: the API from Task 3.

- [ ] **Step 1: README note "Slow sensors and cheap transceivers"**

Put it next to the §7 recorder checklist around `README.md:64`, in four short bullets taken from the spec's Docs section:
- the setter is primary, and the macros only affect source builds (PlatformIO `build_flags`, CMake), not the Debian shared lib or the Arduino IDE;
- the break is the port's job: direction first, then `SDI12_BREAK_MS` plus its own margin;
- escalate the margin from the retry loop;
- the marking margin is a shortcut (§7.0 allows a 100 ms wake, answered by §7.2 retries), while the reply margin tolerates late repliers.

- [ ] **Step 2: Test counts**

- Change 196 → 207 everywhere it appears: README ×3, TESTING.md top line and sample output, `library.json` description, `library.properties` paragraph.
- In TESTING.md, change the master suite heading 71 → 82 and add two table rows: "First-byte deadline | 4" and "Timing margins | 7".

- [ ] **Step 3: Version 0.6.0**

- Bump `CMakeLists.txt` `VERSION 0.6.0`, `sdi12.h` MINOR 6 / PATCH 0, `library.json`, and `library.properties`.
- Prepend a `debian/changelog` entry `libsdi12 (0.6.0-1) unstable; urgency=medium`. Give it three bullets (first-byte fix, example recv fix, margins API), copy the maintainer line from 0.5.4-1, and date it with `date -R`.

- [ ] **Step 4: Verify**

Run: `cd test && make && ./test_sdi12 | tail -2` and `grep -rn "196" README.md TESTING.md library.*`
Expected: `207 Tests 0 Failures`; grep returns nothing.

- [ ] **Step 5: Commit and tag**

```bash
git add -u
git commit -m "chore: release 0.6.0"   # body: one line per change, "Suite: 207 tests."
git tag -a v0.6.0 -m "libsdi12 0.6.0"
```

- [ ] **Step 6: Ask Phillip before pushing**

Ask Phillip, and only on his OK run `git push origin master v0.6.0`. Also ask whether to run `gh release create v0.6.0`.

---

## Part B — libsdi12-verifier (repo `V`)

Gate: Phillip has committed the `response_too_early()` work in progress, and `v0.6.0` is pushed.

### Task 5: Pin v0.6.0; single post-break marking; first-byte waits

**Files:**
- Modify: `lib/libsdi12` (submodule), `include/timing.h` (after `timing_master` `:101`), `src/timing.c:99-113`
- Modify: `src/sensor_tests.c:1188,1273,1296,1593`, `src/selftest_faults.c:132,320`

**Interfaces:**
- Produces: `static inline uint32_t timing_first_byte_ms(timing_ctx_t *ctx)` returning `SDI12_FIRST_BYTE_TIMEOUT_MS + timing_master(ctx)->response_margin_ms`.

- [ ] **Step 1: Record the baseline**

Run: `cmake -B build -S . && cmake --build build && ./build/sdi12-verifier --self-test | grep SUMMARY`
Expected: a SUMMARY line. Note it as BASELINE.

- [ ] **Step 2: Pin and commit**

```bash
git -C lib/libsdi12 fetch --tags && git -C lib/libsdi12 checkout v0.6.0
git add lib/libsdi12
git commit -m "chore: pin libsdi12 submodule to v0.6.0 release tag"
```

- [ ] **Step 3: Implement**

- Add `timing_first_byte_ms` to `include/timing.h`.
- Replace the six `SDI12_RESPONSE_TIMEOUT_MS` recv deadlines listed above with it. Leave every `spec_limit_us` alone.
- In `cb_break`, delete the 9 ms marking delay and its comment, keep the 15 ms break, and give it the comment `/* §7.0: break ≥ 12 ms; 15 for USB-serial jitter */`.
- Check every reader of `break_end_us` (`grep -n break_end_us src/*.c`): it now means break end, not break end + 9 ms. Adjust any test that assumed otherwise, and say which ones in the commit body.

- [ ] **Step 4: Verify**

Run: `cmake --build build && ./build/sdi12-verifier --self-test | grep SUMMARY`
Expected: identical to BASELINE.

- [ ] **Step 5: Commit**

```bash
git commit -am "fix: single post-break marking; first-byte waits use libsdi12's deadline"
```

### Task 6: Wake/reply probe, integration and report

**Files:**
- Create: `include/wake_probe.h`, `src/wake_probe.c`
- Modify: `CMakeLists.txt` (`add_executable` list, `:42`), `src/main.c:241-275`, `include/reporter.h` (`report_t`), `src/reporter.c` (text header `~:108`, JSON `~:195`)

**Interfaces:**
- Consumes: `sdi12_master_set_timing_margin`, `SDI12_FIRST_BYTE_TIMEOUT_MS`, `timing_master()`.
- Produces:
  - `test_result_t wake_probe_run(timing_ctx_t *ctx, char addr);` It leaves the found margins applied to `timing_master(ctx)`. Result name `"Wake-up & Reply Timing Probe"`, key `"wake_probe"`, spec `"\xc2\xa7" "7.0"`.
  - `report_t` fields `uint8_t marking_margin_ms, response_margin_ms;`.

- [ ] **Step 1: Implement the probe**

Constants: `PROBE_TRIES 3`, `PROBE_STEP_MS 2`, `LATE_RETRY_GAP_MS 50`, `LATE_RETRIES 3`. The limits are `SDI12_MARKING_TIMEOUT_MS - SDI12_MARKING_MS` (78) and `SDI12_MARKING_TIMEOUT_MS - SDI12_FIRST_BYTE_TIMEOUT_MS` (62).

An attempt at (m, r) does `set_timing_margin(m, r)`, then `send_break`, then `sdi12_master_acknowledge`. It succeeds when the result is OK and the sensor is present. (m, r) **works** when `PROBE_TRIES` attempts all succeed, stopping at the first miss.

```
s0 = successes of PROBE_TRIES attempts at (0,0)
if s0 == 3                      → PASS "Wakes and replies at spec timing"
find first working margin, PROBE_STEP_MS steps, each phase up to its limit:
   phase 1: (m, 0) m = 2..78
   phase 2: (0, r) r = 2..62
   phase 3: (k, k) k = 2..62
found (m, 0)                    → PASS "Missed the command at minimum marking; answers with +%u ms (wake within §7.0's 100 ms)"
found with r > 0 and s0 == 0    → FAIL "Reply starts late: needs +%u ms past the 25 ms first-byte deadline; §7.0 allows 15 ms (+0.40)"
found with r > 0 and s0 > 0     → PASS "Intermittent at spec timing (%d/3 replies); stable with +%u ms marking, +%u ms reply"
not found: set (78,0); send_break; up to LATE_RETRIES acknowledges, each preceded by
   delay(LATE_RETRY_GAP_MS) except the first        (§7.2: last retry > 100 ms after break)
   answered                     → PASS "Wakes only after 87 ms; reached by a §7.2 retry >100 ms after the break. Later tests may miss it"  (keep (78,0))
   no answer                    → set (0,0); FAIL "No answer within §7.0's 100 ms wake window, or no sensor at this address"
```

Write § as `"\xc2\xa7"` in C strings, matching the existing tests.

- [ ] **Step 2: Integrate**

- In `verifier_run_sensor_tests`, before the identity pre-scan, call `wake_probe_run`. Then `report_add_result` it, copy the master's margins into `report_t`, and in verbose mode print it in the same row format `test_suite_run` uses. The verbose "Tests:" count becomes `suite.count + 1`. The probe runs even with `--test`.
- Reporter: when either margin is non-zero, the text header prints `  Margins:   +%u ms marking, +%u ms reply (lenient run)`. JSON always emits `"margins": {"marking_ms": M, "reply_ms": R}`.

- [ ] **Step 3: Verify**

Run: `cmake --build build && ./build/sdi12-verifier --self-test | grep -E "Wake-up|SUMMARY"`
Expected: the probe shows PASS with "Wakes and replies at spec timing"; SUMMARY passes = BASELINE + 1, with no new failures.

Run: `./build/sdi12-verifier --self-test --format json | grep '"margins"'`
Expected: `"margins": {"marking_ms": 0, "reply_ms": 0}`

Run: `./build/sdi12-verifier --self-test --test acknowledge | grep Wake-up`
Expected: one probe line.

- [ ] **Step 4: Commit**

```bash
git add include/wake_probe.h src/wake_probe.c CMakeLists.txt src/main.c include/reporter.h src/reporter.c
git commit -m "feat: wake/reply probe before the sensor suite; report lenient runs"
```

### Task 7: Loopback faults that exercise the probe

**Files:**
- Modify: `include/hal_loopback.h` (`lb_fault_t` `:20-70`), `hal/hal_loopback.c` (priv `:95-115`, `lb_flush :315`, `lb_read :336`, `lb_set_break :387`, both `lb_delay_ms` variants `:422,:442`)
- Modify: `src/selftest_faults.c` (new tests; `selftest_fault_tests_register :555`)

**Interfaces:**
- Consumes: `wake_probe_run` (Task 6).
- Produces:
  - `LB_FAULT_SLOW_WAKE = 1u << 7` and `LB_FAULT_LATE_REPLY = 1u << 8`.
  - `#define LB_SLOW_WAKE_MS 20` and `#define LB_LATE_REPLY_MS 50`.

- [ ] **Step 1: Implement the faults**

- **`LB_FAULT_SLOW_WAKE`:** priv gets `uint32_t wake_ms; bool awake;`. `lb_set_break(false)` resets both, and every `lb_delay_ms` adds its requested `ms` to `wake_ms`. In `lb_flush`, if the fault is set and the sensor isn't awake: drop the command when `wake_ms < LB_SLOW_WAKE_MS` (clear `cmd_len`, no processing); otherwise set `awake = true`.
- **`LB_FAULT_LATE_REPLY`:** reuse the pacing state. A response becomes readable only once `LB_LATE_REPLY_MS` of wall-clock time has passed since the flush.
- Each fault gets a one-line doc comment in the enum, matching its neighbours.

- [ ] **Step 2: Write the fault self-tests**

Every test ends with `hal_loopback_set_faults(ctx->hal, LB_FAULT_NONE)` and `sdi12_master_set_timing_margin(timing_master(ctx), 0, 0)`. It captures the margins before that reset.

- `fault_slow_wake`: probe status PASS, captured marking > 0, reply 0. Other HAL delays between break end and the command also count toward `wake_ms`, so don't assert an exact margin.
- `fault_late_reply`: probe status FAIL, captured reply ≥ `LB_LATE_REPLY_MS - SDI12_FIRST_BYTE_TIMEOUT_MS` (25).
- `fault_probe_absent`: probe `addr == '9' ? '8' : '9'` (an address the loopback doesn't answer). Expect status FAIL, margins after the run (0, 0), and elapsed `micros` < 30 s.
- `fault_probe_cleanup` (register last): margins (0, 0), and `sdi12_master_acknowledge` at `addr` succeeds.

- [ ] **Step 3: Verify**

Run: `cmake --build build && ./build/sdi12-verifier --self-test | grep -E "fault_|Slow|Late|Absent|Cleanup|SUMMARY"`
Expected: the four new tests PASS; SUMMARY passes = BASELINE + 5, with no new failures or errors.

- [ ] **Step 4: Commit**

```bash
git add include/hal_loopback.h hal/hal_loopback.c src/selftest_faults.c
git commit -m "test: loopback slow-wake and late-reply faults exercise the wake probe"
```

### Task 8: Verifier docs and release 0.10.0

**Files:**
- Modify: `README.md`, `CMakeLists.txt:2`, `include/verifier.h:9`, `debian/changelog`

- [ ] **Step 1: README**

- Counts: 47 → 48 compliance tests (31 → 32 sensor); `--self-test` fault tests 9 → 13.
- Add a probe row to the sensor table.
- Add one sentence explaining a "lenient run".
- Refresh any sample SUMMARY output.

- [ ] **Step 2: Version**

- Bump `CMakeLists.txt` and `VERIFIER_VERSION` to 0.10.0.
- Prepend a `debian/changelog` entry in that file's existing format.

- [ ] **Step 3: Verify**

Run: `cmake --build build && ./build/sdi12-verifier --self-test | grep SUMMARY`
Expected: matches Task 7.

- [ ] **Step 4: Commit, tag, and ask before pushing**

Commit `chore: release 0.10.0`. Tag in the verifier's existing format (check `git tag --sort=-creatordate | head -3`). Ask Phillip before pushing. Then update the self-test baseline in the `project-libsdi12-release` memory to the new SUMMARY.
